"""Compare recorded input envelopes. Read-only; never authorizes restoration."""
import argparse
import csv
import json


def read(path):
    with open(path, newline='') as stream:
        return [{key: int(value) for key, value in row.items()}
                for row in csv.DictReader(stream)]


def audit(local, remote):
    for rows in (local, remote):
        if not rows:
            raise ValueError('Empty capture')
        for i, row in enumerate(rows):
            if row['sample'] != i or row['word13'] != 0:
                raise ValueError('Invalid sample index or reserved word')
            if any(not 0 <= row[f'word{w}'] <= 0xffffffff for w in range(14)):
                raise ValueError('Word outside uint32 range')
            if i and row['word0'] != (rows[i-1]['word0'] + 1) & 0xffffffff:
                raise ValueError('Counter discontinuity')
        if len({r['word0'] for r in rows}) != len(rows):
            raise ValueError('Duplicate counter')
    other = {r['word0']: r for r in remote}
    differences = [0] * 10
    first = [None] * 10
    overlap = equal = active = 0
    for row in local:
        peer = other.get(row['word0'])
        if peer is None:
            continue
        overlap += 1
        active += any(row[f'word{w}'] for w in range(1, 5))
        changed = False
        for w in range(1, 11):
            if row[f'word{w}'] != peer[f'word{w}']:
                changed = True
                differences[w-1] += 1
                if first[w-1] is None:
                    first[w-1] = {'counter': row['word0'], 'local': row[f'word{w}'],
                                  'remote': peer[f'word{w}']}
        equal += not changed
    return {'schema': 1, 'overlapping_counters': overlap,
            'local_unmatched': len(local)-overlap, 'remote_unmatched': len(remote)-overlap,
            'active_button_records': active, 'equal_full_envelopes': equal,
            'word_mismatch_counts': differences, 'first_differences': first,
            'button_pairs_match': bool(overlap) and not any(differences[:4]),
            'full_envelopes_match': bool(overlap) and not any(differences),
            'network_restore_authorized': False,
            'limitation': 'Equal counters are assumed alignment; matching buttons alone do not establish frame identity or auxiliary-field ownership.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('local')
    parser.add_argument('remote')
    args = parser.parse_args()
    print(json.dumps(audit(read(args.local), read(args.remote)), indent=2))
