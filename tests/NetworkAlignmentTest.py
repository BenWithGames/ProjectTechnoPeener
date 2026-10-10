import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from check_network_alignment import analyze


def records():
    rows = []
    for i in range(80):
        r = dict(sample=i, **{f'word{w}': 0 for w in range(14)})
        r['word0'] = 100+i
        r['word1'] = i % 7
        r['word3'] = (i*i) % 11
        rows.append(r)
    return rows


class AlignmentTests(unittest.TestCase):
    def test_zero_offset(self):
        self.assertTrue(analyze(records(), records())['unique_zero_offset_evidence'])

    def test_shift_rejected(self):
        peer = records()
        for r in peer:
            r['word0'] += 2
        result = analyze(records(), peer)
        self.assertFalse(result['unique_zero_offset_evidence'])
        self.assertEqual(result['candidates'][0]['offset'], 2)

    def test_swapped_rejected(self):
        peer = records()
        for r in peer:
            r['word1'], r['word3'] = r['word3'], r['word1']
        self.assertFalse(analyze(records(), peer)['unique_zero_offset_evidence'])

    def test_idle_ambiguous(self):
        rows = records()
        for r in rows:
            r['word1'] = r['word3'] = 0
        self.assertFalse(analyze(rows, rows)['unique_zero_offset_evidence'])

    def test_no_overlap(self):
        peer = records()
        for r in peer:
            r['word0'] += 1000
        with self.assertRaises(ValueError):
            analyze(records(), peer)


if __name__ == '__main__':
    unittest.main()
