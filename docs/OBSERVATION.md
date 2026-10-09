# Read-only training observation

`tools/observe_training.py` samples candidate SC6 timer, input, and move-ID
locations through Windows ReadProcessMemory. It requests query/read access only;
it does not write memory, install hooks, inject inputs, or modify network behavior.

Enter offline Training, then run with 64-bit Python:

```powershell
$sc6Process = Get-Process SoulcaliburVI
python .\tools\observe_training.py --pid $sc6Process.Id --training-confirmed --seconds 10 --output .\captures\idle.csv
```

The explicit training flag is a user assertion, not automatic game-mode detection.
The executable SHA-256 must match the currently inspected installation. An
unknown executable or failed memory read stops the probe. Existing captures
are not overwritten.

Candidate offsets were researched in SCUFFLE's Config/address_map.ini (which
overrides AddressMap.py) and SoulCaliburGameState.py at revision
b8e8129b58417b8b71189f05ea3e789b09493f10:
https://github.com/FottenSC/Scuffle/tree/b8e8129b58417b8b71189f05ea3e789b09493f10
License: [SCUFFLE MIT license](licenses/SCUFFLE.txt).

Validation sequence:

1. Stand idle for five seconds and observe timer progress and neutral inputs.
2. Hold A, B, K, and G separately with neutral gaps; compare raw button values.
3. Hold directions separately, then test P2 independently if a controller is available.
4. Pause/resume and reset training to characterize the timer.

The probe reads the timer before and after the other fields and drops samples
whose timer changed. This reduces torn observations but does not establish an
atomic simulation snapshot. Polling can repeat or miss frames. It is not an
input recorder suitable for deterministic replay and is not a battle-frame hook.
Do not infer complete state or deterministic behavior from these few fields.

## Initial observations on this installation

- `captures/idle-01.csv`: AddressMap.py defaults returned all zeros; rejected.
- `captures/idle-02.csv`: configured timer 0x47F1428 advanced 2424 to 2725
  over about five seconds (60.22 counts/second). 301 distinct values were
  sampled; one polling gap missed a count. P1/P2 input values were zero.
- `captures/inputs-01.csv`: P1 raw inputs included 0, 1, 2, 4, 8; P2 stayed
  zero. The timer advanced by 1804 in about 30 seconds. P1 candidate move IDs
  included 264, 298, 329, and 65535; P2 included 8229 and 65535. Their exact
  semantics are not yet established.
- `captures/directions-01.csv`: P1 raw inputs were 0, 1024, 2048, 4096,
  and 8192, corresponding to high-byte direction flags 0, 4, 8, 16, 32.
  The low-byte buttons stayed zero; P2 stayed zero. Do not interpret these as
  numpad directions. P1 candidate move ID 89 appeared during movement.

## Pause and reset checks

- `captures/pause-reset-01.csv`: timer remained at 11334 from 8.403 to
  14.086 seconds (about 5.68 seconds), then resumed. The user confirmed this
  run included pause/resume only, not reset.
- `captures/reset-01.csv`: timer decreased from 19703 to 1 at 15.973 seconds
  during the requested training reset, then advanced to 242. Polling did not
  observe zero and must not assume the first observed count is the first frame.
- Both captures missed four timer counts between samples, confirming that
  this polling approach is unsuitable for exact input replay.

Run `python tools/analyze_capture.py captures/reset-01.csv` to summarize
plateaus, decreases, and unobserved counts. A decrease is an observation;
without action context it cannot distinguish reset from wraparound or state changes.

## Frame-boundary research

A static scan of the inspected executable found no direct RIP-relative references
to the configured timer/input addresses or within 0x1000 bytes of them. A wider
scan found references elsewhere in the battle-data region. This suggests indirect
structure access but does not prove any specific structure base or layout.

A hypothesized player-structure-relative timer offset also produced no validated
instruction match. No writer or battle-update function has been identified.
The next research step is to trace writes to the timer during offline training,
record the writer's call stack, and compare its call count with the observed
timer across idle, attacks, hitstop, pause, and reset. Only then evaluate a hook;
the writer may update one player rather than advance the entire fight.

### Bounded hardware-watchpoint diagnostic

`tools/WatchTimer.cpp` and `scripts/Watch-Timer.ps1` implement an x64 Windows
debugger diagnostic. The wrapper checks the executable hash and requires explicit
offline-training confirmation. It compiles locally, attaches, places a 4-byte
write watchpoint on the timer in debug-register slot 0, and records up to eight
hits with register values and stack addresses. It times out after 15 seconds,
restores modified debug registers, and detaches. No executable instructions or
game memory are patched. Existing occupied slot 0 causes it to abort.

Compilation with warnings as errors succeeded. Attachment from the agent's
execution environment was denied by Windows (error 5), so no runtime watchpoint
or stack capture has yet been validated. The game remained running.

From a normal PowerShell terminal at the project directory, while offline
Training is unpaused:

```powershell
.\scripts\Watch-Timer.ps1 -GameProcessId (Get-Process SoulcaliburVI).Id -TrainingConfirmed -OutputPath "$PWD\captures\timer-writer-local-01.txt"
```

Do not run alongside another debugger. Inspect the final `restored` and `detached`
fields after a successful capture. The instruction pointer is the instruction
AFTER the memory write, not necessarily the writer's address. Stack addresses
are candidates for analysis, not proven simulation hooks.

### Successful user-run watchpoint capture

`captures/timer-writer-success-01.txt` contains eight consecutive writes on
thread 53160, with timer values 40726 through 40733. All have the same stack.
The tool reports `restored=1 detached=1 failed=0`.

Disassembly from the PE unwind function boundary confirms:

```
Function RVA: 0x2FFEB0 (end 0x300108)
0x300097: inc dword ptr [r10 + 0x458]
0x30009E: comiss xmm4, xmm7   ; post-write instruction pointer
```

The function assigns r10 from its second argument (rdx) at entry. The watched
timer therefore corresponds to an object at RVA 0x47F0FD0 plus field 0x458.
This is derived from the watched address and confirmed instruction, not a
complete structure definition. Caller return RVAs begin 0x2FFE48, 0x2FE069,
0x34E0A7. Their containing unwind ranges begin 0x2FFE32, 0x2FDEA0, and
0x34DCE1 respectively; these can include compiler-generated fragments.

This identifies the timer writer, NOT the full battle-step boundary. The
function also updates float and 16-bit fields and may be a per-player animation
or timing routine. Eight idle writes do not establish behavior during attacks,
hitstop, reset, or other modes. Investigate callers and count per-player invocations
before installing a simulation hook.

### Caller correlation capture

**Disabled after a reported game crash.** See
[incident analysis](CALLER_TRACE_INCIDENT.md). The instructions below describe
the quarantined diagnostic; do not run another game capture until shutdown has
been repaired and validated against a disposable process.

Add `-Mode Callers` to Watch-Timer.ps1 to use three hardware slots:

| Slot | Type | RVA | Purpose |
| --- | --- | --- | --- |
| DR0 | Execute | 0x2FFEB0 | Timer routine entry and argument pointers |
| DR1 | Execute | 0x2FDEA0 | Candidate caller entry |
| DR2 | Write, 4 bytes | 0x47F1428 | P2 timer writes |

This mode stops at 30 seconds or 12,000 events, whichever comes first. It also
reads the P1 timer candidate at 0x475A038; that candidate is not independently
validated. It logs stacks only for the first six events to limit debugger cost.
Execute breakpoints use the processor resume flag to execute the stopped
instruction once before the next breakpoint. Slots 0-2 must be unused and are
restored on normal completion. Run only via the hash-checking wrapper.

Compilation with warnings as errors passed. The new mode has not yet been
validated against the running game; the earlier write-only mode was validated.
Collect separate idle and attack captures before drawing call-count conclusions.
Debugger event handling can slow the game, so wall-clock throughput is not a
performance measurement or proof of hitstop behavior.

```powershell
.\scripts\Watch-Timer.ps1 -GameProcessId (Get-Process SoulcaliburVI).Id -TrainingConfirmed -Mode Callers -OutputPath "$PWD\captures\callers-idle-01.txt"
python .\tools\analyze_callers.py .\captures\callers-idle-01.txt
```

Start with an idle capture. Then repeat to a different file while performing
attacks that hit the opponent. Inspect per-routine second arguments to determine
whether the same routine is invoked for multiple player/timing objects, then
compare candidate caller entries with both players' updates. No game-control
hook is installed by this diagnostic.

The observations support the configured timer/input candidates. Button values
are consistent with SCUFFLE's bit flags, but exact mappings require controlled
action correlation. P2 controls and timer behavior during hitstop remain untested.
No candidate offsets have been integrated into the native DLL.
