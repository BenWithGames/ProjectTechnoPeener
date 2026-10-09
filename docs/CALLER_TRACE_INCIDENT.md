# Caller trace: crash report and quarantine

Update: shutdown and debug-handle lifetime repairs are implemented and the
disposable-process suite passed 40/40 repeated tests. See
[debugger test details](DEBUGGER_TESTS.md). SC6 caller mode remains disabled.

The user reported SC6 crashing during/after the first caller trace. The trace
contains 12,000 events over 22.190 seconds and ends at the programmed event cap:

```
hits=12000 restored=1 detached=1 failed=0
routine_calls=2678 caller_calls=7988 timer_writes=1334
```

These flags report API outcomes only. They do not establish that the target
survived detachment or that no pending debug exception remained. No new game
crash dump was found alongside the executable; the existing dump predates this run.

Code review identified a shutdown race: ContinueDebugEvent resumes the target
with watchpoints still armed before the per-thread restore/detach phase. A new
breakpoint can occur in that interval without being handled by the event loop.
This is a plausible cause, not a confirmed diagnosis from a crash stack.

Caller mode is disabled in both the wrapper and compiled tool. Do not run older
copies of WatchTimer.exe in caller mode. The next engineering step is a debugger
shutdown implementation that clears owned breakpoints while the target is stopped
at a debug event and handles pending events correctly. Validate event-cap,
timeout, cancellation, thread creation/exit, unrelated exceptions, and target
survival against a disposable test process before returning to SC6. The original
write-only mode shares the shutdown code; its earlier success is not proof that
it cannot hit the same race. Avoid further game debugger captures for now.

## Useful observations from the captured data

- Timer routine second argument RVA 0x4759BE0: 1,344 calls.
- Timer routine second argument RVA 0x47F0FD0: 1,334 calls.
- Each address plus 0x458 matches the corresponding player timer candidate.
- Candidate caller 0x2FDEA0 appears about six times per P2 timer increment.
- The trace shows caller second arguments 0, 1, and 2 for each player object.
- The observed routine is therefore shared across player timing objects;
  the candidate caller is not a once-per-battle-frame function.
- The next caller level to inspect statically is around return RVAs 0x34E0A7,
  0x34E0BB, and 0x34E0D2. No frame-control hook is justified by this capture.
