# One-shot offline save probe

The research DLL supports a separately armed save attempt. It is not rollback,
not a complete-state claim, and has no battle-state load/replay operation.

## Implementation and validation

`SaveStream.hpp` uses the inspected Win64 stream layout and a separately owned
160 KiB payload. Every transfer is counted; the first failure remains recorded,
and subsequent writes fail closed. Reads, resets, seeks, and deletion are rejected.
The in-process copier catches memory access faults. Partial/failed saves are not
written as binary payloads. A serializer fault invalidates the capture; SEH does
not guarantee recovery from arbitrary internal game or allocator corruption.

`SaveProbe.hpp` checks player pointers, buffer getter methods, bounded linked
lists, manager serializers, and auxiliary serializers against the inspected
methods before calling writer RVA `3841E0` once. This whitelist covers the
observed Training setup, not all characters or modes. The known byte-count
bookkeeping global `470E870` is preserved around the call. Game allocator use
and temporary range-table growth still occur. No restore routine is called.

The test runs on the first valid recorder entry before RVA `2DBC60`, then the
recorder collects 600 entries or stops after 20 seconds. Data output happens
outside the entry callback. Automatic match-mode detection is still absent;
the user must remain in offline Training. A hung serializer cannot be stopped
by the recorder's 20-second deadline.

Local MSVC /W4 /WX tests passed: stream layout offsets, exact-capacity write,
overflow including integer wraparound input, sticky failure, failed copy,
forbidden operations, faulting preflight, and one-attempt behavior. Existing
recorder bridge tests passed. The native DLL built successfully. Live save
invocation has not yet been validated.

## Run after installing the staged DLL with SC6 closed

Start SC6 through Steam and enter offline Training. Then run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Arm-Recorder.ps1 -TrainingConfirmed -SaveOnly
```

Files go into the installed mod's `captures` directory, with a `save-PID-TICKS`
stem: frame CSV, frame metadata TXT, `-save.txt`, and a `.bin` only if the save
returned without reported faults or transfer failures and produced bytes.
Check `preflight_ok=1`, `invoked=1`, `fault=0`, `transfer_failures=0`, and
`payload_written=1`. Inspect frame continuity and ask whether the game remained
responsive. None of these checks alone establish complete or deterministic state.

## Read-only inventory evidence

`tools/inspect_serializer_objects.py` requires the verified executable hash and
explicit offline Training confirmation. It reads memory only and bounds list
walks. Its results are not atomic snapshots.

Captures `serializer-objects-5952-01.json` through `-03.json` had no read errors
and stable player pointers. Both save pointers matched the recorder pointers.
Observed linked writers: `30EA70` and `30EB50`; manager writers: `318860`,
`33FBC0`, `31C190`, `340ED0`, `317BD0`, `341110`; auxiliary writers: no-op
`2D2BB0` and `32AC00`. Player buffer getters were `30B7B0` and `30B680`.
These methods were inspected in the verified executable. Nested routine
`391CA0` extends the pointer range table via `384140`, as does `301820`.
Static disassembly evidence is saved under workspace `work/serialization-*.txt`.

## First live save-only result

Capture `save-54696-17659062` completed in user-confirmed offline Training.
The user reported that it seemed to work. Preflight passed and the writer returned:
1,310 transfers, 106,766 bytes, zero transfer failures, zero reported faults.
The measured preflight-plus-save interval was 315 ticks at 10 MHz (31.5 microseconds);
this is one measurement, not a performance guarantee. The byte-count bookkeeping
field was 4,326 both before and after the writer. The binary file length matches
the reported byte count. The following recorder capture reached 600 samples,
with no skipped entries, reported faults, or outer-counter discontinuities.

This validates one save-only invocation for the observed Training setup. No battle
state was restored, and completeness/deterministic replay remain unverified.
Next: repeat the save-only test during combat and special mechanics, then audit
and test the load path before attempting save/restore/replay.

### A+G save during throw freeze

Capture `save-54696-18134562`: first boundary row has move IDs 487/10041,
both animation progress values 2, multiplier 0, selector -1. This matches the
previously observed A+G throw freeze and places the one-shot save in that state.
Preflight passed; 1,310 transfers produced 106,766 bytes with zero reported faults
or transfer failures. Duration: 378 ticks at 10 MHz (37.8 microseconds).
The recorder completed 600 rows with zero skipped entries or outer discontinuities.
User confirmation of responsiveness for this particular run is pending.
User confirmed the A+G throws landed and the game stayed responsive.
The first Soul Charge activation watcher timed out without arming a save;
no Soul Charge save result is claimed.

### Soul Charge activation save

Capture `save-54696-18326562`: first row move 219/65535, multiplier 0,
selector 0, progress 15/360. Preflight passed, 1,310 transfers, 106,766 bytes,
zero faults/failures. Duration 322 ticks at 10 MHz (32.2 microseconds).
600 recorder samples, zero skipped entries or outer discontinuities.
The preceding retry detected activation but PowerShell could not resolve
Get-FileHash; it created no trigger. The watcher now verifies executable and DLL
hashes itself and creates the PID-bound arm file exclusively. No process memory
is written by the watcher. User confirmation of this run is pending.
Soul Charge user confirmation: finished and responsive.

### Soul Attack activation save

Capture `save-54696-18383562`: first row move 679/65535, selector 0,
multiplier 0, progress 3/242. Preflight passed; 1,356 transfers, 107,094 bytes,
zero reported faults/failures; 420 ticks at 10 MHz (42 microseconds).
600 subsequent frame entries with zero skips/discontinuities. This is an
activation-state save, not demonstrated impact-state coverage. User hit and
responsiveness confirmation pending.
Soul Attack user confirmation: landed and responsive.

### Critical Edge activation save

Capture `save-54696-18436812`: first row move 656/65535, selector 0,
multiplier 0, progress 13/66. 1,351 transfers, 107,078 bytes; preflight passed,
zero faults/failures. 331 ticks at 10 MHz (33.1 microseconds). 600 recorder
entries, zero skips/discontinuities. This save captures startup, not a verified
later cinematic state. User cinematic/responsiveness confirmation pending.
Critical Edge user confirmation: landed, cinematic finished, responsive.

### Reversal Edge slowdown save

Capture `save-54696-18494562`: first row moves 187/193, selector -1,
multiplier 0.049987793, both animation progress values 4.09997559.
Preflight passed; 1,356 transfers, 107,094 bytes, zero reported faults/failures;
359 ticks at 10 MHz (35.9 microseconds). 600 rows, zero skips/discontinuities.
User clash/responsiveness confirmation pending.

All five combat/special save binaries were checked against reported byte counts;
all five frame CSVs contain 600 rows with consecutive outer counters. Metrics
and SHA-256 hashes are in captures/combat-save-validation.json. Coverage is one
save point per mechanic in this Training setup, not every phase, character, or
match mode. No battle-state restoration or deterministic replay was attempted.

Reversal Edge user confirmation: connected, clash finished, responsive. All five cases now have user responsiveness confirmation.
