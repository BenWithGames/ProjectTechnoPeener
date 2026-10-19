# Lightweight live rollback timings

## Purpose

Run 79392 recorded 22 first-round input-publication intervals above 50 ms (maximum approximately 150 ms). Partner 47932 completed 25 corrections and 41 replayed frames. The pauses cluster near correction activity, but the old reports did not split execution time by operation. This instrumentation measures costs without changing prediction, restoration, verification or round policy.

## Report fields

Each session generation exports `continuous_timing_schema=1`, the QPC frequency, invalid-clock count, and buckets with `calls`, `total_us`, `mean_us`, `max_us`, `max_frame`, `over_16_67ms`, and `over_50ms`. Existing generation prefixes preserve retired-round metrics. Frames are native outer frame numbers, not zero-based session-relative frame numbers. Counts are attempts, including early returns and failed attempts; a timing count is not proof of successful restoration.

| Bucket | Measured boundary |
| --- | --- |
| capture | Owned boundary capture, including dependencies, effects, verification and any normal history eviction; includes endpoint capture and recaptures during replay |
| restore | Entire storage restore call, including endpoint capture, workspace allocation/destruction, preflight, loading, verification and suffix cleanup |
| workspace | Allocation and construction of the adjacent-restore workspaces; destruction remains included in restore |
| prepare | Adjacent-pair preflight and preparation during reverse loading |
| load_fixups | Retained effect restoration and native delayed loading/fixups; checks internal to that routine are included |
| verification | Retained-ownership validation, post-load payload comparison, completion/selector restoration and final blend/camera/ring checks, and effect comparison |
| replay | Adapter input composition and one native update while catching up to the pre-restore head, including the counter check; excludes snapshot capture preceding that update |
| live_update | Same adapter step for a normal update, outside correction catch-up |
| cleanup | Rewind/suffix retirement after successful restore; normal eviction is included in capture |

Timings are inclusive and overlap: **do not sum all buckets**. In particular restore contains several other buckets. Verification includes existing completion fixups; it is not a pure memcmp benchmark. Capture also contains its own safety checks. No native-operation ordering was changed to obtain these timings.

## Hot-path behavior

Each enabled span uses two QueryPerformanceCounter calls and a fixed set of relaxed atomic aggregate updates. The simulation thread performs no timing-related heap allocation, file I/O, string formatting, histogram allocation, or waits. The existing reporting worker formats microseconds. A thread-local binding propagates the session timing sink into existing restoration helpers and restores its previous value on scope exit. Mock/non-live users without a sink do not query the clock.

The simulation thread is the sole writer of a session's metrics. Readers access atomics, so there are no unsynchronized reads of mutable game-thread counters. A report taken during an operation can contain slightly different sampling moments across fields; completed/stopped-generation totals settle after the last span exits. Clock anomalies increment a diagnostic and never affect gameplay decisions.

These are wall-clock durations, including preemption; they are not CPU profiling samples or GPU/present timings. Existing pending-clock diagnostics remain necessary to distinguish synchronization waits. This build does not claim to fix stutters and does not restore the old 100 ms experimental hold.

## Regression tests

CombatUpdateAdapterTest verifies unit conversion, thresholds, maxima/frame attribution, invalid clock handling, nested/disabled bindings, exception unwinding, and both-slot separation of three replay updates from normal updates without changing counters or update count. NativeRollbackWindowTest verifies capture/restore/workspace/cleanup attempt counts and retained timing evidence after failed restoration. Existing verification, round handoff and ownership regressions remain in the full build.

Logs: `build-native/live-timing-adapter.log`, `build-native/live-timing-window.log`, `build-native/live-timing-full-build.log`.
