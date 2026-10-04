from camera_projection import load_camera as _load_camera, legacy_fixture_camera, project_world
"""Track unobstructed grid lines through a complete cell of native GPU motion.

Capture with tests/capture-grid-motion.ps1 at 1280x720, 48 frames, cell 0,
CellSpan 1, default seed and palette 0. Small short-span patches previously
missed brightness modulation in the rest of the cell cycle.
"""
import argparse
import sys
_LEGACY_CAMERA = "--legacy-camera" in sys.argv
if _LEGACY_CAMERA: sys.argv.remove("--legacy-camera")
def load_camera(directory):
    return legacy_fixture_camera() if _LEGACY_CAMERA else _load_camera(directory)
import json
from pathlib import Path
import unittest

import numpy as np
from PIL import Image

DIRECTORY = Path()


class GridMotionStability(unittest.TestCase):
    def test_tracked_lines_do_not_blink_over_a_complete_cell(self):
        poses = json.loads((DIRECTORY / 'motion-p0.json').read_text(encoding='utf-8-sig'))
        self.assertEqual(len(poses), 48)
        self.assertEqual(float(poses[0]['Phase']), 0)
        self.assertEqual(int(poses[-1]['Cell']), 1)
        frames = []
        for index, pose in enumerate(poses):
            self.assertEqual((pose['Width'], pose['Height'], pose['Seed']),
                             (1280, 720, '6215337262362608466'))
            self.assertAlmostEqual(int(pose['Cell']) + float(pose['Phase']), index / 47)
            with Image.open(DIRECTORY / (pose['Name'] + '.png')) as image:
                frame = np.asarray(image.convert('RGB'), dtype=np.float32)
                self.assertEqual(frame.shape, (720, 1280, 3))
                frames.append(frame)
        # Literal ground-level fixtures. Their terrain height is zero and the
        # camera-to-point rays are clear throughout the movement. A line must
        # keep its light as it moves between pixel centers, including the more
        # compressed transverse line. Landscape checks separately require motion.
        # Raised edge heights are native, hand-checked fixture interpolation.
        # Independent first-hit checks keep other faces out of these crops.
        # Raised faces already have lower emission because of their resolution
        # fade. Their light floors preserve that baseline instead of requiring
        # the plain's brightness; the .05 CV ceiling catches their edge pulses.
        for x_cell, z_cell, height, limit, peak_floor, mean_floor in (
                (-14, 16.5, 0, .10, 65, 45), (-5.5, 30, 0, .10, 65, 45),
                (-20, 37.5, .559389235, .05, 55, 35),
                (-21, 38.5, .679258405, .05, 55, 35)):
            peaks, means = [], []
            for index, frame in enumerate(frames):
                camera = load_camera(DIRECTORY)
                depth = camera["cell_size"] * (z_cell - index / 47)
                x, y, _ = project_world(camera, frame.shape[1], frame.shape[0], camera["cell_size"]*x_cell, height, depth)
                x,y = round(x),round(y)
                crop = frame[y-1:y+2, x-1:x+2, 0]
                peaks.append(float(crop.max()))
                means.append(float(crop.mean()))
            variation = float(np.std(peaks) / np.mean(peaks))
            print(f'line ({x_cell}, {z_cell}): peak CV={variation:.4f}, '
                  f'mean peak={np.mean(peaks):.2f}, minimum mean={min(means):.2f}')
            with self.subTest(x=x_cell, z=z_cell):
                self.assertLess(variation, limit, 'grid line blinks across the full cell cycle')
                self.assertGreater(np.mean(peaks), peak_floor, 'grid line must retain visible light')
                self.assertGreater(min(means), mean_floor, 'grid line must remain visible throughout motion')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    args, extra = parser.parse_known_args()
    DIRECTORY = args.directory
    unittest.main(argv=[__file__, *extra], verbosity=2)
