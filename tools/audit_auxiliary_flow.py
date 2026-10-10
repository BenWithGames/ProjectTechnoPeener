"""Read-only, hash-pinned disassembly of input producer and consumer paths."""
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
            parent = getattr(entries[start].unwindinfo, 'FunctionEntry', None)
            if parent is None:
                return start
            start = parent
        raise ValueError('Unwind cycle')
    dis = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    functions = {}
    for target in (0x3fe960, 0x3fcd10, 0x2dbc60, 0x37d95d, 0x321f40):
        fragment = next(s for s, e in entries.items() if s <= target < e.struct.EndAddress)
        owner = root(fragment)
        lines = []
        for start, entry in sorted(entries.items()):
            if root(start) != owner:
                continue
            size = entry.struct.EndAddress-start
            lines.extend(f'{i.address:08x}: {i.mnemonic} {i.op_str}'
                         for i in dis.disasm(pe.get_data(start, size), start))
        functions[hex(target)] = lines
    result = {'exe_sha256': PIN, 'game_modified': False, 'functions': functions,
              'limitation': 'Static paths do not establish complete indirect consumers or online replay correctness.'}
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(f'Saved {len(functions)} producer/consumer paths to {args.output}')


if __name__ == '__main__':
    main()
