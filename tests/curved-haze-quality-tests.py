"""Paired native GPU checks for a curved horizon veil and readable scenery.

The baseline must use the same executable settings, route pose and cockpit.
Removing the veil, flattening it, or extending it over the foreground should
fail these checks. No Python approximation of the shader is used.
"""
import argparse
from pathlib import Path
import unittest

import numpy as np
from PIL import Image

DIRECTORY = Path()
BASELINE = Path()


def rgb(directory, name):
    with Image.open(directory / (name + '.png')) as image:
        return np.asarray(image.convert('RGB')).astype(float)


class CurvedHazeQuality(unittest.TestCase):
    def test_haze_rises_toward_both_edges(self):
        for name in ('magenta', 'cyan', 'amber', 'portrait', 'ultrawide', 'small'):
            with self.subTest(view=name):
                frame, baseline = rgb(DIRECTORY, name), rgb(BASELINE, name)
                self.assertEqual(frame.shape, baseline.shape)
                h, w = frame.shape[:2]
                # Ignore the narrow old neon stripe; measure the broad added
                # atmosphere, away from the cockpit pillars and upper sun.
                addition = np.maximum(frame - baseline, 0).max(axis=2)
                y0, y1 = round(h * .22), round(h * .44)
                heights = np.arange(y0, y1) / h
                def profile(x0, x1):
                    return np.median(addition[y0:y1, round(w*x0):round(w*x1)], axis=1)
                center = profile(.485, .515)
                center_height = float(np.dot(heights, center) / max(center.sum(), 1))
                for x0, x1 in ((.15, .21), (.79, .85)):
                    side = profile(x0, x1)
                    self.assertGreater(float(side.max()), 10, 'a visible veil must reach both sides')
                    side_height = float(np.dot(heights, side) / max(side.sum(), 1))
                    self.assertLess(side_height, center_height - .012,
                                    'the haze must form an arc, not a horizontal band')
                    self.assertGreater(int((side > 6).sum()), round(h*.035),
                                       'the haze must have a soft broad falloff, not a thin stripe')

    def test_foreground_and_upper_sky_stay_unchanged(self):
        for name in ('magenta', 'cyan', 'amber'):
            with self.subTest(palette=name):
                frame, baseline = rgb(DIRECTORY, name), rgb(BASELINE, name)
                h = frame.shape[0]
                np.testing.assert_array_equal(frame[:round(h*.19)], baseline[:round(h*.19)])
                # Recompiled GPU arithmetic can move an encoded channel by
                # one quantization level; a visible foreground change cannot.
                difference = np.abs(frame[round(h*.56):] - baseline[round(h*.56):])
                self.assertLessEqual(float(difference.max()), 1)
                self.assertLess(float(difference.mean()), .01)

    def test_distant_grid_does_not_cut_a_straight_strip_through_haze(self):
        for name in ('magenta', 'cyan', 'amber'):
            with self.subTest(palette=name):
                frame = rgb(DIRECTORY, name)
                h, w = frame.shape[:2]
                # The empty right shoulder spans the sky/ground join. Median
                # removes isolated wires while retaining a full-width seam.
                region = frame[round(h*.328):round(h*.347), round(w*.73):round(w*.78)]
                profile = np.median(region, axis=1)
                self.assertLess(float(np.abs(np.diff(profile, axis=0)).max()), 8,
                                'distant grid density must not produce a hard horizon stripe')

    def test_grid_and_city_remain_readable(self):
        for name in ('magenta', 'cyan', 'amber'):
            with self.subTest(palette=name):
                frame = rgb(DIRECTORY, name)
                h, w = frame.shape[:2]
                grid = frame[round(h*.45):round(h*.56), round(w*.16):round(w*.32)].max(axis=2)
                self.assertGreater(float(np.quantile(grid, .98)-np.quantile(grid, .10)), 65,
                                   'glowing grid cores must remain distinct beneath the haze')
                city = frame[round(h*.274):round(h*.318), round(w*.60):round(w*.71)]
                warm = (city[:,:,0] > city[:,:,2]*1.18) & (city[:,:,1] > city[:,:,2]*1.08)
                self.assertGreater(int(warm.sum()), 20, 'city windows must survive the veil')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    args, extra = parser.parse_known_args()
    DIRECTORY, BASELINE = args.directory, args.baseline
    unittest.main(argv=['curved-haze-quality'] + extra)
