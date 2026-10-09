"""Classify exported immediate-restore differences; never changes game state.
Usage: python Analyze-NativeAdjacent.py <capture stem>
"""
import argparse
import collections
import csv
import json
import struct
from pathlib import Path


def analyze(stem):
    def data(suffix):
        return Path(str(stem) + suffix).read_bytes()

    def rows(suffix):
        with Path(str(stem) + suffix).open(newline='') as stream:
            return list(csv.DictReader(stream))

    result = {'capture': str(stem), 'scope': 'immediate post-load; no replay update',
              'classification_is_not_restore_approval': True}
    sample = rows('.csv')[0]
    players = [int(sample[f'p{s}_pointer']) for s in (1, 2)]
    native_a, native_c = data('-before.bin'), data('-restored.bin')
    if len(native_a) != len(native_c):
        raise ValueError('Native payload lengths differ')
    native = []
    for row in rows('-transfers.csv'):
        if row['phase'] != 'before':
            continue
        off, size = int(row['offset']), int(row['length'])
        a, c = native_a[off:off+size], native_c[off:off+size]
        if len(a) != size or len(c) != size:
            raise ValueError('Truncated native record')
        if a == c:
            continue
        source = int(row['source'])
        rva = int(row['writer_return']) - int(row['module_base'])
        rebuilt = struct.unpack_from('<Q', c)[0] if size >= 8 else None
        known = (size == 48 and rva == 0x30AA54 and
                 any(source == player + 0x43DF0 and rebuilt == player for player in players) and a[8:] == c[8:])
        native.append({'index': int(row['index']), 'writer_rva': hex(rva),
                       'changed_bytes': sum(x != y for x, y in zip(a, c)),
                       'validated_rebuilt_player_pointer': known})
    result['native'] = native
    for category, labels in [('transforms', ('original_a', 'original_b', 'restored_after_save')),
                             ('camera', ('0', '1', '4'))]:
        tables = [rows(f'-{category}-{label}.csv') for label in labels]
        if not all(table == tables[0] for table in tables):
            raise ValueError(f'{category} topology differs')
        buffers = [data(f'-{category}-{label}.bin') for label in labels]
        if len(set(map(len, buffers))) != 1:
            raise ValueError(f'{category} payload lengths differ')
        differences = []
        for row in tables[0]:
            off, size = int(row['offset']), int(row['length'])
            a, b, c = [buffer[off:off+size] for buffer in buffers]
            if any(len(part) != size for part in (a, b, c)):
                raise ValueError('Truncated visual record')
            changed = [i for i in range(size) if a[i] != c[i]]
            if changed:
                differences.append(dict(row, changed_offsets=changed,
                    restored_equals_current=(b == c),
                    source_equals_current=(a == b)))
        result[category] = {'differing_records': len(differences),
            'by_kind': dict(collections.Counter(row.get('kind', 'camera') for row in differences)),
            'retained_current_records': sum(row['restored_equals_current'] for row in differences),
            'records': differences}
    a, b, c = [data(f'-blend-{label}.bin') for label in ('original_a', 'original_b', 'restored_after_save')]
    if any(len(buffer) != 2760 for buffer in (a, b, c)):
        raise ValueError('Unexpected blend layout')
    result['blend'] = {'changed_offsets': [i for i in range(len(a)) if a[i] != c[i]],
        'history_pointers': [{label: hex(struct.unpack_from('<Q', buffer, side*1380+1372)[0])
                             for label, buffer in [('source', a), ('current', b), ('restored', c)]}
                            for side in range(2)]}
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stem', type=Path)
    args = parser.parse_args()
    result = analyze(args.stem)
    output = Path(str(args.stem) + '-immediate-analysis.json')
    output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: ({k: v for k, v in value.items() if k != 'records'}
                           if isinstance(value, dict) else value)
                      for key, value in result.items()}, indent=2))
    print(output)
