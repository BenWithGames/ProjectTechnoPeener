"""Read-only discovery of SC6's native input clock. No hooks or memory writes.

Requires the pinned executable. Results are asynchronous observations, not a
coherent game snapshot, proof of private mode, or permission to alter a clock.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
from pathlib import Path
import struct
import time

EXPECTED_SHA256 = 'F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553'
TABLES = {0x3282F18: 'base', 0x3283578: 'derived'}
FIELDS = {'delay_390': 0x390, 'slots': 0x398, 'slot_mask': 0x39C,
          'identity': 0x3A0, 'simulation_cursor': 0x3A4,
          'wait_count': 0x3A8, 'poll_count': 0x3AC,
          'consumer_cursor': 0x3B0, 'consumer_end': 0x3B4,
          'held0': 0x3B8, 'held1': 0x3BC}
DERIVED = {'mask_43fc': 0x43FC, 'mask_4400': 0x4400,
           'lead_440c': 0x440C, 'local_cursor': 0x4410,
           'all_cursor': 0x4414, 'previous_all': 0x4418,
           'unchanged_count': 0x441C}


def decode(data, base):
    """Validate structural bounds, retaining raw field names where unproven."""
    if len(data) < 0x43E0:
        return None
    table = struct.unpack_from('<Q', data)[0] - base
    if table not in TABLES:
        return None
    row = {'vtable_rva': hex(table), 'implementation': TABLES[table]}
    row.update({name: struct.unpack_from('<i', data, offset)[0]
                for name, offset in FIELDS.items()})
    if not 1 <= row['slots'] <= 2 or not 0 <= row['slot_mask'] < (1 << row['slots']):
        return None
    if row['simulation_cursor'] < 0 or row['identity'] < -1:
        return None
    if table == 0x3283578:
        if len(data) < 0x4426:
            return None
        row.update({name: struct.unpack_from('<i', data, offset)[0]
                    for name, offset in DERIVED.items()})
        row.update({name: data[offset] for name, offset in
                    [('mode_4404', 0x4404), ('state_4424', 0x4424), ('wait_reason_4425', 0x4425)]})
    entries = []
    # Nearby tagged entries let us corroborate the layout without dumping the
    # whole history or treating an absent entry as a predicted neutral input.
    for slot in range(row['slots']):
        for frame in range(max(0, row['simulation_cursor'] - 2), row['simulation_cursor'] + 1):
            offset = 0x3C0 + slot * 0x2000 + (frame & 0x1FF) * 16
            identity, stored_frame, held, marked = struct.unpack_from('<iiIB', data, offset)
            entries.append({'slot': slot, 'requested': frame, 'identity': identity,
                            'frame': stored_frame, 'held': held, 'marked': marked,
                            'tag_match': identity == row['identity'] and stored_frame == frame})
    row['history'] = entries
    return row


def self_test():
    base = 0x140000000
    data = bytearray(0x4428)
    struct.pack_into('<Q', data, 0, base + 0x3283578)
    struct.pack_into('<iiii', data, 0x398, 2, 1, 7, 513)
    for slot in range(2):
        offset = 0x3C0 + slot * 0x2000 + (512 & 511) * 16
        struct.pack_into('<iiIB', data, offset, 7, 512, 1024 + slot, 1)
    row = decode(data, base)
    assert row and row['history'][1]['tag_match'] and row['history'][4]['held'] == 1025
    assert not row['history'][2]['tag_match']
    assert decode(data[:100], base) is None
    struct.pack_into('<i', data, 0x398, 3)
    assert decode(data, base) is None
    print('native clock decoder: PASS')


def inspect(args):
    if C.sizeof(C.c_void_p) != 8:
        raise RuntimeError('64-bit Python is required')
    k = C.WinDLL('kernel32', use_last_error=True)
    p = C.WinDLL('psapi', use_last_error=True)
    class Region(C.Structure):
        _fields_ = [('base', C.c_void_p), ('allocation', C.c_void_p),
                    ('allocation_protect', W.DWORD), ('partition', W.WORD),
                    ('size', C.c_size_t), ('state', W.DWORD),
                    ('protect', W.DWORD), ('kind', W.DWORD)]
    assert C.sizeof(Region) == 48
    k.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
    k.OpenProcess.restype = W.HANDLE
    k.CloseHandle.argtypes = [W.HANDLE]
    k.ReadProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
    k.VirtualQueryEx.argtypes = [W.HANDLE, C.c_void_p, C.POINTER(Region), C.c_size_t]
    k.VirtualQueryEx.restype = C.c_size_t
    k.QueryFullProcessImageNameW.argtypes = [W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)]
    p.EnumProcessModulesEx.argtypes = [W.HANDLE, C.POINTER(W.HMODULE), W.DWORD, C.POINTER(W.DWORD), W.DWORD]
    handle = k.OpenProcess(0x410, False, args.pid)  # QUERY_INFORMATION | VM_READ only
    if not handle:
        raise C.WinError(C.get_last_error())
    try:
        name = C.create_unicode_buffer(32768)
        length = W.DWORD(len(name))
        if not k.QueryFullProcessImageNameW(handle, 0, name, C.byref(length)):
            raise C.WinError(C.get_last_error())
        exe = Path(name.value)
        if exe.name.lower() != 'soulcaliburvi.exe' or hashlib.sha256(exe.read_bytes()).hexdigest().upper() != EXPECTED_SHA256:
            raise RuntimeError('Unsupported process/executable')
        modules = (W.HMODULE * 1024)()
        needed = W.DWORD()
        if not p.EnumProcessModulesEx(handle, modules, C.sizeof(modules), C.byref(needed), 3) or not needed.value:
            raise RuntimeError('Cannot enumerate executable module')
        base = modules[0]
        def read(address, size):
            buffer = C.create_string_buffer(size)
            count = C.c_size_t()
            if not k.ReadProcessMemory(handle, address, buffer, size, C.byref(count)) or count.value != size:
                return None
            return buffer.raw
        needles = [struct.pack('<Q', base + table) for table in TABLES]
        candidates = set()
        address = scanned = failures = table_matches = 0
        regions = []
        limited = False
        if args.address is not None:
            block = read(args.address, 0x4428)
            if block is None or decode(block, base) is None:
                raise RuntimeError('Requested object no longer matches the pinned layout')
            candidates.add(args.address)
            address = 0x7FFFFFFF0000
        while address < 0x7FFFFFFF0000:
            region = Region()
            if not k.VirtualQueryEx(handle, address, C.byref(region), C.sizeof(region)):
                break
            start, end = region.base or 0, (region.base or 0) + region.size
            if end <= address:
                break
            address = end
            if region.state != 0x1000 or region.kind != 0x20000 or region.protect & 0x100 or (region.protect & 0xFF) not in (4, 8, 0x40, 0x80):
                continue
            regions.append((start, end))
        # UObject allocations usually live in smaller committed regions. Visit
        # those first so large asset buffers do not exhaust the scan budget.
        # This is a search priority only, not an object-identity assumption.
        regions.sort(key=lambda pair: (pair[1] - pair[0] > 16 * 1024 * 1024, pair[0]))
        for start, end in regions:
            for chunk in range(start, end, 1024 * 1024):
                size = min(1024 * 1024, end - chunk)
                if scanned + size > args.max_mib * 1024 * 1024:
                    limited = True
                    break
                scanned += size
                data = read(chunk, size)
                if data is None:
                    failures += 1
                    continue
                for needle in needles:
                    pos = data.find(needle)
                    while pos >= 0:
                        candidate = chunk + pos
                        if candidate % 8 == 0:
                            table_matches += 1
                            block = read(candidate, 0x4428)
                            if block is not None and decode(block, base):
                                candidates.add(candidate)
                        pos = data.find(needle, pos + 8)
            if limited:
                break
        samples = []
        for sample in range(args.samples):
            for candidate in sorted(candidates):
                before = read(base + 0x470D0C4, 4)
                block = read(candidate, 0x4428)
                after = read(base + 0x470D0C4, 4)
                row = decode(block, base) if block is not None else None
                samples.append({'address': hex(candidate), 'time_ns': time.perf_counter_ns(),
                                'outer_before': int.from_bytes(before, 'little') if before else None,
                                'outer_after': int.from_bytes(after, 'little') if after else None,
                                'state': row})
            if sample + 1 < args.samples:
                time.sleep(args.interval_ms / 1000)
        report = {'schema': 1, 'pid': args.pid, 'module': hex(base), 'read_only': True,
                  'coherent_snapshot': False, 'scanned_bytes': scanned, 'scan_limited': limited,
                  'failed_reads': failures, 'table_matches': table_matches,
                  'candidate_count': len(candidates), 'samples': samples}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(f'Candidates={len(candidates)} limited={limited}; report={args.output}')
    finally:
        k.CloseHandle(handle)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--max-mib', type=int, default=4096)
    parser.add_argument('--address', type=lambda value: int(value, 0),
                        help='Reinspect a discovered object in the same process, with layout revalidation')
    parser.add_argument('--samples', type=int, default=3)
    parser.add_argument('--interval-ms', type=int, default=50)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        if (not args.pid or args.output is None or not 1 <= args.max_mib <= 16384
                or not 1 <= args.samples <= 600 or not 10 <= args.interval_ms <= 1000):
            parser.error('--pid, --output, and a scan budget of 1..16384 MiB are required')
        inspect(args)
