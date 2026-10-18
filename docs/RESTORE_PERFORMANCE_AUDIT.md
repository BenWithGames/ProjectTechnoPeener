# Restore performance audit — 70076 / 38024

Read-only Ghidra investigation; installed DLL remains ActiveAnimationGroup 12969A5D2679A2FBC1BA7E09FB3FB7E43C0D061D39D0140C4756C6EB467D5083.

Measured full-match results: 38 cycles each, two transitions, clean native match end. Restore average 18.729/19.953 ms; replay 4.445/5.105 ms; archive 3.198/6.495 ms; peak step 36.167/41.156 ms (Ben/partner). These phases are nested within step and must not be added to step. Current 100ms hold is startup-only, not per replay cycle.

Fresh Ghidra exports (workspace work/ghidra-sc6-focus/restore-performance and restore-performance-dependencies): 3841E0 save, 384540 load, 349290 animation reset, 30AE80 player load, 309ED0 opponent reconstruction, 384C40 relocation vector growth.

Findings:
- Build-Native compiles Mod.cpp and FrameRecorder.cpp without /O1 or /O2. Existing local test scripts often use /O2, so production code is slower than their test executables. An isolated build/ABI audit is needed before enabling optimization globally; compiler changes can affect hook-adjacent code, stack-sensitive serializer provenance and undefined behavior. Preserve precise floating-point semantics, do not use fast-math.
- Actual NativeAdjacentRestore::native_equal benchmark, synthetic equal 128KiB payload in 256 records, 1000 calls: /Od 0.269510ms, /O2 /fp:precise 0.048327ms; all 1000 comparisons passed in each. This is one comparator, not a native loader or live-match benchmark; no whole-match improvement estimate is justified. Harness and build commands in repo work/restore-performance.
- FullRestoreStream::prepare_reader_from uses full structure assignment, including fixed 160KiB payload and 4096-entry metadata capacities. NativeBoundarySnapshot also assigns after_save in full. FullStateProbe copy/reset already skips unused transform capacity but still copies other capacities. Candidate: copy only published stream payload and record metadata while preserving every scalar/callback/guard and proving stale tails unobservable. Measure bytes and time before prioritizing over other work.
- NativeAdjacentRestore::native_equal scans every byte even when records are identical. Candidate: memcmp equal-record fast path, preserving identity/provenance checks and existing detailed mismatch path; no hash-based acceptance. FullStateProbe raw mismatch counts also scan payloads bytewise; the same exact-equality fast path can preserve counts and failure evidence.
- Ghidra 384540 loads both players then calls 309ED0 in both directions. That helper writes opponent identity and mirrored transform fields. 30AE80 performs native animation reset, restores control values, rebuilds internal pointers. These are semantic work, not removable copies.
- 384C40 allocates/grows a local 16-byte-entry relocation table and frees old allocation; save/load free it at exit. Reusing native table allocation is a possible later target but requires ownership, allocator and reentrancy proof. No evidence currently quantifies its cost; do not patch it first.

Recommended order: exact-equality fast paths with mismatch regressions and offline timings; selective compiler optimization with ABI/provenance coverage; bounded stream copies; then subdivide restoration timings to identify remaining native vs adapter cost. Keep native loader, all ownership, exact endpoints, diagnostics and round safety unchanged. No code/runtime changes or new game test were made in this investigation.

## Implemented optimization candidate
- Exact equal-record fast path in NativeAdjacentRestore follows the rebuilt-self-pointer guard. Differing records still enter the previous compatibility and full diagnostic scan.
- Raw difference counts use exact 256-byte equality checks, counting every differing byte in unequal blocks. No hashes or sampling.
- FullRestoreStream copies only write_position payload bytes and writes metadata entries, preserving all counters, callbacks and sticky errors. Reusing a smaller image clears obsolete published metadata. Reader preparation and expected-image preparation use this path; archived source remains immutable.
- Main DLL C++ units use /O2 /Ob1 /fp:precise. No fast-math, no native loader replacement. Hook tests run with both the original /Od and the production optimization flags. Assembly bridges remain unchanged.
- Focused regression covers stale payload tails, source isolation, smaller destination metadata, invalid source lengths, sticky errors, exact mismatch counts across block edges, and equal-byte invalid self-pointer rejection.
- Updated synthetic native comparator benchmark (same 128KiB / 256 records / 1000 passing calls): /Od 0.003332ms; /O2 0.002069ms. Original values were 0.269510/0.048327ms respectively. These are not live-match timing predictions.
