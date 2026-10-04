"""Known-data checks: wrong ranks, zero cadence, and submitted-as-display must fail."""
import importlib.util
from pathlib import Path
import sys
import unittest
sys.dont_write_bytecode = True

source = Path(__file__).resolve().parents[1] / 'tools' / 'analyze-performance.py'
spec = importlib.util.spec_from_file_location('analysis', source)
analysis = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = analysis
spec.loader.exec_module(analysis)


class AnalysisTests(unittest.TestCase):
    def test_known_nearest_rank_distribution(self):
        result = analysis.summarize(list(range(1, 101)))
        self.assertIsNotNone(result)
        self.assertEqual(result['median_ms'], 50.5)
        self.assertEqual(result['p95_ms'], 95)
        self.assertEqual(result['p99_ms'], 99)
        self.assertEqual(result['worst_ms'], 100)
        self.assertAlmostEqual(result['average_fps'], 1000 / 50.5)

    def test_no_samples_are_unavailable(self):
        self.assertIsNone(analysis.summarize([]))

    def test_submitted_intervals_cannot_prove_displayed_cadence(self):
        result = analysis.analyze_presentmon([{'MsBetweenPresents': '6.94'}] * 100)
        self.assertIsNone(result['displayed'])
        self.assertEqual(analysis.gates(result, 144, 1920, 1080)['minimum'], 'unverified')

    def test_displayed_time_preserves_gaps_and_counts_bad_data(self):
        rows = [{'DisplayedTime': x, 'ProcessID': '12', 'SwapChainAddress': '1'}
                for x in ['6.94', '6.94', '20', 'NA', '0', '-1', 'nan', 'bad']]
        result = analysis.analyze_presentmon(rows)
        self.assertIsNotNone(result['displayed'])
        self.assertEqual(result['displayed']['count'], 3)
        self.assertEqual(result['displayed']['worst_ms'], 20)
        self.assertEqual(result['undisplayed_rows'], 1)
        self.assertEqual(result['invalid_display_rows'], 4)
        self.assertEqual(analysis.gates(result, 144, 1920, 1080)['minimum'], 'fail')

    def test_multiple_swapchains_cannot_be_merged(self):
        rows = [{'DisplayedTime': '7', 'ProcessID': '12', 'SwapChainAddress': x}
                for x in ['1', '2']]
        with self.assertRaises(ValueError):
            analysis.analyze_presentmon(rows)

    def test_preferred_requires_reference_output_and_refresh(self):
        result = analysis.analyze_presentmon([{'DisplayedTime': '6.94'}] * 100)
        self.assertEqual(analysis.gates(result, 144, 1920, 1080)['preferred'], 'pass')
        self.assertEqual(analysis.gates(result, 144, 2560, 1440)['preferred'], 'unverified')
        self.assertEqual(analysis.gates(result, 60, 1920, 1080)['minimum'], 'unverified')

    def test_app_cpu_gpu_do_not_include_present_wait_or_invalid_queries(self):
        rows = [{'cpu_active_ms': '0.4', 'present_ms': '6', 'gpu_ms': '2'},
                {'cpu_active_ms': '0.6', 'present_ms': '7', 'gpu_ms': ''}]
        result = analysis.analyze_app(rows)
        self.assertIsNotNone(result['cpu_active'])
        self.assertEqual(result['cpu_active']['p99_ms'], 0.6)
        self.assertEqual(result['gpu']['count'], 1)
        self.assertEqual(result['present_api']['p99_ms'], 7)

    def test_gpu_missing_and_disjoint_data_stay_visible(self):
        result = analysis.analyze_app([{'gpu_ms': '2'}, {'gpu_ms': ''}, {'gpu_ms': 'nan'}])
        self.assertEqual(result.get('gpu_missing_or_invalid_rows'), 2)


if __name__ == '__main__':
    unittest.main()
