"""Compare fixed GPU captures: complementary grid halos, unchanged road.

Use tools/capture-visuals.ps1 for both directories with the same seed.
The baseline is the renderer before shared palette highlights.
"""
import argparse
from pathlib import Path
import unittest

import numpy as np
from PIL import Image

BASELINE = Path()
DIRECTORY = Path()


def read(path):
    with Image.open(path) as image:
        return np.asarray(image.convert('RGB'), dtype=np.float32)


class HighlightQuality(unittest.TestCase):
    def test_grid_cores_keep_the_original_palette_saturation(self):
        # Select clear line pixels using the original renderer, independently
        # of the new halo. Adding the complementary color over these pixels
        # washes out their hue even if the original emission still exists.
        for palette, main, complement in (('magenta', 0, 1), ('cyan', 1, 0), ('amber', 0, 2)):
            with self.subTest(palette=palette):
                before = read(BASELINE / (palette + '.png'))[350:440, 170:330]
                after = read(DIRECTORY / (palette + '.png'))[350:440, 170:330]
                core = before[:, :, main] > 70
                self.assertGreater(int(core.sum()), 1000)
                old_dominance = (before[:, :, main] - before[:, :, complement])[core].mean()
                new_dominance = (after[:, :, main] - after[:, :, complement])[core].mean()
                print(f'{palette}: core color dominance {new_dominance:.2f}, baseline {old_dominance:.2f}')
                self.assertGreaterEqual(new_dominance, old_dominance * .90,
                                        'contrasting halo must not wash out the core color')
                np.testing.assert_allclose(after[core], before[core], atol=1,
                                           err_msg='line cores must retain their original RGB, with no highlight tint')

    def test_grid_has_complementary_light_while_road_retains_its_color(self):
        # Clear flat ground beside the road, below terrain and horizon.
        # The highlight must contribute visible contrasting light, not just
        # turn up the original magenta/cyan/amber grid brightness.
        for palette, channel, main in (('magenta', 1, 0), ('cyan', 0, 1), ('amber', 2, 0)):
            with self.subTest(palette=palette):
                before = read(BASELINE / (palette + '.png'))
                after = read(DIRECTORY / (palette + '.png'))
                self.assertEqual(after.shape, (720, 1280, 3))
                delta = after[350:440, 170:330] - before[350:440, 170:330]
                gain = float(delta[:, :, channel].mean())
                print(f'{palette}: complementary channel mean gain {gain:.2f} RGB8')
                # A faint fringe should occupy dark pixels outside the lines,
                # not raise the average brightness across the whole grid.
                outside = before[350:440, 170:330, main] < 35
                outer_gain = delta[:, :, channel][outside]
                self.assertGreater(int((outer_gain > 3).sum()), 500,
                                   'contrasting light must remain visible outside line cores')
                self.assertLessEqual(float(np.percentile(outer_gain, 99)), 20,
                                     'exterior highlight should remain faint')
                # Pavement and center dashes inside the road exclude its edges
                # and all neighboring terrain. Exact RGB preserves road glow.
                np.testing.assert_array_equal(after[300:490, 610:670],
                                              before[300:490, 610:670])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--directory', type=Path, required=True)
    args, extra = parser.parse_known_args()
    BASELINE, DIRECTORY = args.baseline, args.directory
    unittest.main(argv=[__file__, *extra], verbosity=2)
