"""Native-image checks for subtle horizon light and short-interval valley drift."""
import argparse
import importlib.util
from pathlib import Path
import unittest
import numpy as np
from PIL import Image
from camera_projection import load_camera
_metrics_spec = importlib.util.spec_from_file_location(
    'neon_landscape_metrics', Path(__file__).with_name('neon-landscape-quality-tests.py'))
_metrics = importlib.util.module_from_spec(_metrics_spec)
_metrics_spec.loader.exec_module(_metrics)
shoulder_halo = _metrics.shoulder_halo

DIRECTORY = Path()
BASELINE = None
PALETTES = ('magenta', 'cyan', 'amber')


def rgb(directory, name):
    with Image.open(directory / (name + '.png')) as frame:
        return np.asarray(frame.convert('RGB'), dtype=np.float32)


def horizon_light(directory, name):
    frame = rgb(directory, name)
    h, w = frame.shape[:2]
    horizon = load_camera(directory)['horizon_fraction']
    return float(frame[round(h*(horizon-.007)):round(h*(horizon+.014)),
                       round(w*.445):round(w*.555)].mean())


class LandscapeDetailQuality(unittest.TestCase):
    def test_late_fog_has_no_center_horizon_seam(self):
        horizon = load_camera(DIRECTORY)['horizon_fraction']
        for name in PALETTES:
            frame = rgb(DIRECTORY, name+'-late-fog-small')
            h, w = frame.shape[:2]
            above = int(h*horizon)
            sky = frame[above, w//2-1:w//2+2].mean(axis=0)
            ground = frame[above+1, w//2-1:w//2+2].mean(axis=0)
            with self.subTest(palette=name):
                jump = float(np.abs(sky-ground).max())
                print(f'{name} late-fog center join: {jump:.2f} RGB8')
                self.assertLessEqual(jump, 7)

    def test_horizon_light_is_subtler_in_every_palette(self):
        if BASELINE is None:
            self.skipTest('requires the retained native baseline')
        for name in PALETTES:
            before, after = horizon_light(BASELINE, name), horizon_light(DIRECTORY, name)
            with self.subTest(palette=name):
                print(f'{name} horizon light: {before:.2f} -> {after:.2f} RGB8')
                self.assertLess(after, before*.82)

    def test_valley_fog_visibly_drifts_within_two_seconds(self):
        horizon = load_camera(DIRECTORY)['horizon_fraction']
        for name in PALETTES:
            a, b = rgb(DIRECTORY, name), rgb(DIRECTORY, name+'-fog-2s')
            h, w = a.shape[:2]
            difference = np.abs(a-b).max(axis=2)
            for side, left, right in [('left', .06, .43), ('right', .57, .94)]:
                region = difference[round(h*(horizon+.021)):round(h*(horizon+.118)),
                                    round(w*left):round(w*right)]
                with self.subTest(palette=name, side=side):
                    print(f'{name} {side} valley drift: mean {region.mean():.3f}, changed {(region>1).mean():.3f}')
                    self.assertGreater(float(region.mean()), .20)
                    self.assertGreater(float((region>1).mean()), .025)

    def test_glow_increases_modestly_in_every_palette(self):
        if BASELINE is None:
            self.skipTest('requires the retained native baseline')
        for name in PALETTES:
            before, after = shoulder_halo(BASELINE, name), shoulder_halo(DIRECTORY, name)
            with self.subTest(palette=name):
                print(f'{name} halo: {before:.2f} -> {after:.2f} RGB8')
                self.assertGreater(after, before*1.025)
                self.assertLess(after, before*1.30)

    def test_foreground_road_stays_dark_and_fog_free(self):
        for name in PALETTES:
            a, b = rgb(DIRECTORY, name), rgb(DIRECTORY, name+'-fog-2s')
            h, w = a.shape[:2]
            region = (slice(round(h*.80), round(h*.97)), slice(round(w*.54), round(w*.61)))
            with self.subTest(palette=name):
                self.assertLess(float(a[region].mean()), 12)
                self.assertLessEqual(float(np.abs(a[region]-b[region]).max()), 1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    args, extra = parser.parse_known_args()
    DIRECTORY, BASELINE = args.directory, args.baseline
    unittest.main(argv=[__file__, *extra], verbosity=2)
