# Round-end playback audit — 72328

Pinned executable SHA256 F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553.

User observed the freeze during SC6's end-of-round highlight, before the next round. Six original-input replay cycles completed; cycle seven restored successfully but failed endpoint verification. No round-identity change was observed.

## Ghidra evidence

Research output: workspace work/ghidra-sc6-focus/round-end-audit and round-flow-audit; scripts SC6RoundEndAudit.java and SC6RoundFlowAudit.java. Research names are annotations, not original symbols. RoundFlow uses PE unwind ranges including chained fragments.

- RVA386530 adjudicates round outcomes from player+3D0 and other conditions, writes the 16-bit result at RVA4846408, and queues the post-round state at RVA4843ED8. Timer expiration is explicitly written as player+3D0=4 by RVA384EE0; therefore health zero alone cannot cover round endings.
- RVA37D670 saves native states, loads a historical state using RVA384540, sets the 32-bit playback flag RVA48463F8 to 1 (instruction RVA37DE6A), runs playback, then restores the saved state and clears the flag (RVA37E5EE).
- RVA385450 directly returns whether RVA48463F8 is nonzero; it is a native Normal-mode virtual method.
- Native loader RVA301B10 restores both current and pending battle-state pointers (4843ED0/4843ED8). Consequently Normal-mode identity can describe recorded combat during highlight playback, not live combat.

## Archived failure confirmation

Read steam-inputs-72328-retained-evidence.bin without altering it. Module base derived from expected record349 writer minus its report RVA30A9CE: 7FF79AA40000. Native payload record664 covers both globals.

| Snapshot | Playback 48463F8 | Round result 4846408 |
|---|---:|---:|
| saved | 1 | 0 |
| expected | 1 | 0 |
| restored | 1 | 0 |
| actual | 1 | 0 |

This confirms the failed window was captured inside native playback. A round-result-only check would also miss this window: historical combat had result zero. It does not prove every earlier mismatch had this cause.

## Required integration

Extend fresh owner-thread fight admission with an explicit native-playback exclusion and round-result exclusion, preserving current mode/handler, frame, generation, thread and round checks. Reject unknown reads. Discard an unmodified partial window if either changes before restoration. Do not abandon a partially restored game. Keep all endpoint verification and diagnostics. Report marker values and skip reasons, and regression-test Normal mode with playback=1/result=0, completed-round results, partial-window cancellation, and resumption only after live combat readiness.

No runtime code, DLL, installation or running process changed in this investigation. Integration and live validation remain pending.

## Implementation

RoundFightReadiness now reads the 32-bit playback and 16-bit result globals at the fresh owner-thread boundary. Status4 denotes playback, status5 a declared result. Any nonzero marker denies admission, including historical Normal mode. RetainedObjectReplay uses its existing unmodified-window discard/ownership cleanup path and continues waiting; no already-restored transaction is abandoned. Schema2 reports marker values and both wait counters. Full-match handshake signature changed so older peers cannot silently join. Existing exact endpoint verification and round safety remain unchanged. Regression coverage includes all 65,535 nonzero result values, nonzero playback flags, partial-window discard with no restore, and resumption.

Validation: focused NativeRollbackWindow tests, full Build-Native.ps1 (including RecorderHook native-memory reads), and Test-TestSuite.ps1 passed. Packaged NativePlayback build; not installed or live-validated.
