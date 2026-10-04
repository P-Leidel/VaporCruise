"""CPU regressions for retained five-capture background repeat references."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

import numpy as np
from PIL import Image

spec = importlib.util.spec_from_file_location(
    'current_background', Path(__file__).with_name('current-background-quality-tests.py'))
background = importlib.util.module_from_spec(spec)
spec.loader.exec_module(background)


class BackgroundRepeatConsensus(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        previous = background.DIRECTORY
        background.DIRECTORY = self.root
        self.addCleanup(setattr, background, 'DIRECTORY', previous)
        self.zero = np.zeros((100, 100, 3), dtype=np.uint8)

    def save_references(self, stem, arrays):
        names = [stem] + [f'{stem}-reference-{index}' for index in range(1, 5)]
        for name, array in zip(names, arrays):
            Image.fromarray(array).save(self.root / (name + '.png'))

    def compare(self):
        case = background.CurrentBackground('test_repeat_capture_with_sparse_resolve_rounding')
        case.test_repeat_capture_with_sparse_resolve_rounding()

    def changed(self, pixels, amount=1):
        value = self.zero.copy()
        value.reshape(-1, 3)[:pixels, 0] = amount
        return value

    def median(self, captures):
        validate = getattr(background, 'median_reference', None)
        self.assertIsNotNone(validate, 'five-capture median validation is missing')
        return validate(captures)

    def test_median_requires_exactly_five_samples(self):
        for count in (0, 3, 4, 6, 7):
            with self.subTest(count=count), self.assertRaises(ValueError):
                self.median([self.zero.astype(np.int16)] * count)

    def test_median_rejects_inconsistent_sample_types(self):
        captures = [self.zero.astype(np.int16)] * 4 + [self.zero.astype(np.float32)]
        with self.assertRaises(ValueError):
            self.median(captures)

    def test_median_rejects_non_rgb_samples(self):
        with self.assertRaises(ValueError):
            self.median([self.zero[:, :, 0].astype(np.int16)] * 5)

    def test_isolated_one_level_outlier_does_not_fail_repeat(self):
        self.save_references('magenta', [self.changed(12)] + [self.zero] * 4)
        self.save_references('repeat', [self.zero] * 5)
        self.compare()

    def test_transient_two_level_excursion_is_rejected(self):
        self.save_references('magenta', [self.changed(1, 2)] + [self.zero] * 4)
        self.save_references('repeat', [self.zero] * 5)
        with self.assertRaises(ValueError):
            self.compare()

    def test_persistent_one_level_change_above_pixel_gate_fails(self):
        self.save_references('magenta', [self.zero] * 5)
        self.save_references('repeat', [self.changed(12)] * 5)
        with self.assertRaises(AssertionError):
            self.compare()

    def test_one_level_change_at_pixel_gate_is_allowed(self):
        self.save_references('magenta', [self.zero] * 5)
        self.save_references('repeat', [self.changed(10)] * 5)
        self.compare()

    def test_persistent_two_level_change_below_pixel_gate_fails(self):
        self.save_references('magenta', [self.zero] * 5)
        self.save_references('repeat', [self.changed(1, 2)] * 5)
        with self.assertRaises(AssertionError):
            self.compare()

    def test_missing_fifth_sample_is_rejected(self):
        self.save_references('magenta', [self.zero] * 5)
        self.save_references('repeat', [self.zero] * 4)
        with self.assertRaises(FileNotFoundError):
            self.compare()

    def test_reference_dimensions_must_agree(self):
        self.save_references('magenta', [self.zero] * 5)
        self.save_references('repeat', [self.zero] * 4 + [self.zero[:99]])
        with self.assertRaises(ValueError):
            self.compare()


if __name__ == '__main__':
    unittest.main(verbosity=2)
