"""Check paired native Debug captures with the skyline rim enabled/disabled."""
import argparse
from pathlib import Path
import unittest
import numpy as np
from PIL import Image

DIRECTORY = Path()


class SunSilhouetteLight(unittest.TestCase):
    def test_sun_adds_local_warm_edge_light(self):
        def read(name):
            with Image.open(DIRECTORY / name) as image:
                return np.asarray(image.convert('RGB'), dtype=np.float32)
        delta = read('rim-on.png') - read('rim-off.png')
        city = delta[160:242, 550:740]
        self.assertGreater(int((city[:, :, 0] > 2).sum()), 100,
                           'central skyline must receive visible sun edge light')
        gain = city.sum(axis=(0, 1))
        self.assertGreater(gain[0]+gain[1], gain[2]*3,
                           'sun illumination must have a warm color')
        self.assertLess(float(city.max()), 30,
                        'edge light must retain the dark silhouette')
        np.testing.assert_array_equal(delta[:100], 0)
        np.testing.assert_array_equal(delta[350:], 0)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    args, extra = parser.parse_known_args()
    DIRECTORY = args.directory
    unittest.main(argv=[__file__, *extra], verbosity=2)
