from camera_projection import load_camera as _load_camera, legacy_fixture_camera
"""Image gates for a clear distant ground without a broad luminous fog bank."""
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

class HorizonDepthQuality(unittest.TestCase):
    def test_horizon_glows_without_a_black_trough(self):
        for name in ('magenta', 'cyan', 'amber'):
            with self.subTest(palette=name):
                with Image.open(DIRECTORY / (name + '.png')) as image:
                    frame = np.asarray(image.convert('RGB')).astype(float)
                h, w = frame.shape[:2]
                camera=load_camera(DIRECTORY)
                def shoulder(offset0, offset1):
                    rows = np.arange(round(h*(camera["horizon_fraction"]+offset0)), round(h*(camera["horizon_fraction"]+offset1)))
                    columns = np.rint(w*.5 + 5/camera["height"]*(rows-h*camera["horizon_fraction"])).astype(int)
                    return float(np.median(frame[rows, columns].max(axis=1)))
                glow = shoulder(.002, .006)
                # Sample beyond the glow, where filtered ground detail resumes;
                # the transition itself should remain gradual rather than dip.
                tail = shoulder(.045, .060)
                self.assertGreater(glow, 40, 'the visible horizon must have a soft glow')
                self.assertGreater(glow-tail, 10, 'the glow must taper into darker ground')
                rows = np.arange(round(h*(camera["horizon_fraction"]+.002)), round(h*(camera["horizon_fraction"]+.045)))
                columns = np.rint(w*.5 + 5/camera["height"]*(rows-h*camera["horizon_fraction"])).astype(int)
                profile = frame[rows, columns].max(axis=1)
                self.assertGreaterEqual(float(np.quantile(profile, .05)), tail-4,
                                        'the transition must not dip into a dark block')

    def test_far_formations_do_not_pop_when_the_mesh_window_advances(self):
        frames = []
        for name in ('far-edge-before', 'far-edge-after'):
            with Image.open(DIRECTORY / (name + '.png')) as image:
                frames.append(np.asarray(image.convert('RGB')).astype(float))
        h = frames[0].shape[0]
        # This route puts a peak on the entering mesh edge. Material fading
        # alone still leaves an opaque silhouette against the background.
        delta = np.abs(frames[1]-frames[0])[:round(h/3)]
        self.assertLessEqual(float(delta.max()), 3,
                             'a new far section must not introduce a hill silhouette')

    def test_distant_ground_does_not_form_a_bright_veil(self):
        # Follow the empty flat shoulder between road and rocks. A bright
        # atmosphere-color replacement makes this whole strip luminous.
        for name in ('magenta', 'cyan', 'amber'):
            with self.subTest(palette=name):
                with Image.open(DIRECTORY / (name + '.png')) as image:
                    frame = np.asarray(image.convert('RGB')).astype(float)
                h, w = frame.shape[:2]
                camera=load_camera(DIRECTORY)
                rows = np.arange(round(h*(camera["horizon_fraction"]+.008)), round(h*(camera["horizon_fraction"]+.030)))
                columns = np.rint(w*.5 + 5/camera["height"]*(rows-h*camera["horizon_fraction"])).astype(int)
                brightness = frame[rows, columns].max(axis=1)
                self.assertLess(float(np.median(brightness)), 65,
                                'allow a narrow horizon glow without the former broad bright veil')

    def test_near_grid_stays_readable(self):
        for name in ('magenta', 'cyan', 'amber'):
            with self.subTest(palette=name):
                with Image.open(DIRECTORY / (name + '.png')) as image:
                    frame = np.asarray(image.convert('RGB')).astype(float)
                h, w = frame.shape[:2]
                camera=load_camera(DIRECTORY)
                region = frame[round(h*.65):round(h*.85), :round(w*.25)].max(axis=2)
                self.assertGreater(float(np.quantile(region, .99)-np.quantile(region, .10)), 70,
                                   'nearby neon must retain strong contrast')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    args, extra = parser.parse_known_args()
    DIRECTORY = args.directory
    unittest.main(argv=['horizon-depth-quality'] + extra)
