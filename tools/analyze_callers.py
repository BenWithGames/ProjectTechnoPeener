"""Summarize bounded caller traces; counts do not establish a battle-step hook."""
import collections
import json
import re
import sys

def summarize(text):
    base_match = re.search(r'module_base=0x([0-9a-f]+)', text)
    base = int(base_match[1], 16) if base_match else 0
    counts = collections.Counter()
    arguments = collections.Counter()
    windows = collections.defaultdict(collections.Counter)
    pending = None
    for line in text.splitlines():
        if line.startswith('hit='):
            fields = dict(re.findall(r'(\w+)=([^ ]+)', line))
            pending = fields
            slot = int(fields.get('slots', '0'))
            for flag, name in ((1, 'timer_routine_entries'), (2, 'caller_entries'), (4, 'timer_writes')):
                if slot & flag:
                    counts[name] += 1
                    windows[int(fields['ms']) // 5000][name] += 1
        elif line.startswith('regs ') and pending:
            fields = dict(re.findall(r'(\w+)=([^ ]+)', line))
            if int(pending.get('slots', '0')) & 1:
                arg = int(fields['rdx'], 16)
                arguments[hex(arg - base) if base <= arg < base + 0x4ba2000 else hex(arg)] += 1
            pending = None
    finish = next((line for line in reversed(text.splitlines()) if 'restored=' in line), None)
    cleanup_ok = bool(finish and all(x in finish.split() for x in ('restored=1', 'detached=1', 'failed=0')))
    return {'complete_cleanup_reported': cleanup_ok, 'observed_counts': dict(counts),
            'timer_routine_second_arguments_game_rva_or_absolute': dict(arguments),
            'five_second_windows': {str(k*5): dict(v) for k,v in sorted(windows.items())},
            'completion_line': finish,
            'limitation': 'Debugger pauses perturb timing. Entry counts and write correlations do not prove a full battle-step boundary or capture returns.'}

if __name__ == '__main__':
    with open(sys.argv[1]) as file:
        print(json.dumps(summarize(file.read()), indent=2))
