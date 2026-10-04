"""Historical checker for archived interior captures from the lighting pass.

Retired when car interiors and their capture options were removed. Current
visual verification rejects this suite; the source remains for interpreting
archived before/after captures.

Removing local lighting must fail the gain checks; spilling it into glass,
washing out the grid, or brightening the pavement must fail the guards.
The original interior capture wrapper and managed profile were removed.
"""
import argparse
from pathlib import Path
import unittest
import numpy as np
from PIL import Image

BASELINE = Path()
DIRECTORY = Path()


def frame(directory, car, palette):
    with Image.open(directory / f'car-{car}-palette-{palette}.png') as image:
        return np.asarray(image.convert('RGB').resize((1280, 720)), dtype=np.float32)


class RefinedLighting(unittest.TestCase):
    def test_distant_ridges_receive_restrained_aerial_light(self):
        for palette in range(3):
            before, after = frame(BASELINE, 0, palette), frame(DIRECTORY, 0, palette)
            # Scope ridge contrast above the mountain foot. The foot now has
            # stronger continuous fog, checked by horizon-grid-join-tests.py.
            delta = after[180:210, 80:200]-before[180:210, 80:200]
            self.assertGreater(np.abs(delta).mean(), .25,
                               'distant ridges should separate through aerial perspective')
            self.assertLess(np.abs(delta).max(), 15,
                            'distant facets must retain their silhouette contrast')

    def test_sun_has_a_local_outer_glow(self):
        for palette in range(3):
            before, after = frame(BASELINE, 0, palette), frame(DIRECTORY, 0, palette)
            gain = (after-before)[100:145, 744:766].max(axis=2).mean()
            print(f'palette {palette}: sun edge gain {gain:.2f}')
            self.assertGreater(gain, 3, 'sun edge needs visible local light')
            self.assertLess(gain, 45, 'sun halo must remain restrained')
            # Upper-left open sky should retain its dark background.
            np.testing.assert_allclose(after[95:130, 100:170], before[95:130, 100:170], atol=1)

    def test_cabin_light_is_localized_and_preserves_material_detail(self):
        for car in range(3):
            for palette in range(3):
                before, after = frame(BASELINE, car, palette), frame(DIRECTORY, car, palette)
                gain = (after-before)[535:570, 800:1050].max(axis=2).mean()
                print(f'car {car} palette {palette}: dashboard gain {gain:.2f}')
                self.assertGreater(gain, 2, 'dashboard should receive reflected light')
                self.assertLess(gain, 35, 'material should not become an emissive panel')
                old_detail = np.abs(np.diff(before[535:570, 800:1050], axis=1)).mean()
                new_detail = np.abs(np.diff(after[535:570, 800:1050], axis=1)).mean()
                self.assertGreater(new_detail, old_detail*.85,
                                   'lighting must retain dashboard texture contrast')
                # Lower passenger surfaces remain recognizable and dark.
                self.assertLess(np.abs(after[650:700, 800:1050]-before[650:700, 800:1050]).mean(), 8)

    def test_sheen_does_not_wash_out_the_dark_road_or_grid(self):
        for palette in range(3):
            before, after = frame(BASELINE, 0, palette), frame(DIRECTORY, 0, palette)
            road = after[295:430, 654:690].max(axis=2)
            self.assertLess(np.percentile(road, 90), 40, 'pavement must stay dark')
            gain = (after-before)[295:430, 654:690].max(axis=2).mean()
            self.assertGreater(gain, .5, 'road needs a faint sunset sheen')
            self.assertLess(gain, 15, 'reflection must remain subtle')
            np.testing.assert_allclose(after[350:440, 170:330], before[350:440, 170:330], atol=1,
                                       err_msg='foreground grid must retain existing light and color')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--directory', type=Path, required=True)
    args, extra = parser.parse_known_args()
    BASELINE, DIRECTORY = args.baseline, args.directory
    unittest.main(argv=[__file__, *extra], verbosity=2)
