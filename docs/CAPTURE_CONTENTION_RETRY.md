# Frame 418 verification capture contention

Ben 62308: NativeRollingHistory capture stage 10, native stage 15, snapshot valid/saved=1. Main report: wind failure 4, runtime capture check 2, gate outcome 2 (contended), one contention, same owner/caller thread, runtime/window not stopped, no object/address/vtable examined, mismatch object/offset UINT32_MAX. This identifies the failed temporary effect verification capture before memory was read, rather than differing effect bytes. Friend 5556 had no local failure and completed 23 corrections before the peer stopped; Ben completed 20.

Fresh focused Ghidra output: work/ghidra-sc6-focus/capture418-gate-audit (2DBC60, 2FC5B0, 334430, 334960). Native effect creation/destruction paths coexist with hooked allocation/release bookkeeping. The C++ bridge audit establishes the mutex contract: release_decision performs bookkeeping under the gate; native allocator/free calls run outside it. The saved log does not identify the particular thread that held the mutex.

Fix: capture_image uses bounded contention retry, at most 64 try-lock attempts, with yielding outside the lock and a 1 ms QPC retry budget. OS scheduling can exceed that budget; this is not a hard real-time deadline. Zero QPC overhead is added to uncontended acquisition. The memory callback runs only once after acquisition; read failures, mismatches, stopped lifetimes, ownership checks, and unsafe states are not retried or ignored. Exhaustion still fails safely. Other gate operations keep their existing policy.

Diagnostics retained; added retry recovered/exhausted counts and last acquisition attempt count to the existing retention report. Existing capture, restore, timing, exact endpoint verification and round checks are unchanged.

Regression tests cover deterministic contention exhaustion, release then successful acquisition, one callback invocation, stopped-lifetime rejection, and no retry of an entered invalid read. Existing hook lifetime, object retention, snapshot binding and replay comparison tests pass. Live validation is required; this addresses this particular transient-lock stop, not every possible effect failure or remaining slow motion.
