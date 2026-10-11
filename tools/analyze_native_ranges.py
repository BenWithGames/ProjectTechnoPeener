"""Classify the observed pinned-build range pattern, without reading game memory."""
import argparse
import csv
import json
import hashlib
import struct
from pathlib import Path


def analyze(path, exe=None):
    with path.open(newline='', encoding='utf-8-sig') as source:
        rows = [{key: int(value) for key, value in row.items()}
                for row in csv.DictReader(source)]
    sentinel_reconstructed = False
    if rows and 'registered_size' in rows[0]:
        rows = [dict(index=row['index'], base=row['base'], size=row['registered_size']) for row in rows]
        # The same-snapshot inventory deliberately omits native sentinel zero.
        if rows[0]['index'] == 1:
            rows.insert(0, dict(index=0, base=0, size=0))
            sentinel_reconstructed = True
    if not rows or any(row['index'] != i for i, row in enumerate(rows)):
        raise ValueError('Missing or nonsequential range indices')
    if any(row['base'] < 0 or row['size'] < 0 or
           row['base'] + row['size'] > (1 << 64) - 1 for row in rows):
        raise ValueError('Invalid address range')
    # 391D33..391D63: EBX=0x3f; register [root+8], EBX&~15;
    # increment EBX by 8 until EBX>=0x43f. Signed rounding adds zero here.
    expected = [value & ~15 for value in range(0x3f, 0x43f, 8)]
    repeated = rows[4:132]
    matches = (len(repeated) == len(expected) and
               len({row['base'] for row in repeated}) == 1 and
               [row['size'] for row in repeated] == expected)
    result = {
        'schema': 1, 'input': str(path.resolve()), 'entries': len(rows),
        'sentinel_matches': rows[0]['base'] == rows[0]['size'] == 0,
        'sentinel_reconstructed_from_omitted_inventory_row': sentinel_reconstructed,
        'player_range_sizes_match': len(rows) >= 3 and
            rows[1]['size'] == rows[2]['size'] == 0x973f0,
        'stage_repeated_range_pattern_matches': matches,
        'stage_repeated_entries': len(repeated),
        'stage_descriptor_range': rows[3] if len(rows) > 3 else None,
        'descriptor_size_verified': False,
        'required_descriptor_evidence': 'Signed counts at +0x28/+0x2a/+0x2c and per-record unsigned counts, captured with the range list',
        'ownership_verified': False,
        'interpretation': 'Relocation search ranges, not independent allocations or payload-copy instructions',
    }
    if exe is not None:
        import pefile
        from audit_update_entry import PIN
        data = exe.read_bytes()
        if hashlib.sha256(data).hexdigest() != PIN:
            raise ValueError('Unsupported executable')
        pe = pefile.PE(data=data)
        sites = [0x301937, 0x30194c, 0x301961, 0x301976, 0x30198b,
                 0x3019a0, 0x3019b5, 0x3019ca, 0x3019df, 0x3019f4, 0x301a09]
        sizes = [24,40,40,56,40,40,40,40,56,56,24]
        offsets = []
        for site, size in zip(sites, sizes):
            instruction = pe.get_data(site, 7)
            if instruction[:3] != bytes.fromhex('488d15') or pe.get_data(site-6,6) != b'\x41\xb8'+struct.pack('<I',size):
                raise ValueError(f'Global registration mismatch at {site:#x}')
            call = pe.get_data(site+10,5)
            if call[0] != 0xe8 or site+15+struct.unpack_from('<i',call,1)[0] != 0x384140:
                raise ValueError('Unexpected registration target')
            offsets.append(site+7+struct.unpack_from('<i',instruction,3)[0])
        tail = rows[132:143]
        bases = {row['base']-offset for row, offset in zip(tail, offsets)}
        match = (len(rows)==143 and len(bases)==1 and
                 [row['size'] for row in tail]==sizes and next(iter(bases))>0 and next(iter(bases))%65536==0)
        result['fixed_global_registration_matches'] = match
        result['fixed_global_rvas'] = offsets
        result['inferred_module_base'] = next(iter(bases)) if match else None
        result['module_base_source'] = 'Agreement of eleven pinned registrations; not a live module query'
        result['distinct_nonzero_bases'] = len({row['base'] for row in rows[1:]})
        result['remaining_stage_pointer_source'] = 'Selected query root +8 (RVA 0x4844DD0 or 0x4845E80); allocation ownership not established'
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--exe', type=Path)
    args = parser.parse_args()
    result = analyze(args.csv, args.exe)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
