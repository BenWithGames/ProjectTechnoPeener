"""Read-only SC6 research probe. Does not inject code or write process memory."""
import argparse
import csv
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
from pathlib import Path
import time
import struct

EXPECTED_SHA256 = 'F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553'
# Candidate offsets from SCUFFLE Config/address_map.ini at
# b8e8129b58417b8b71189f05ea3e789b09493f10 (overrides AddressMap.py defaults).
# These are observations, not a verified simulation hook or complete state.
OFFSETS = {'outer_counter': (0x470D0C4, 4), 'p1_timer': (0x475A038, 4),
           'timer': (0x47F1428, 4), 'p1_input': (0x4717840, 2),
           'p2_input': (0x47AEC30, 2), 'p1_move': (0x475A04A, 2),
           'p2_move': (0x47F143A, 2)}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--training-confirmed', action='store_true', required=True,
                        help='Confirm you are in offline training; mode detection is not implemented.')
    parser.add_argument('--seconds', type=float, default=5)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--animation', action='store_true', help='Read unverified animation-progress candidates.')
    parser.add_argument('--step-inputs', action='store_true', help='Read animation-step inputs through player pointers.')
    args = parser.parse_args()
    float_fields = set()
    if args.animation or args.step_inputs:
        for player, object_rva in [('p1', 0x4759BE0), ('p2', 0x47F0FD0)]:
            for label, offset in [('progress', 8), ('previous', 12), ('end', 16), ('rate', 48)]:
                field = player + '_' + label
                OFFSETS[field] = (object_rva + offset, 4)
                float_fields.add(field)
    dynamic = {}
    signed_fields = set()
    if args.step_inputs:
        for label, offset, size in [('gate',0x48462D0,1),('scale',0x48462D4,4),('scale_alt',0x48462F4,4),('selector',0x4846458,4),('side0_limit',0x48462D8,4),('side1_limit',0x48462DC,4),('side0_mode',0x484645C,4),('side1_mode',0x4846460,4),('extra_gate',0x4846418,4),('side0_countdown',0x48462E0,4),('side1_countdown',0x48462E4,4),('side0_source',0x48462E8,4),('side1_source',0x48462EC,4),('selected_source',0x4846300,4),('third_scale',0x4846304,4)]:
            OFFSETS[label]=(offset,size)
        float_fields.update(['scale','scale_alt','side0_limit','side1_limit','third_scale'])
        signed_fields.update(['selector','side0_countdown','side1_countdown'])
        for player, slot in [('p1',0x470F1A0),('p2',0x470F1A8)]:
            OFFSETS[player+'_pointer']=(slot,8)
            for label, offset, size in [('index',0x23C,1),('state',0x19EC,2),('base_step',0x3500,4),('step_override',0x3510,4),('extra_flag',0x16E6,1),('pending_duration',0x3514,4),('pending_source',0x3518,4)]:
                dynamic[player+'_'+label]=(player,offset,size)
            float_fields.update([player+'_base_step',player+'_step_override'])
            signed_fields.add(player+'_pending_duration')
    if not 0 < args.seconds <= 120:
        parser.error('--seconds must be between 0 and 120')
    k = C.WinDLL('kernel32', use_last_error=True)
    p = C.WinDLL('psapi', use_last_error=True)
    k.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
    k.OpenProcess.restype = W.HANDLE
    k.CloseHandle.argtypes = [W.HANDLE]
    k.ReadProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
    k.QueryFullProcessImageNameW.argtypes = [W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)]
    p.EnumProcessModulesEx.argtypes = [W.HANDLE, C.POINTER(W.HMODULE), W.DWORD, C.POINTER(W.DWORD), W.DWORD]
    process = k.OpenProcess(0x0400 | 0x0010, False, args.pid) # query + read only
    if not process:
        raise C.WinError(C.get_last_error())
    try:
        name = C.create_unicode_buffer(32768)
        length = W.DWORD(len(name))
        if not k.QueryFullProcessImageNameW(process, 0, name, C.byref(length)):
            raise C.WinError(C.get_last_error())
        exe = Path(name.value)
        if exe.name.lower() != 'soulcaliburvi.exe':
            raise RuntimeError('PID does not belong to SoulcaliburVI.exe')
        if hashlib.sha256(exe.read_bytes()).hexdigest().upper() != EXPECTED_SHA256:
            raise RuntimeError('Unsupported executable hash; revalidate offsets before reading.')
        modules = (W.HMODULE * 1024)()
        needed = W.DWORD()
        if not p.EnumProcessModulesEx(process, modules, C.sizeof(modules), C.byref(needed), 3):
            raise C.WinError(C.get_last_error())
        if not needed.value or not modules[0]:
            raise RuntimeError('No executable module found')
        base = modules[0]
        def read_at(field, address, size):
            buffer = C.create_string_buffer(size)
            count = C.c_size_t()
            if not k.ReadProcessMemory(process, address, buffer, size, C.byref(count)) or count.value != size:
                raise RuntimeError(f'Failed to read {field}: Windows error {C.get_last_error()}')
            if field in float_fields:
                return struct.unpack('<f', buffer.raw)[0]
            return int.from_bytes(buffer.raw, 'little', signed=field in signed_fields)
        def read(field):
            offset, size = OFFSETS[field]
            return read_at(field, base+offset, size)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        rows = []
        rejected = 0
        start = time.perf_counter()
        # Exclusive creation preserves previous captures.
        with args.output.open('x', newline='') as file:
            writer = csv.DictWriter(file, fieldnames=['elapsed_ms', *OFFSETS, *dynamic,
                'p1_buttons', 'p1_direction', 'p2_buttons', 'p2_direction'])
            writer.writeheader()
            while time.perf_counter() - start < args.seconds:
                row = {field: read(field) for field in OFFSETS}
                for field, (player, offset, size) in dynamic.items():
                    pointer = row[player+'_pointer']
                    if not 0x10000 <= pointer < 0x0000800000000000:
                        raise RuntimeError('Invalid player pointer')
                    row[field] = read_at(field, pointer+offset, size)
                pointers_changed = args.step_inputs and any(read(p+'_pointer') != row[p+'_pointer'] for p in ('p1','p2'))
                if pointers_changed or read('outer_counter') != row['outer_counter'] or read('timer') != row['timer']:
                    rejected += 1
                    continue
                row['elapsed_ms'] = round((time.perf_counter() - start) * 1000, 3)
                for player in ('p1', 'p2'):
                    row[player + '_buttons'] = row[player + '_input'] & 255
                    row[player + '_direction'] = row[player + '_input'] >> 8
                writer.writerow(row)
                rows.append(row)
                time.sleep(0.004)
        if not rows:
            raise RuntimeError('No coherent samples captured')
        summary = {'samples': len(rows), 'rejected_timer_changed': rejected,
            'duration_ms': rows[-1]['elapsed_ms'],
            'outer_counter_first': rows[0]['outer_counter'], 'outer_counter_last': rows[-1]['outer_counter'],
            'p1_timer_first': rows[0]['p1_timer'], 'p1_timer_last': rows[-1]['p1_timer'],
            'timer_first': rows[0]['timer'], 'timer_last': rows[-1]['timer'],
            'distinct_timer_values': len({r['timer'] for r in rows}),
            'p1_inputs': sorted({r['p1_input'] for r in rows}),
            'p2_inputs': sorted({r['p2_input'] for r in rows}),
            'p1_moves': sorted({r['p1_move'] for r in rows}),
            'p2_moves': sorted({r['p2_move'] for r in rows})}
        print(json.dumps(summary, indent=2))
    finally:
        k.CloseHandle(process)

if __name__ == '__main__':
    main()



