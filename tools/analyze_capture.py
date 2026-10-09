"""Summarize timer plateaus and resets in a read-only probe capture."""
import csv
import json
import sys

def summarize(rows):
    if not rows:
        raise ValueError('Empty capture')
    groups = []
    resets = []
    first = last = float(rows[0]['elapsed_ms'])
    previous = int(rows[0]['timer'])
    missed = 0
    for row in rows[1:]:
        now, timer = float(row['elapsed_ms']), int(row['timer'])
        if timer != previous:
            if last - first >= 200:
                groups.append({'start_ms': first, 'end_ms': last, 'timer': previous})
            if timer < previous:
                resets.append({'time_ms': now, 'before': previous, 'after': timer})
            else:
                missed += max(0, timer - previous - 1)
            first, previous = now, timer
        last = now
    if last - first >= 200:
        groups.append({'start_ms': first, 'end_ms': last, 'timer': previous})
    return {'samples': len(rows), 'plateaus_over_200ms': groups,
            'timer_decreases': resets, 'unobserved_counts_between_samples': missed}

if __name__ == '__main__':
    with open(sys.argv[1], newline='') as file:
        print(json.dumps(summarize(list(csv.DictReader(file))), indent=2))
