"""Read-only, hash-pinned disassembly of round-event and accessor paths."""
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
    for target in (0x3bdf40, 0x3c4679, 0x94a160, 0x964770,
                   0x3b5410, 0x3f0560, 0x43d210, 0x21bb8f0,
                   0x949370, 0x949450, 0x978720, 0x978800,
                   0x3ef099, 0x1ecdf60, 0x3b1808, 0x3c41c0, 0x3f41b0, 0x3bddd0, 0x3be200, 0x2d7390):
        fragment = next((s for s, e in entries.items() if s <= target < e.struct.EndAddress), None)
        if fragment is None:
            # Leaf helpers may have no unwind entry. Bound decoding, ending at
            # return or unconditional tail jump; do not infer adjacent functions.
            lines = []
            for i in dis.disasm(pe.get_data(target, 64), target):
                lines.append(f'{i.address:08x}: {i.mnemonic} {i.op_str}')
                if i.mnemonic in ('ret', 'jmp'):
                    break
            functions[hex(target)] = lines
            continue
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
              'findings': {'notification_owner': 'ALuxBattleLevelScriptActor',
                           'notification_requires': 'event code 1/6/7, nonnull context, resolved level script actor, class check, positive round accessor',
                           'delegate_registration': 'RVA 0x3B548A registers handler 0x3C4580 on manager delegate +0x490',
                           'excluded_current_round_owners': ['ALuxBattleReplayPlayer', 'ALuxBattleReplayRecorder']},
              'limitation': 'Static gates do not establish which failed live or whether an upstream event is a reliable rollback boundary.'}
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(f'Saved {len(functions)} round-event paths to {args.output}')


if __name__ == '__main__':
    main()


