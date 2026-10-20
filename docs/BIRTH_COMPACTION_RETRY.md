# Optional birth-history compaction deferral

Run 11308 reached round 2 and stopped at continuous_capture_stage=26. That stage includes binding validation, NativeEffectSession::compact_birth_history and cursor rebasing. The old report does not distinguish its subconditions. Journal failure and session failure remained zero; 334 corrections had completed. Friend 25504 reported no local failure and received peer stop. Thus contention is a supported code defect to fix, but not a proven exact cause of this recorded run.

Code audit: compact_birth_history used edit_births, whose try_access returned false both for a busy gate and for a rejected edit. NativeRollingHistory treated either as permanent failure. This is inappropriate for optional prefix compaction: preserving older journal entries is valid. The gate also serves all routed release decisions, including unrelated releases.

Ghidra read-only audit SC6AllWindAudit, output work/ghidra-sc6-focus/birth-compaction-release: RVA 333FD0 follows the next link, updates the preceding/following links and manager head, then calls native free (return site 3340F9). Do not bypass release bookkeeping or wait on a worker while holding game state. No native allocation/release function is changed here.

Fix: try_discard_prefix returns applied, deferred, stopped or rejected. Only deferred is accepted without compaction. The rolling history rebases cursors only after applied. Owner/session/reference-count checks and stopped/invalid-journal checks remain fatal; mandatory capture/load/verification gates are unchanged. Capacity remains bounded and overflow still rejects. No spin, sleep, retry loop, file I/O or game allocator call is added to the game thread.

suite_birth_compaction_* reports attempts, deferred count, result (1 applied, 2 deferred, 3 stopped, 4 journal rejected, 5 session/retention guard), first cursor and retained count. Tests force lock contention on another thread, verify entries unchanged, release and retry, verify a single compaction, and reject sealed, invalid cursor and stopped cases.

This addresses one concrete cleanup failure mode. Live validation is still required; it does not claim to fix the prior transform mismatch or frame-104 capture failure, nor the separate sustained slowdown.
