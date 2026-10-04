from camera_projection import load_camera as _load_camera, legacy_fixture_camera, project_world
"""Run the retained GPU invariants relevant to the current cockpit landscape.

The older full terrain suite includes pavement crops below the windshield and
requirements for superseded visual designs. Keep those historical checks intact.
"""
import argparse
import sys
_LEGACY_CAMERA = "--legacy-camera" in sys.argv
if _LEGACY_CAMERA: sys.argv.remove("--legacy-camera")
def load_camera(directory):
    return legacy_fixture_camera() if _LEGACY_CAMERA else _load_camera(directory)
import importlib.util
from pathlib import Path
import unittest
import numpy as np
from PIL import Image

DIRECTORY = Path()


class RoadGlowQuality(unittest.TestCase):
    def test_halo_continues_past_both_dash_ends(self):
        for palette in ('magenta', 'cyan', 'amber'):
            with self.subTest(palette=palette):
                with Image.open(DIRECTORY / (palette + '.png')) as image:
                    frame = np.asarray(image.convert('RGB')).astype(np.float32)
                h, w = frame.shape[:2]
                # Inspect the centerline through the open windshield. Bright
                # cores locate the actual ends; outside each end a halo must
                # decay across several pixels before reaching dark pavement.
                column = frame[:, w//2].max(axis=1)
                core = column > 130
                starts = np.flatnonzero(~core[:-1] & core[1:]) + 1
                ends = np.flatnonzero(core[:-1] & ~core[1:]) + 1
                starts = starts[(starts > h*.45) & (starts < h*.68)]
                ends = ends[(ends > h*.45) & (ends < h*.69)]
                self.assertGreaterEqual(len(starts), 2)
                self.assertGreaterEqual(len(ends), 2)
                for start in starts:
                    self.assertGreater(float(column[start-5:start-2].mean()), 15,
                                       'dash top has a luminous halo beyond its core')
                for end in ends:
                    self.assertGreater(float(column[end+2:end+5].mean()), 15,
                                       'dash bottom has a luminous halo beyond its core')


class TerrainBaseQuality(unittest.TestCase):
    def assert_stable_intersections(self, paths):
        peaks = []
        for path in paths:
            with Image.open(path) as image:
                frame = np.asarray(image.convert('RGB'))
            self.assertEqual(frame.shape, (720, 1280, 3))
            # Fixed seed, cell 0, phases .25 through .425: the left foreground
            # rock meets the plain here. Intersections must have one line's
            # emission, not isolated near-white sparks from additive overlap.
            base = frame[278:309, 155:325].max(axis=2)
            peaks.append(int(base.max()))
            self.assertLessEqual(int(base.max()), 195,
                                 f'{path.name}: crease intersection flashes')
            self.assertGreater(int((base > 120).sum()), 100,
                               'the grid must remain visible at terrain bases')
        self.assertLessEqual(max(peaks) - min(peaks), 8,
                             'intersection brightness should remain stable in motion')

    def test_fold_intersections_do_not_sparkle(self):
        self.assert_stable_intersections(
            [DIRECTORY / f'base-{frame}.png' for frame in range(8)])

    def assert_stable_distant_lines(self, paths):
        frames = []
        for path in paths:
            with Image.open(path) as image:
                frames.append(np.asarray(image.convert('RGB')).astype(np.float32))
        self.assertEqual(len(frames), 16)
        # Follow three unobstructed ground joins, rather than a stationary
        # screen crop. Coordinates are literal cells from the fixture terrain.
        # Longitudinal lines are checked for retained local light; the cross
        # line is checked for peak pulsing as it moves between pixel rows.
        for x_cell, z_cell, statistic, limit, retained_light in (
                (-14, 16.5, 'mean', .10, 54),
                (-13, 15.5, 'mean', .10, 44),
                (-10.5, 17, 'max', .12, 104)):
            values = []
            brightness = []
            for index, frame in enumerate(frames):
                self.assertEqual(frame.shape, (720, 1280, 3))
                phase = .25 + .025 * index
                camera = load_camera(DIRECTORY)
                depth = camera["cell_size"] * (z_cell - phase)
                # Project world height 0 through the fixture's .8 focal scale,
                # 3.2 camera height and 1/3-screen horizon; address pixel centers.
                x, y, _ = project_world(camera, frame.shape[1], frame.shape[0], camera["cell_size"]*x_cell, 0, depth)
                x,y = round(x),round(y)
                patch = frame[y-1:y+2, x-1:x+2, 0]
                values.append(float(patch.mean() if statistic == 'mean' else patch.max()))
                brightness.append(float(patch.mean()))
            variation = float(np.std(values) / np.mean(values))
            with self.subTest(x_cell=x_cell, z_cell=z_cell):
                self.assertLess(variation, limit, 'distant base line blinks during subpixel motion')
                self.assertGreater(np.mean(values), retained_light,
                                   'filtering must retain the local grid light')
                self.assertGreater(min(brightness), 50, 'stability must retain visible line light')

    def test_distant_base_lines_remain_visible_in_motion(self):
        self.assert_stable_distant_lines(
            [DIRECTORY / f'base-{frame}.png' for frame in range(16)])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    args = parser.parse_args()
    DIRECTORY = args.directory
    path = Path(__file__).with_name('terrain-quality-tests.py')
    spec = importlib.util.spec_from_file_location('terrain_quality', path)
    terrain = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(terrain)
    terrain.DIRECTORY = args.directory
    names = (
        'test_continuous_world_manifest',
        'test_adjacent_world_wrap',
        'test_different_world_sections',
        'test_slope_motion_and_unobstructed_sky',
        'test_grid_reaches_marked_horizon_distance',
        'test_ground_blends_into_horizon',
        'test_hints_survive_terrain',
        'test_asymmetric_mountains',
    )
    suite = unittest.TestSuite(terrain.TerrainQuality(name) for name in names)
    suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(RoadGlowQuality))
    suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(TerrainBaseQuality))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(0 if result.wasSuccessful() else 1)
