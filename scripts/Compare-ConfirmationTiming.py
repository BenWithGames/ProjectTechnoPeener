"""Compare paired automatic reports without extracting snapshot payloads.

Phase change is relative to the first 32 matching advances in each round.
It is not absolute clock synchronization or a one-way latency measurement.
"""
import argparse
import bisect
import collections
import csv
import io
import json
import pathlib
import re
import statistics
import zipfile


def read(path):
    rounds = {}
    grace = []
    with zipfile.ZipFile(path) as archive:
        for name in archive.namelist():
            if name.endswith('-continuous-events.csv'):
                rows = csv.DictReader(io.StringIO(archive.read(name).decode('utf-8-sig')))
                for row in rows:
                    if int(row['kind']) != 6:
                        continue
                    rounds.setdefault(int(row['round']), {}).setdefault(int(row['frame']), int(row['qpc']))
            if name.endswith('-continuous.txt'):
                grace.extend(line for line in archive.read(name).decode('utf-8-sig').splitlines()
                             if line.startswith(('receipt_grace_', 'confirmation_age_behind_frames_',
                                                 'receipt_frontier_', 'receipt_unique_gap_')))
    return rounds, grace


def confirmation_summary(events, outer, frequency):
    """Local-clock receipt attribution; no claim about wire arrival before ReadP2P."""
    if frequency <= 0:
        raise ValueError('QPC frequency must be positive')
    outer = sorted(outer, key=lambda row: row['begin_qpc'])
    begins = [row['begin_qpc'] for row in outer]
    result = []
    for round_id in sorted({row['round'] for row in events}):
        rows = [row for row in events if row['round'] == round_id]
        receipts = [row for row in rows if row['kind'] == 9]
        corrections = [row for row in rows if row['kind'] == 4]
        if not receipts and not corrections:
            continue
        delays, locations = [], collections.Counter()
        for row in receipts:
            received, drained = row['aux0'], row['aux1']
            if not received:
                locations['unmeasured_receipt_timing'] += 1
                continue
            if drained < received:
                locations['invalid_receipt_timing'] += 1
                continue
            delays.append((drained - received) * 1000 / frequency)
            i = bisect.bisect_right(begins, received) - 1
            if i < 0 or received > outer[-1]['end_qpc']:
                locations['outside_outer_capture_coverage'] += 1
                continue
            inside = received <= outer[i]['end_qpc']
            locations['inside_outer' if inside else 'between_outer_updates'] += 1
            # Conservative evidence only: was the NEXT completed live update
            # already over before drain? This does not label every native
            # callback eligible or assume input pairs can be changed mid-frame.
            j = i + 1
            while j < len(outer) and outer[j]['end_qpc'] < drained:
                if outer[j]['live_updates']:
                    locations['passed_completed_live_update'] += 1
                    break
                j += 1
        result.append({'round': round_id, 'receipts': len(receipts),
                       'corrections': len(corrections), 'measured_receipts': len(delays),
                       'correction_depths': dict(sorted(collections.Counter(row['detail'] for row in corrections).items())),
                       'receive_to_drain_mean_ms': statistics.mean(delays) if delays else None,
                       'receipt_locations': dict(locations)})
    return result


def read_confirmation_summary(path, frequency):
    events, outer = [], []
    with zipfile.ZipFile(path) as archive:
        for name in archive.namelist():
            target = (events if name.endswith('-continuous-events.csv') else
                      outer if name.endswith('-outer-timing.csv') else None)
            if target is None:
                continue
            for row in csv.DictReader(io.StringIO(archive.read(name).decode('utf-8-sig'))):
                target.append({key: int(value) for key, value in row.items()})
    return confirmation_summary(events, outer, frequency)


def compare(left, right, frequency):
    result = []
    for round_id in sorted(left.keys() & right.keys()):
        frames = sorted(left[round_id].keys() & right[round_id].keys())
        if len(frames) < 32:
            continue
        deltas = [(right[round_id][f] - left[round_id][f]) * 1000 / frequency for f in frames]
        baseline = statistics.median(deltas[:32])
        windows = []
        for start in range(0, len(frames), 120):
            end = min(start + 120, len(frames))
            windows.append({'first_frame': frames[start], 'last_frame': frames[end-1],
                            'samples': end-start,
                            'peer_phase_change_ms': statistics.median(deltas[start:end])-baseline})
        result.append({'round': round_id, 'common_frames': len(frames), 'windows': windows})
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('local')
    parser.add_argument('peer')
    parser.add_argument('--qpc-frequency', type=int, required=True,
                        help='Both reports must use this same QPC frequency')
    args = parser.parse_args()
    if args.qpc_frequency <= 0:
        parser.error('QPC frequency must be positive')
    ids = [re.match(r'match-([0-9a-f]{32})-', pathlib.Path(p).name) for p in (args.local, args.peer)]
    if not all(ids) or ids[0].group(1) != ids[1].group(1):
        parser.error('Supply reports with the same shared match ID')
    a, ag = read(args.local)
    b, bg = read(args.peer)
    print(json.dumps({'relative_phase_only': True, 'negative_means_peer_gained_lead': True,
                      'rounds': compare(a, b, args.qpc_frequency),
                      'local_counters': ag, 'peer_counters': bg,
                      'local_confirmation_summary': read_confirmation_summary(args.local, args.qpc_frequency),
                      'peer_confirmation_summary': read_confirmation_summary(args.peer, args.qpc_frequency)}, indent=2))
