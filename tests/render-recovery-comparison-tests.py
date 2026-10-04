"""CPU regressions for independent fixed-count recovery references."""
import importlib.util
from pathlib import Path
import unittest

import numpy as np

spec = importlib.util.spec_from_file_location(
    'recovery_comparisons', Path(__file__).with_name('render-recovery-comparisons.py'))
comparisons = importlib.util.module_from_spec(spec)
import_failure = None
try:
    spec.loader.exec_module(comparisons)
except (IndexError, OSError) as error:
    import_failure = str(error)


class RecoveryComparisonTests(unittest.TestCase):
    def setUp(self):
        self.width, self.height = 1280, 25
        self.base = np.zeros((self.height, self.width, 4), dtype=np.uint8)
        self.base[:, :, 3] = 255

    def compare(self, controls, recovered, width=None, height=None):
        self.assertIsNone(import_failure, 'recovery checker performs fixture I/O on import')
        compare = getattr(comparisons, 'compare_arrays', None)
        self.assertIsNotNone(compare, 'importable recovery reference validation is missing')
        return compare(controls, recovered, width or self.width, height or self.height)[0]

    def changed(self, pixels, amount=1):
        value = self.base.copy()
        value.reshape(-1, 4)[:pixels, 0] = amount
        return value

    def test_checker_import_does_not_read_fixture_arguments(self):
        self.assertIsNone(import_failure, 'recovery checker performs fixture I/O on import')

    def test_larger_pose_uses_five_independent_samples(self):
        result = self.compare([self.base] * 5, [self.base] * 5)
        self.assertTrue(result['passed'])
        self.assertEqual(len(result['individual']), 5)

    def test_small_pose_uses_twenty_nine_independent_samples(self):
        base = np.zeros((25, 480, 4), dtype=np.uint8)
        base[:, :, 3] = 255
        result = self.compare([base] * 29, [base] * 29, width=480, height=25)
        self.assertTrue(result['passed'])
        self.assertEqual(len(result['individual']), 29)

    def test_incomplete_or_extra_sample_count_fails(self):
        for count in (1, 3, 4, 6):
            with self.subTest(count=count), self.assertRaises(ValueError):
                self.compare([self.base] * count, [self.base] * count)

    def test_small_pose_rejects_old_five_sample_count(self):
        base = np.zeros((25, 480, 4), dtype=np.uint8)
        with self.assertRaises(ValueError):
            self.compare([base] * 5, [base] * 5, width=480, height=25)

    def test_missing_recovered_sample_fails(self):
        with self.assertRaises(ValueError):
            self.compare([self.base] * 5, [self.base] * 4)

    def test_inconsistent_image_dimensions_fail(self):
        with self.assertRaises(ValueError):
            self.compare([self.base] * 5, [self.base] * 4 + [self.base[:24]])

    def test_dimensions_must_match_requested_pose(self):
        with self.assertRaises(ValueError):
            self.compare([self.base] * 5, [self.base] * 5, height=24)

    def test_float_sample_type_fails(self):
        with self.assertRaises(ValueError):
            self.compare([self.base] * 5, [self.base] * 4 + [self.base.astype(np.float32)])

    def test_rgb_sample_without_alpha_fails(self):
        with self.assertRaises(ValueError):
            self.compare([self.base[:, :, :3]] * 5, [self.base] * 5)

    def test_isolated_one_level_outlier_preserves_raw_evidence(self):
        result = self.compare([self.changed(64)] + [self.base] * 4, [self.base] * 5)
        self.assertTrue(result['passed'])
        self.assertEqual(result['individual'][0], {'maximum': 1, 'fraction': .002})
        self.assertEqual(result['median'], {'maximum': 0, 'fraction': 0})

    def test_persistent_one_level_fraction_regression_fails(self):
        result = self.compare([self.base] * 5, [self.changed(33)] * 5)
        self.assertFalse(result['passed'])
        self.assertEqual(result['median']['maximum'], 1)

    def test_exact_fraction_boundary_remains_allowed(self):
        result = self.compare([self.base] * 5, [self.changed(32)] * 5)
        self.assertTrue(result['passed'])
        self.assertEqual(result['median']['fraction'], .001)

    def test_persistent_color_regression_fails_maximum_gate(self):
        result = self.compare([self.base] * 5, [self.changed(1, 2)] * 5)
        self.assertFalse(result['passed'])
        self.assertEqual(result['median']['fraction'], 1 / 32000)

    def test_persistent_line_displacement_fails(self):
        control, recovered = self.base.copy(), self.base.copy()
        control[:, 400, :3] = 100
        recovered[:, 401, :3] = 100
        self.assertFalse(self.compare([control] * 5, [recovered] * 5)['passed'])

    def test_transient_two_level_excursion_is_not_hidden(self):
        result = self.compare([self.changed(1, 2)] + [self.base] * 4, [self.base] * 5)
        self.assertFalse(result['passed'])
        self.assertEqual(result['median']['maximum'], 0)
        self.assertEqual(result['raw_to_median']['control'][0]['maximum'], 2)


if __name__ == '__main__':
    unittest.main(verbosity=2)
