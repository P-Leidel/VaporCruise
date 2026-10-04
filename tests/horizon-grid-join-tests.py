from camera_projection import load_camera as _load_camera, legacy_fixture_camera, project_world
"""Native GPU regressions for opaque skyline bases, continuous fog and slope grids.

Capture 48 poses with tests/capture-grid-motion.ps1 using its defaults.
Literal pixel/world fixtures refer to this fixed seed and 1280x720 camera.
"""
import argparse
import sys
_LEGACY_CAMERA = "--legacy-camera" in sys.argv
if _LEGACY_CAMERA: sys.argv.remove("--legacy-camera")
def load_camera(directory):
    return legacy_fixture_camera() if _LEGACY_CAMERA else _load_camera(directory)
from pathlib import Path
import unittest
import numpy as np
from PIL import Image

DIRECTORY = Path()


def read(index):
    with Image.open(DIRECTORY / f'motion-p0-{index:02}.png') as image:
        return np.asarray(image.convert('RGB'), dtype=np.float32)


class HorizonGridJoins(unittest.TestCase):
    def test_short_building_base_occludes_the_lowest_sun_stripe(self):
        frame = read(0)
        # The small central tower's lower facade crosses the last pink stripe.
        # A coverage fade previously let that stripe shine through the building.
        facade = frame[224:226, 625:635, 0]
        self.assertLess(float(facade.mean()), 100,
                        'sunlight must illuminate, not make the facade transparent')

    def test_fog_has_no_dark_trench_at_the_mountain_foot(self):
        frame = read(0)
        # An unobstructed mountain foot and the adjacent low haze: the old
        # per-material fog strengths left a dark horizontal gap between them.
        foot = np.median(frame[213:221, 100:250, 0])
        haze = np.median(frame[229:233, 100:250, 0])
        self.assertGreater(float(foot/haze), .65,
                           'mountain foot must blend continuously into the fog')

    def test_raised_square_edges_keep_light_without_pulsing(self):
        frames = [read(index) for index in range(48)]
        for x_cell, z_cell, height in ((-20, 37.5, .559389235),
                                     (-21, 38.5, .679258405)):
            peaks = []
            for index, frame in enumerate(frames):
                camera=load_camera(DIRECTORY)
                depth=camera["cell_size"]*(z_cell-index/47)
                x,y,_=project_world(camera,frame.shape[1],frame.shape[0],camera["cell_size"]*x_cell,height,depth)
                x,y=round(x),round(y)
                peaks.append(float(frame[y-1:y+2, x-1:x+2, 0].max()))
            with self.subTest(x=x_cell, z=z_cell):
                print(f'raised edge {x_cell}, {z_cell}: mean peak {np.mean(peaks):.2f}, CV {np.std(peaks)/np.mean(peaks):.4f}')
                self.assertGreater(np.mean(peaks), 70,
                                   'one compressed axis must not erase the other grid direction')
                self.assertLess(np.std(peaks)/np.mean(peaks), .05,
                                'retained slope lighting must remain stable through motion')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    args, extra = parser.parse_known_args()
    DIRECTORY = args.directory
    unittest.main(argv=[__file__, *extra], verbosity=2)
