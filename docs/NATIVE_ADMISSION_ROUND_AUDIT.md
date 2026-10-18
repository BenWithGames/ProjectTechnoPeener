# Native admission and round handoff audit — 2026-10-18

Status: live prediction is not enabled. Do not label the existing FullMatchReplay
runner as predictive networking or use these findings to bypass all native waits.

## Binary evidence

Pinned executable SHA256:
`F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553`.
Read-only Ghidra exports are under workspace
`work/ghidra-sc6-focus/prediction-round-admission/` (fresh exports this audit),
`prediction-input-publication/`, and `prediction-envelope/`.

* RVA 3FDFA0 computes the native wait reason after network servicing. Phase 2
  can report remote availability (2), local lead (3), or stalled progress (4).
  Other phases include startup, disconnect and completion. A missing remote
  record alone does not authorize advancing through all those states.
* RVA 3E9EE0 consumes and clears byte 4425 and changes counter 3A8. Its return
  depends on the prior counter as well as the reason. A pre-service sample or
  the reason read after this function cannot substitute for the actual reason.
  NativeClockAccess now retains a separate post-service/pre-wait observation,
  while still running the original wait function and honoring its result.
* RVA 3F0720 returns the tagged input word without checking the validity byte.
  Writing speculative tags into the native ring would therefore affect native
  consumers even with validity left clear. Do not treat validity=0 as isolation.
* RVA 3D73F0 publishes through a dynamic listener list (virtual calls), not just
  the battle update. The observed listener is the native input recorder (see
  the runtime audit below); other possible bindings remain unproven.
* RVA 3FCD10 updates the outer owner's previous raw buttons (1498), previous
  envelope (14B8), current envelope (14A8), suppression and restriction masks.
  Replaying the inner battle function alone does not rerun this outer path.
  A native prediction implementation must explicitly own this input history
  and its confirmed correction; repeating the already-filtered envelope is not
  proof that every native consumer sees the same corrected stream.
* RVA 384040 initializes fighter state during round preparation. RVA 384EE0
  still executes paths conditional on round result 4846408 and playback
  48463F8. A round counter change is not sufficient evidence that fighting has
  begun. Preserve RoundFightReadiness checks and the existing playback guards.

## Changes validated locally

* BoundedRollbackSession refuses a new begin while speculative frames or an
  unapplied correction remain. Receipt of equal input is insufficient until
  reconciliation retires it. Reusing the same session/round identity is refused.
* CombatBoundaryReservation exposes quiescence only when neither a reserved
  clock advance nor its pending battle step exists and no failure is latched.
* NativeClockAccess preserves the reason before the stateful wait call. It does
  not override waiting, alter confirmation cursors, or write speculative input.
* Regression coverage includes changed and unchanged late input at round reset,
  stale-round traffic, pending clock/battle reservations, and reason clearing.

These are prerequisites, not completed native round rearming. In particular,
the settled scheduler predicate does not certify native resource ownership,
native round readiness, transport quiescence or bilateral agreement.

## Remaining integration required before a live-prediction test

1. Supply a separately owned, frame-tagged predictive input stream at the native
   consumer boundary. Preserve authoritative ring entries and confirmation
   cursors, local-input availability, previous-button semantics and filtering.
   Audit listener behavior before allowing speculative native publication.
2. Coordinate the final speculative combat frames with round completion.
   A predicted KO cannot discard unresolved corrections or restore across the
   already-entered end-of-round playback. Settlement must precede handoff.
3. Retire old histories and effect ownership on the simulation thread, with
   no pending clock reservation. Replace routes only at that verified boundary;
   keep old route/exchange objects alive for in-flight worker reads.
4. Establish a fresh round agreement and frame mapping, then rearm only when
   native fighting readiness is established. Transport publication currently
   binds one round and a finite recording; it needs a round-aware lifetime
   rather than resetting a live shared buffer under the network worker.

No endpoint verification, effect ownership, camera/RNG/animation diagnostics,
or round safety check is removed by this audit. No live performance or latency
claim follows from the local regression tests.

## Native recorder and outer-state audit (2026-10-18)

Evidence: workspace `work/native-input-delivery-67876.json`, read-only inspector
`work/inspect_native_input_delivery.py`, and Ghidra exports in
`work/ghidra-sc6-focus/prediction-native-recorder/`,
`prediction-listener-binding/`, `prediction-listener-type/`,
`prediction-listener-target/`, `prediction-listener-owner/`,
`prediction-outer-boundary/` and `prediction-state-queue/`.

The pinned binary's listener registration at 4281D0 binds a component through
401CC0 to holder vtable 3298810. Its target getter 41D7F0 returns component+390,
whose interface vtable is 3291350. Functions 428580/428750 start and stop the
recording; 428E20 appends both players' input words and increments its frame
count while 427930 reports active. Native publisher 3F6600 calls this listener
through 3D73F0 before incrementing its publication cursor.

The active private-match capture found one verified clock/owner backlink and
159 samples spanning native frames 475 through 953. Every sample had outer
mode 2, one listener of the audited recorder type, recording enabled, and round
index 2. Of those, 157 samples had stable clock and list observations; their
recording count equaled the publication cursor. Two samples crossed an update
and are not coherent frame snapshots. This read-only sampling is evidence for
the active binding, not proof of arbitrary thread-safe reads or all listeners.

Consequently, speculative tagged ring entries cannot safely be published by
the unmodified native publisher: raw lookup ignores the validity byte and the
listener would append predictions. Native recording must receive confirmed
inputs, while speculative combat delivery preserves its own prior-input state.
No publication override or ring mutation was performed in this inspection.

Ghidra also shows 3FE520 calling 3FCE80 and 3FCA60 after the inner battle update.
3FCE80 consumes the outer state queue (1470/1478/147C), changes mode 1480, and
invokes lifecycle functions. These operations are outside SingleUpdateCall.
The observed queue was empty in all 159 samples; no round transition occurred.
This does not validate deferring or replaying queued state changes. A predicted
KO must settle before these lifecycle effects are committed; merely swapping
the rolling session at a changed round identifier is insufficient.

This audit made no production changes and did not install a new build.
