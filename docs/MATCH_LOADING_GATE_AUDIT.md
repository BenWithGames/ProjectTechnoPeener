# Match loading gate investigation

Status: loading readiness agreement and bounded native gate connected for
matching automatic-play builds. Live validation is pending. This phase moves
hook preparation and peer loading readiness ahead of intros. It does NOT remove
the final owner-thread fighting-boundary/adaptive-delay release agreement.

## Evidence, 2026-10-22

Fresh Ghidra headless runs against the hash-pinned executable:
- work/ghidra-sc6-focus/loading-gate-audit
- work/ghidra-sc6-focus/loading-trigger-audit
- work/ghidra-sc6-focus/loading-flag-audit
- work/ghidra-sc6-focus/loading-advance-audit

The prior intro path is HgBattleModeStart (vtable RVA 3E87378).
384040 writes startup input enable flags. 3840E0 tests the mode counter at +8
against +C, clears native state and sets the next mode through 4843ED8. This is
an intro-to-combat transition, not evidence of a loading-screen readiness gate.
The UE wrapper named SetStartMatch calls 525100, which copies setup data and
transfers allocation ownership; it is not a boolean load-ready predicate.
TriggerBattleStart's exec entry 946920 traverses gameplay event data; its name
alone does not justify treating it as the loading gate.

The live lobby read-only sample (PID 65296) has dummy vtable 3E87320,
no descriptor resources and battle frame zero. Existing readiness in
CombatResourcePreparation is published from a valid battle callback. Existing
SteamHandshake waits for resources.ready and clock_ready, and NativeCombatArming
requires a fresh Normal fighting boundary. Blocking before these callbacks and
waiting for the existing full start agreement would be circular.

## Initial policy investigation (before integration)

MatchLoadingGate binds a match-scoped key, makes nonblocking hold decisions,
requires local preparation plus peer readiness and commit, has a five-second
monotonic deadline, and latches native fallback after timeout, identity change,
unsafe boundary or peer cancellation. Incompatible peers bypass. Late commits
cannot reactivate a timed-out gate. An unexpected modified-simulation state is
contained rather than silently resumed. This policy alone cannot establish
safe bilateral cancellation; the transport adapter must carry cancellation and
exclude stale activation after fallback.

Regression tests cover those policy transitions and are part of Build-Native.
At that stage, remaining work was to identify a repeatedly polled native loading-ready transition,
verify its owner/lifetime and worker progress, split pre-battle negotiation from
battle-frame admission, connect bilateral cancellation/fallback, and validate
first match, skipped intros, rematches, unmatched builds and disconnects.
Do not install the policy as if it implements these remaining requirements.

## Initial native loading predicate and observer

Further Ghidra runs: loading-readiness-audit, loading-predicate-audit and
loading-caller-audit. LuxBattleGameMode::HasFinishedLoadingAssets (UE exec
944C90) calls 3F1870, which resolves the reference at +488 and queries 426060.
The native tick at 3FBE10 checks this predicate in state 1 (+468), advances to
state 3, and continues its base tick. There is also another engine-state path
to the virtual callbacks. Observing this predicate is not proof that overriding
it would gate every online loading path safely.

The read-only loading-gate-transition-2.json capture includes stage intro,
player intro, Start and Normal. Descriptor presence persists through parts of
the transition, so it cannot establish fresh ownership by itself.

The initial LoadingReadinessObserver recorded up to 128 state/result transitions, caller,
thread, battle frame and monotonic timestamp. Records are process-cumulative;
overflow is counted. There is no allocation, file output or wait in the hook.
Installation requires the existing executable hash check and exact prologue.
That observer-only build reported loading_gate_enabled=0. Existing rollback, adaptive
delay, verification and round guards remain in effect.

The observer-only live check was to start from a fresh process in the lobby, load a match and
allow intros, then complete it. Inspect loading_readiness_* in the report to
confirm callback execution and state progression. A successful observer run
does not validate loading synchronization; pre-battle negotiation, cancellation
and safe native fallback still require integration and testing.

## Loading synchronization integration, 2026-10-22

Both reports for match 23aa20402c068514f6179735ddbf518f recorded one native
3FBE50 call at state 1 and battle frame 0, on the later combat owner thread.
Peer report 9124 completed cleanly; local report 59808 later timed out on peer
silence. The user confirms the peer left after playing.

Fresh Ghidra loading-alternate-audit verifies the base tick and Blueprint-backed
ReadyToStartMatch/ReadyToEndMatch queries. Machine code at 3FBEFF tail-calls
3D1DA0 -> 1E671F0. It executes the base tick before alternate queries at return
addresses 1E67216 and 1E6723E. The original native tick is never skipped.

The native asset predicate is called normally. Only return address 3FBE50,
state 1, a published prepared match, and the bound owner/object can withhold a
true result. The two alternate predicates also execute normally and are gated
only for that same object in state 1 at those exact return addresses. Other
GameModes, UObject wrappers, native false results and unrelated callers retain
their native result. Event overflow does not bypass the gate.

On the control thread, prepare the existing resource capture and native hooks,
then enable effect/animation hooks before side discovery. Publish a stable
match-bound loading session only after preparation succeeds. A 48-byte dedicated
message on the mod's existing channel contains both fresh match nonces, build,
stage and reserved-zero word. It never reads native network traffic. Stages:
0 prepared; 1 native assets ready; 2 peer readiness observed; 3 commit observed;
4 native gate released; 9 cancel. Stage messages retry at 20 ms, including for
10 seconds after final activation is queued. Both actual releases are required
before the existing battle-start request may be queued.

The gate returns false to the native state machine, rather than sleeping or
spinning. Native loading/base tick and the independent control worker continue.
Five seconds from the first readiness observation bounds waiting. Timeout,
changed owner/object, cancellation or missed loading disables this automatic
attempt before simulation modification. The game continues through the native
path. For this experimental build a failed automatic attempt remains disabled
until restart (existing terminal automatic-failure behavior). Cancellation after
activation was queued uses existing stop/containment, never unsafe native resume.

The compatibility and automatic-play signatures changed: older builds take
native fallback. Up to 128 match contexts have stable process-lifetime storage;
exhaustion takes native fallback. Successful sessions retire independently and
create fresh contexts on subsequent matches. Late old nonce packets cannot
release a new gate. No delay thresholds, correction settings, exact verification,
resource ownership or round transition guards were relaxed.

Test-LoadingSynchronization covers packet loss, deadlines, terminal cancellation,
identity/build rejection, monotonic stages, fresh-match context lifetime, owner
and caller restrictions, native bypass and cancellation after queue. Live test:
fresh lobby, no warmup, both on this build; load and play normally, then wait for
report completion before leaving. Check loading_sync stage/peer_stage=4,
reason=0, admitted=1, withheld>0 and existing continuous activation diagnostics.
A passing local test is not evidence that the live loading workers progress
while the predicate is withheld; the bounded live run must establish that.
