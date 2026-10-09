"""Compare adjacent throw-audit snapshots without touching the game process."""
import argparse
import csv
import json
from pathlib import Path


def analyze(stem):
    def read(suffix):
        return Path(str(stem) + suffix).read_bytes()

    def differences(a, b):
        if len(a) != len(b):
            raise ValueError('Snapshot lengths differ')
        return [i for i, (x, y) in enumerate(zip(a, b)) if x != y]

    report = dict(line.split('=', 1) for line in read('-restore.txt').decode().splitlines() if '=' in line)
    for key, value in {'fault': '0', 'audit_done': '1', 'restore_invoked': '0',
                       'ring_save_unchanged': '1', 'ring_rotation_ok': '1'}.items():
        if report.get(key) != value:
            raise ValueError(f'Not a valid save-only adjacent audit: {key}')
    result = {'capture': stem.name, 'older_frame_restore_tested': False, 'blend': [], 'changed_transforms': []}
    a, b = read('-blend-original_a.bin'), read('-blend-original_b.bin')
    if len(a) != 2760 or len(b) != 2760:
        raise ValueError('Unexpected blend schema')
    for side in range(2):
        for name, start, size in [('weights', 0, 140), ('blocks', 140, 1232), ('history_pointer', 1372, 8)]:
            off = side * 1380 + start
            result['blend'].append({'side': side, 'field': name,
                                    'changed_offsets': differences(a[off:off+size], b[off:off+size])})
    tables = []
    for phase in ('original_a', 'original_b'):
        with Path(str(stem) + f'-transforms-{phase}.csv').open(newline='') as f:
            tables.append(list(csv.DictReader(f)))
    if tables[0] != tables[1]:
        raise ValueError('Transform topology changed')
    a, b = read('-transforms-original_a.bin'), read('-transforms-original_b.bin')
    differences(a, b)
    for row in tables[0]:
        off, size = int(row['offset']), int(row['length'])
        if off + size > len(a):
            raise ValueError('Transform bounds invalid')
        changed = differences(a[off:off+size], b[off:off+size])
        if changed:
            result['changed_transforms'].append({**row, 'changed_offsets': changed})
    result['camera_extended'] = {}
    for field in ('smoothing', 'targets', 'response'):
        result['camera_extended'][field] = differences(read(f'-camera-0-{field}-1.bin'), read(f'-camera-1-{field}-1.bin'))
    result['measured_input_envelope_stable'] = all(report.get(f'input_phase_{i}_valid') == '1' and
                                                  report.get(f'input_phase_{i}_changed_mask') == '0' for i in (0, 1))
    result['limitations'] = 'Dynamic fields require combat-specific restoration; this report does not certify replay safety or complete input/state coverage.'
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stem', type=Path)
    args = parser.parse_args()
    result = analyze(args.stem)
    output = Path(str(args.stem) + '-replay-readiness.json')
    output.write_text(json.dumps(result, indent=2) + '\n')
    print(f'{output}: {len(result["changed_transforms"])} changed transform records; '
          f'input envelope stable={result["measured_input_envelope_stable"]}')
