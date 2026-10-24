# Admission-to-combat execution gap

## Evidence and scope

Paired match 0bde3c8ec11f1c3aa0872c6b3bb7648c: local round 3 head 264
was admitted at QPC 332598148573. The next outer entry was 332598316483,
16.791 ms later at 10 MHz. The outer clock-call count advanced twice between
outer entries, with one pending reservation. Its eventual combat CPU work was
6.729 ms. These distinguish execution scheduling delay from correction cost.
The old journals do not record engine tick-list decisions or world pause state,
so the entire delay cannot be conclusively attributed to a particular engine
scheduler branch from these captures alone.

Read-only Ghidra audit: work/ghidra-sc6-focus/admission-gap-audit and
admission-gap-pause-callers, plus bounded machine code in
missed-update-pause-audit and missed-update-sequence-audit.
* 3FDFA0 phase 2 assigns reason 2 when native confirmed input is unavailable;
  reasons 1/3/4/5 have separate transition/pacing/timeout semantics.
* 3E9EE0 sets network pause bit 1 for a nonzero reason and clears it on zero;
  it consumes 4425 and updates the wait history at 3A8.
* 433290 changes only the requested pause bit. 438980 combines pause sources
  and calls the native controller pause operation. 2049490 queries the world
  object returned by 1C20550, field 500. No direct world-field writes are added.
* 3FE520 consumes clock frame minus the outer consumed frame. Zero delta runs
  no combat-loop iteration. Only 3E2000 increments the admitted clock frame.

## Change

Previously, a valid prediction-limit rejection left the engine network pause
set until a later admitted/pending update released it. Now an owner-thread,
validated active prediction-budget wait clears that side effect through the
existing native zero-reason waiting routine. The combat clock remains stopped.
No confirmation, reservation, combat update, or additional prediction is created.

Permission requires available reservation mapping, successful prediction/fight
eligibility, prediction authorization, failed admission, and session admission
reason 5 with a quiescent reservation. Native reasons other than 0/2 never clear.
The original wait history is restored; unrelated pause bits remain intact.
Failures retain fail-closed handling. Native-only operation and transitions are
unchanged. No sleep, grace wait, extra delay, direct combat call, or catch-up loop
is introduced. Adaptive 0--2 delay and maximum prediction 3 are unchanged.

This removes the identified pause latch during eligible budget stalls. Live
paired validation is still needed to establish the reduction in execution gaps
and any effect on noncombat presentation while combat remains withheld.
It cannot eliminate waiting for truly missing inputs at the prediction limit,
or recover an already-lost phase lead by itself.

## Diagnostics and tests

All prior diagnostics retained. prediction_native_pause_budget_releases counts
native clear-path calls during valid budget stalls. Outer timing schema 3 appends
admission QPC, clock, identity, and frame; admission-to-outer statistics include
only a matching single pending delta consumed by one live update. Stale stamps
from another clock/round/frame are excluded. This measures post-clock-advance
to outer entry, not input-to-display latency or rendered FPS.

Regression cases exercise native reasons 0--5; authorization, reservation,
readiness, stop and candidate rejection; twenty repeated budget stalls without
advancement followed by one confirmed reservation; preserved pause bits and wait
history; existing recovery/transition/ownership tests; matching and stale timing
attribution. The test executable was renamed after an earlier test-only stack
overflow left the prior executable locked; large new test fixtures use the heap.
Local tests do not substitute for live validation.
