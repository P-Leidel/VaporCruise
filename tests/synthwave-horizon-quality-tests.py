"""Actual GPU checks for a luminous arc, rather than a uniform fog band."""
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


class SynthwaveHorizonQuality(unittest.TestCase):
    def test_side_glow_connects_to_the_ground_grid(self):
        for name in ('magenta', 'cyan', 'amber'):
            with self.subTest(palette=name):
                frame = rgb(DIRECTORY, name)
                h, w = frame.shape[:2]
                # Empty side shoulders inside the windshield: the halo's
                # underside must not leave a dark shelf before the dense grid.
                for x0, x1 in ((.11, .15), (.85, .89)):
                    columns = slice(round(w*x0), round(w*x1))
                    join = frame[round(h*.339):round(h*.355), columns].max(axis=2)
                    ground = frame[round(h*.36):round(h*.375), columns].max(axis=2)
                    floor = float(np.quantile(ground, .20))
                    self.assertGreaterEqual(float(np.quantile(join, .20)), floor - 8,
                                            'the lifted side halo must stay connected to the grid')

    def test_luminous_arc_has_visible_broad_shoulders(self):
        # The earlier single veil should fail: an impressive arc needs added
        # light above the flat horizon, on both sides, over more than a line.
        for name in ('magenta', 'cyan', 'amber'):
            with self.subTest(palette=name):
                frame, baseline = rgb(DIRECTORY, name), rgb(BASELINE, name)
                h, w = frame.shape[:2]
                for x0, x1 in ((.13, .23), (.77, .87)):
                    region = frame[round(h*.25):round(h*.32), round(w*x0):round(w*x1)]
                    before = baseline[round(h*.25):round(h*.32), round(w*x0):round(w*x1)]
                    lift = np.median(region.max(axis=2)-before.max(axis=2), axis=1)
                    self.assertGreater(float(lift.max()), 14, 'the arc should be visibly luminous')
                    self.assertGreater(int((lift > 8).sum()), round(h*.015),
                                       'the glow needs broad shoulders, not a thin neon stripe')
                    self.assertLess(float(np.quantile(region.max(axis=2), .98)), 165,
                                    'restrained glow must not overwhelm the sun and skyline')

    def test_terrain_silhouette_interrupts_the_glow(self):
        frame = rgb(DIRECTORY, 'magenta')
        h, w = frame.shape[:2]
        # Fixed-seed right peak, fully inside the windshield. The older left
        # crop at x=24 samples an opaque cabin pillar and cannot test terrain.
        y = round(h*236/720)
        face = frame[y-2:y+3, round(w*1070/1280)-2:round(w*1070/1280)+3].max(axis=2)
        backdrop = frame[y-2:y+3, round(w*1100/1280)-2:round(w*1100/1280)+3].max(axis=2)
        self.assertGreater(float(np.median(backdrop)-np.median(face)), 18,
                           'mist must sit behind terrain instead of painting it flat')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    args, extra = parser.parse_known_args()
    DIRECTORY, BASELINE = args.directory, args.baseline
    unittest.main(argv=['synthwave-horizon-quality'] + extra)
