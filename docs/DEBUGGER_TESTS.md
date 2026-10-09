# Debugger shutdown repair

The diagnostic now holds the final debug event instead of resuming with armed
breakpoints. It suspends the tracked threads, restores its owned debug-register
slots, continues the held event, and resumes the threads while still attached.
It handles already-pending owned single-step events before detachment. The idle
timeout path requests and consumes a debug break before cleanup.

Debug-event process/thread handles are no longer closed prematurely. Windows
manages their lifecycle; early closure could allow a recycled handle value to
be used incorrectly during subsequent events. File handles supplied by load
events are still closed explicitly.

## Disposable target

The same native program has a `--self-test` path. It creates its own target
under the Windows debugger API; it never locates or attaches to SC6. Two worker
threads exercise execution breakpoints and a shared timer write watchpoint.
After detachment the parent checks target liveness and continued counter
progress, signals normal shutdown, and checks the exit code. Idle targets are
checked for liveness and clean exit, without requiring counter progress.

Four scenarios are covered:

- Event limit: stop after 100 events.
- Timeout: stop at 150 ms while workers are active.
- Cancellation: set the same stop flag used by the console handler at 150 ms.
- Idle timeout: no watched activity; request a debug stop and detach.

Cancellation testing exercises the flag path, not delivery of an actual console
Ctrl+C signal. These tests do not establish every possible exception, thread
lifecycle, forced termination, or game integration behavior.

## Results

Earlier versions failed repeat testing; those results were not treated as a
successful repair. The final revision passed 10 repetitions of each scenario:
40/40 passes, with restoration, detachment, liveness, and normal exit verified.
The saved PowerShell runner was then verified with two repetitions per scenario,
giving another 8/8 passes (48 total on the repaired shutdown implementation).

Run the suite from PowerShell:

```powershell
.\scripts\Test-Debugger.ps1
```

It compiles with `/W4 /WX`, creates a fresh directory under `build-debug-tests`,
and saves every trace plus results.json. It stops on the first failure.

All live SC6 tracing remains disabled. Passing synthetic tests is a prerequisite for
a future short game validation, not evidence that the prior SC6 crash is fully
explained or that a simulation hook is safe.

## Short SC6 test, 2026-10-07

User-supplied trace: captures/caller-smoke-user-01.txt. The 100-event limit was reached at 559 ms: 22 routine entries, 67 caller entries, and 11 timer writes, all on thread 43684. Cleanup reported restored=1, detached=1, failed=0. The post-detach check reported the process alive and the timer advancing from 11756 to 11877 over the two-second check. This verifies this short run, not general debugger stability. User confirmation of movement responsiveness is still pending.

The trace repeats six caller entries and two routine entries per observed P2 timer increment across complete cycles. The parent return sites remain RVA 0x34E0A7, 0x34E0BB, and 0x34E0D2. Next: statically inspect that parent before selecting another bounded observation target. The player timer candidates differ by 74 during this capture; neither should yet be treated as a shared authoritative simulation-frame counter.
