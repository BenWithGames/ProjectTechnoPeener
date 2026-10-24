import importlib.util
import pathlib
import unittest

path = pathlib.Path(__file__).parents[1] / 'scripts' / 'Compare-ConfirmationTiming.py'
spec = importlib.util.spec_from_file_location('comparison', path)
comparison = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparison)


class ComparisonTest(unittest.TestCase):
    def test_offset_and_persistent_lead(self):
        a = {1: {n: n * 166667 for n in range(360)}}
        b = {1: {n: t + 100000000000 - (333333 if n >= 120 else 0)
                 for n, t in a[1].items()}}
        windows = comparison.compare(a, b, 10000000)[0]['windows']
        self.assertAlmostEqual(windows[0]['peer_phase_change_ms'], 0)
        self.assertAlmostEqual(windows[1]['peer_phase_change_ms'], -33.3333, places=5)

    def test_insufficient_and_different_round(self):
        self.assertEqual(comparison.compare({1: {0: 1}}, {1: {0: 2}}, 1), [])
        self.assertEqual(comparison.compare({1: {0: 1}}, {2: {0: 2}}, 1), [])

    def test_confirmation_boundary_attribution(self):
        outer = [{'begin_qpc': start, 'end_qpc': start+10, 'live_updates': 1}
                 for start in (100, 200, 300)]
        events = [{'round': 1, 'kind': 9, 'detail': 0, 'aux0': received, 'aux1': drained}
                  for received, drained in ((105, 205), (150, 205), (155, 305), (90, 205), (0, 205), (310, 309))]
        events += [{'round': 1, 'kind': 4, 'detail': depth} for depth in (1, 2, 2)]
        events += [{'round': 2, 'kind': 1, 'detail': 0}]
        summary = comparison.confirmation_summary(events, outer, 1000)
        self.assertEqual(len(summary), 1)
        row = summary[0]
        self.assertEqual(row['correction_depths'], {1: 1, 2: 2})
        self.assertEqual(row['corrections'], 3)
        self.assertEqual(row['receipt_locations']['inside_outer'], 1)
        self.assertEqual(row['receipt_locations']['between_outer_updates'], 2)
        self.assertEqual(row['receipt_locations']['passed_completed_live_update'], 1)
        self.assertEqual(row['receipt_locations']['outside_outer_capture_coverage'], 1)
        self.assertEqual(row['receipt_locations']['invalid_receipt_timing'], 1)
        self.assertEqual(row['receipt_locations']['unmeasured_receipt_timing'], 1)
        self.assertEqual(row['measured_receipts'], 4)
        self.assertEqual(row['receive_to_drain_mean_ms'], 105)

    def test_missing_outer_is_not_successful_admission(self):
        events = [{'round': 1, 'kind': 9, 'detail': 0, 'aux0': 100, 'aux1': 200}]
        row = comparison.confirmation_summary(events, [], 1000)[0]
        self.assertEqual(row['receipt_locations'], {'outside_outer_capture_coverage': 1})
        with self.assertRaises(ValueError):
            comparison.confirmation_summary(events, [], 0)


if __name__ == '__main__':
    unittest.main()
