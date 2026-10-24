# Capture and restoration traversal optimization

## Scope
No delay, prediction, synchronization, native-load order, field policy, ownership lifetime,
round safety or exact endpoint comparison changes. All published payload bytes remain freshly
captured. Indexes exist only within one validation/capture call; they are not reused across
native updates or restoration mutations.

## Ghidra evidence
Pinned executable SHA256 F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553.
Fresh read-only decompilation: work/ghidra-sc6-focus/traversal-reuse-audit (workspace root).
Functions 34B8D0, 34A100, 33A120, 339E20, 333FD0.
34B8D0 walks owner lists through +28 and group/node links through +40/+48/+28,
invoking mutable native updates. This supports rebuilding capture indexes from the current
walk, not treating topology as constant across frames. 34A100 and the sway routines retain
owner/parent dependencies; they must still be validated. 333FD0 removes/frees effects,
so no effect pointer validity is cached by this change.

## Changes
- Capture group duplicate detection uses a bounded per-list address index instead of scanning
  all preceding group and payload records once a list exceeds eight groups. Owned-target duplicate detection uses a per-side
  index shared by both owner lists once it exceeds sixteen targets. Tiny lists keep the original scan. Existing limits and rejection reasons are retained.
- D78 dependency validation lazily indexes eligible captured parents by address AND side.
  Existing suffix, type, size and owner checks remain. Index saturation falls back to the
  original search, rather than introducing a new acceptance limit.
- Wind validation counts kind-23/24 children once per invocation, rather than scanning every
  record for every parent. Missing/duplicate child checks, invalid-parent checks, live reads,
  all-before-write checks and post-write exact comparisons remain.

## Validation
TraversalOptimization compares 2048 malformed parent variants against the original linear
predicate. Synthetic 512-parent optimized run measured 208.008 -> 4.204 microseconds locally.
This is not an end-to-end match speedup estimate. ConstraintSlots covers group cycles,
owner cycles, cross-list duplicates, parent side/type changes and recovery. FullRestore
covers missing/duplicate wind children and the existing full restoration checks.
The full manifest includes the new regression; timings already exported for capture,
restoration and outer updates remain available for the next paired match comparison.

Capture microbenchmark (fresh records identical, targets 0/8/64/256/512): baseline
0.614/1.518/11.598/112.243/381.867 us, optimized
0.634/1.434/7.610/27.085/59.118 us. Synthetic, not a match forecast.
