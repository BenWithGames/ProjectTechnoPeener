"""Recorded-input alignment evidence and native composer fixtures; no game writes."""
import argparse
import json
from pathlib import Path
from audit_network_envelopes import read, audit


def buttons(row, swapped=False):
    pair = tuple(row[f'word{w}'] for w in range(1, 5))
    return pair[2:] + pair[:2] if swapped else pair


def analyze(local, remote, radius=8):
    audit(local, remote)  # Reject malformed/discontinuous captures first.
    by_frame = {r['word0']: r for r in remote}
    # Compare every candidate over the SAME central local frames. Otherwise
    # an offset with almost no overlap could look better than the true offset.
    central = [r for r in local if all(
        ((r['word0'] + offset) & 0xffffffff) in by_frame
        for offset in range(-radius, radius+1))]
    if not central:
        raise ValueError('Insufficient common window')
    candidates = []
    for swapped in (False, True):
        for offset in range(-radius, radius+1):
            mismatches = sum(buttons(r) != buttons(by_frame[
                (r['word0'] + offset) & 0xffffffff], swapped) for r in central)
            candidates.append({'offset': offset, 'swapped': swapped,
                               'mismatches': mismatches})
    ranked = sorted(candidates, key=lambda c: c['mismatches'])
    transitions = sum(buttons(a) != buttons(b) for a, b in zip(central, central[1:]))
    evidence = (ranked[0]['offset'] == 0 and not ranked[0]['swapped']
                and ranked[0]['mismatches'] == 0 and ranked[1]['mismatches'] > 0
                and transitions >= 8)
    return {'schema': 1, 'compared_frames': len(central),
            'button_transitions': transitions, 'search_radius': radius,
            'unique_zero_offset_evidence': evidence, 'candidates': ranked,
            'live_alignment_verified': False,
            'limitation': 'This bounded recorded window cannot establish live session/round identity or pre-delay input availability.'}


def fixtures(local, remote, path):
    peer = {r['word0']: r for r in remote}
    lines = []
    for row in local:
        other = peer.get(row['word0'])
        if other is None:
            continue
        for slot in (1, 2):
            values = [row['word0'], other['word0'], slot]
            values += [row[f'word{w}'] for w in range(1, 11)]
            values += [other[f'word{w}'] for w in range(1, 11)]
            lines.append(' '.join(map(str, values)))
    path.write_text('\n'.join(lines)+'\n', encoding='ascii')
    return len(lines)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('local')
    parser.add_argument('remote')
    parser.add_argument('--fixtures', type=Path)
    args = parser.parse_args()
    local, remote = read(args.local), read(args.remote)
    result = analyze(local, remote)
    if args.fixtures:
        if not result['unique_zero_offset_evidence']:
            raise SystemExit('No unambiguous zero-offset evidence; no fixtures exported')
        result['composer_cases'] = fixtures(local, remote, args.fixtures)
    print(json.dumps(result, indent=2))
