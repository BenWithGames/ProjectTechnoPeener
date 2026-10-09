"""Read-only audit of the pinned battle-update entry and direct callers.

Requires pefile and capstone. This does not attach to or modify SC6.
"""
import argparse
import hashlib
import json
from pathlib import Path
import pefile
import capstone

PIN = 'f8904e4b04bca3b47bc52a683f6190365d2eb89ee8f44f8072759e9c5e04a553'
ENTRY = 0x2DBC60

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    data = args.exe.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != PIN:
        raise SystemExit('Unsupported executable hash; no analysis produced')
    pe = pefile.PE(data=data)
    dis = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    ranges = [(e.struct.BeginAddress, e.struct.EndAddress)
              for e in pe.DIRECTORY_ENTRY_EXCEPTION]
    def instructions(start, end):
        return list(dis.disasm(pe.get_data(start, end-start), start))
    def render(items):
        return [f'{i.address:08x}: {i.mnemonic} {i.op_str}' for i in items]
    start, end = next(r for r in ranges if r[0] == ENTRY)
    body = instructions(start, end)
    callers = []
    address_references = []
    absolute = (pe.OPTIONAL_HEADER.ImageBase + ENTRY).to_bytes(8, 'little')
    for section in pe.sections:
        raw = section.get_data()
        pos = raw.find(absolute)
        while pos >= 0:
            address_references.append({'kind': 'absolute_pointer_candidate', 'rva': hex(section.VirtualAddress+pos)})
            pos = raw.find(absolute, pos+1)
        if section.Characteristics & 0x20000000:
            # REX.W LEA reg,[RIP+disp32], commonly used for callback registration.
            for prefix in (b'\x48\x8d', b'\x4c\x8d'):
                pos = raw.find(prefix)
                while pos >= 0:
                    site = section.VirtualAddress+pos
                    if pos+7 <= len(raw) and raw[pos+2] & 0xc7 == 5 and site+7+int.from_bytes(raw[pos+3:pos+7], 'little', signed=True) == ENTRY:
                        bounds = next((r for r in ranges if r[0] <= site < r[1]), None)
                        if bounds:
                            decoded = instructions(*bounds)
                            index = next((n for n,i in enumerate(decoded) if i.address == site and i.mnemonic == 'lea'), None)
                            if index is not None:
                                address_references.append({'kind': 'rip_lea', 'rva': hex(site), 'context': render(decoded[max(0,index-8):index+16])})
                    pos = raw.find(prefix, pos+1)
    # Scan E8 candidates, then require instruction-boundary confirmation from
    # the containing unwind range. Raw byte matches alone are not call sites.
    for section in pe.sections:
        if not section.Characteristics & 0x20000000:
            continue
        raw = section.get_data()
        offset = raw.find(b'\xe8')
        while offset >= 0:
            site = section.VirtualAddress + offset
            if offset+5 <= len(raw) and site+5+int.from_bytes(raw[offset+1:offset+5], 'little', signed=True) == ENTRY:
                bounds = next((r for r in ranges if r[0] <= site < r[1]), None)
                if bounds:
                    decoded = instructions(*bounds)
                    index = next((n for n, i in enumerate(decoded) if i.address == site and i.mnemonic == 'call'), None)
                    if index is not None:
                        callers.append({'call_rva': hex(site), 'unwind_range': [hex(v) for v in bounds],
                                        'context': render(decoded[max(0,index-20):index+16])})
            offset = raw.find(b'\xe8', offset+1)
    result = {'schema': 1, 'exe_sha256': digest, 'entry_rva': hex(ENTRY),
              'unwind_end_rva': hex(end), 'direct_callers': callers, 'address_references': address_references,
              'entry_disassembly': render(body),
              'calls': render(i for i in body if i.mnemonic == 'call'),
              'limitations': ['Direct calls only; indirect callers are not enumerated.',
                             'Static analysis does not establish repeat-call safety or complete state coverage.'],
              'game_modified': False, 'catch_up_validated': False}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(f'Audited {len(body)} instructions and {len(callers)} direct callers; {args.output}')

if __name__ == '__main__':
    main()
