"""Disassemble update-loop helpers including all chained unwind fragments."""
import argparse
import hashlib
import json
from pathlib import Path
import capstone
import pefile
from audit_update_entry import PIN

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    data = args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != PIN:
        raise SystemExit('Unsupported executable')
    pe = pefile.PE(data=data)
    entries = {e.struct.BeginAddress: e for e in pe.DIRECTORY_ENTRY_EXCEPTION}
    def root(start):
        seen = set()
        while start not in seen:
            seen.add(start)
            entry = entries[start]
            parent = getattr(entry.unwindinfo, 'FunctionEntry', None)
            if parent is None:
                return start
            if parent not in entries:
                raise ValueError(f'Missing chained parent {parent:x}')
            start = parent
        raise ValueError('Unwind cycle')
    groups = {}
    for start, entry in entries.items():
        groups.setdefault(root(start), []).append(entry)
    dis = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    def render(i):
        return f'{i.address:08x}: {i.mnemonic} {i.op_str}'
    targets = [0x3FE520, 0x3FCD10, 0x3FCE80, 0x3FCA60, 0x3999C0]
    functions = []
    for target in targets:
        fragments = sorted(groups[root(target)], key=lambda e: e.struct.BeginAddress)
        decoded = []
        for fragment in fragments:
            s, e = fragment.struct.BeginAddress, fragment.struct.EndAddress
            decoded.extend(dis.disasm(pe.get_data(s,e-s), s))
        functions.append({'entry': hex(target), 'fragments': [[hex(e.struct.BeginAddress),hex(e.struct.EndAddress)] for e in fragments],
                          'instructions': [render(i) for i in decoded],
                          'calls': [render(i) for i in decoded if i.mnemonic == 'call']})
    # Direct relative tail jumps can explain why the original call scanner
    # found no E8 calls to the battle entry.
    tails = []
    for section in pe.sections:
        if not section.Characteristics & 0x20000000:
            continue
        raw = section.get_data()
        pos = raw.find(b'\xe9')
        while pos >= 0:
            site = section.VirtualAddress+pos
            if pos+5 <= len(raw) and site+5+int.from_bytes(raw[pos+1:pos+5],'little',signed=True) == 0x2DBC60:
                fragment = next((e for e in entries.values() if e.struct.BeginAddress <= site < e.struct.EndAddress), None)
                # Leaf tail adapters may have no unwind metadata. Retain that
                # distinction rather than claiming confirmed instruction bounds.
                start = fragment.struct.BeginAddress if fragment else max(section.VirtualAddress,site-16)
                decoded = list(dis.disasm(pe.get_data(start,site+5-start),start))
                tails.append({'site': hex(site), 'unwind_confirmed': fragment is not None,
                              'context': [render(i) for i in decoded]})
            pos = raw.find(b'\xe9',pos+1)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    result = {'schema':1,'exe_sha256':PIN,'functions':functions,'tail_candidates':tails,'game_modified':False}
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    for f in functions:
        print(f["entry"],len(f['fragments']),'fragments',len(f['instructions']),'instructions')
        print('\n'.join(f['calls']))
    print('TAIL CANDIDATES',json.dumps(tails,indent=2))

if __name__ == '__main__':
    main()
