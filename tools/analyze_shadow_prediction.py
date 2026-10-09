"""Offline, observational prediction estimate. Never connects to or writes to SC6."""
import argparse
import bisect
import csv
import json
import collections
import statistics
from analyze_steam_inputs import read


def analyze(local, remote, timing, remote_slot, policy='repeat_pair', delay_frames=0):
    if policy not in ('repeat_pair', 'hold_clear_edges') or delay_frames < 0:
        raise ValueError('Unsupported prediction policy or delay')
    if len(local) != len(remote) or len(timing) != len(local):
        raise ValueError('Mismatched capture lengths')
    ticks = lambda r: (r['word12'] << 32) | r['word11']
    local_ticks = [ticks(r) for r in local]
    if any(b <= a for a, b in zip(local_ticks, local_ticks[1:])):
        raise ValueError('Local timestamps must increase')
    frame_ticks = statistics.median(b-a for a,b in zip(local_ticks, local_ticks[1:]))
    scheduled_ticks = [t + delay_frames*frame_ticks for t in local_ticks]
    for rows in (local, remote):
        if any(b['word0'] != a['word0'] + 1 for a, b in zip(rows, rows[1:])):
            raise ValueError('Counter discontinuity: frame-depth estimate unsupported')
    word = 1 if remote_slot == 1 else 3
    inputs = lambda r: (r[f'word{word}'], r[f'word{word+1}'])
    remote_by_counter = {r['word0']: r for r in remote}
    events = []
    arrival_by_counter = {}
    for i, t in enumerate(timing):
        if t['sample'] != i or t['remote_outer'] != remote[i]['word0'] or t['local_outer'] != local[i]['word0'] or not t['arrival_qpc']:
            raise ValueError('Timing records do not match captures')
        counter = t['remote_outer']
        events.append((t['arrival_qpc'], counter, inputs(remote[i])))
        arrival_by_counter[counter] = t['arrival_qpc']
    events.sort()
    available = {}
    event_index = 0
    evaluated = predicted = wrong = unseeded = truth_mismatch = 0
    depths = []
    censored = 0
    for i, frame in enumerate(local):
        now, counter = scheduled_ticks[i], frame['word0']
        while event_index < len(events) and events[event_index][0] <= now:
            _, received_counter, value = events[event_index]
            available[received_counter] = value
            event_index += 1
        truth = remote_by_counter.get(counter)
        if truth is None:
            continue
        if inputs(frame) != inputs(truth):
            truth_mismatch += 1
            continue
        evaluated += 1
        if counter in available:
            continue
        prior = [c for c in available if c < counter]
        if not prior:
            unseeded += 1
            continue
        predicted += 1
        guess = available[max(prior)]
        if policy == 'hold_clear_edges':
            guess = (guess[0], 0)
        if guess != inputs(truth):
            wrong += 1
            censored += arrival_by_counter[counter] > scheduled_ticks[-1]
            depths.append(max(0, bisect.bisect_right(scheduled_ticks, arrival_by_counter[counter]) - i))
    return dict(scope='offline_shadow_prediction_estimate', policy=policy, hypothetical_delay_frames=delay_frames,
                remote_slot_assumed=remote_slot, frame_counter_alignment_assumed=True,
                evaluated_frames=evaluated, matching_input_unavailable_frames=predicted,
                skipped_unseeded_frames=unseeded, ground_truth_mismatches=truth_mismatch,
                predicted_pair_mismatches=wrong, max_estimated_replay_frames=max(depths, default=0),
                estimated_corrections_over_three_frames=sum(d > 3 for d in depths),
                estimated_depth_histogram=dict(sorted(collections.Counter(depths).items())),
                censored_corrections=censored,
                replay_depth_censored_at_capture_end=True, actual_replays=0,
                rollback_enabled=False, limitation='Normal SC6 networking already controls simulation pacing; this is not a replacement-netcode performance measurement.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('local')
    parser.add_argument('remote')
    parser.add_argument('timing')
    parser.add_argument('--remote-slot', type=int, choices=(1, 2), required=True)
    parser.add_argument('--policy', choices=('repeat_pair', 'hold_clear_edges'), default='repeat_pair')
    parser.add_argument('--delay-frames', type=int, default=0)
    args = parser.parse_args()
    with open(args.timing, newline='') as stream:
        timing = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(stream)]
    print(json.dumps(analyze(read(args.local), read(args.remote), timing, args.remote_slot, args.policy, args.delay_frames), indent=2))
