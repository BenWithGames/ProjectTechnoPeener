# Animation copy diagnostic

RetainedObjectReplay remains original-input reference replay. This revision adds observation only, retaining scalar-reset preservation and all comparison guards.

Pinned executable SHA256 F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553. Interior sites: 2F0F57 (entry gate on animation context +B18), 2F355D (before matrix30 copy, stack RBP+3758), 2F35E0 (after that copy, before matrix31). Nonzero condition copies cache to ring; zero copies ring to cache. At entry RCX/RDX are cache/ring; at both copy sites RBX/R13 hold them. RSP is aligned to16 at these sites. Pinned bytes are checked before installing all hooks.

Each event preserves raw cache/ring translation words at +7B0, +7B4, +7B8, +7BC, cache/ring addresses, entry mask, branch condition, frame, thread, execution phase, validity and monotonic sequence. Phase0 is natural/unclassified; phase2 is the existing reference-replay scope. Missing events do not prove a pass; count, overwrites, invalid reads and drops remain explicit. No stage/bone semantic name is assumed for index30.

4096 events are retained in a bounded overwrite ring. Hook callbacks allocate nothing and use try-lock recording. Assembly bridges preserve volatile registers, flags and legacy FP/SSE state; callback preserves LastError. Failed reads are recorded without modifying game data. This does not force the copy or bypass its condition.

The suite publishes an immutable trace at termination, includes counters in its text report, and writes -animation-copy.bin. Failure evidence also embeds animation.copy. Existing Collect-RollbackReport collects both binary files. tools/analyze_replay_evidence.py decodes either file. Both peers must update (profile12 revision signature changed).

Validation covers direct entry/before/after bridge preservation under a clobbering callback, real MinHook trampolines for both branch directions, no changes to observed game buffers, invalid reads, bounded wrap/drop accounting, phase tags and decoder bounds. Live behavior remains to be verified.
