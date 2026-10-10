import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from audit_network_envelopes import audit


def records(start=10, count=4):
    return [dict(sample=i, **{f'word{w}': start+i if w == 0 else 0
                             for w in range(14)}) for i in range(count)]


class AuditTests(unittest.TestCase):
    def test_equal_does_not_authorize_restore(self):
        result = audit(records(), records())
        self.assertTrue(result['full_envelopes_match'])
        self.assertFalse(result['network_restore_authorized'])

    def test_auxiliary_mismatch_hidden_by_button_check(self):
        peer = records()
        peer[1]['word5'] = 1
        result = audit(records(), peer)
        self.assertTrue(result['button_pairs_match'])
        self.assertFalse(result['full_envelopes_match'])
        self.assertEqual(result['word_mismatch_counts'][4], 1)

    def test_offset_and_no_overlap(self):
        self.assertEqual(audit(records(), records(12))['overlapping_counters'], 2)
        self.assertFalse(audit(records(), records(30))['full_envelopes_match'])

    def test_reset_rejected(self):
        peer = records()
        peer[2]['word0'] = 0
        with self.assertRaises(ValueError):
            audit(records(), peer)

    def test_wrong_slot_detected(self):
        peer = records()
        peer[0]['word3'] = 1
        self.assertFalse(audit(records(), peer)['button_pairs_match'])


if __name__ == '__main__':
    unittest.main()
