# Critical Edge exit recheck (match 3afe089a631b2754c49c7a9bdfddd2c8)

## Evidence and limits
Local PID53612 reports no restoration/replay failure. At native frame14415, upstream event3 occurs. Settlement at endpoint14416 completes, confirmation/input routes retire, and native outer continuation commits; rearming then waits. Peer PID8156 stays on round4, head2489/confirmed2486, repeatedly denied by the prediction cap. Its archive is recovered after exit, without a final failure report. No dropped rollback journal events were reported. This is not evidence of a camera byte mismatch.

Old reports do not save the fight observation before/after settlement. Therefore the exact speculative marker at that event and whether correction cleared it are not proven. A stale pre-settlement exit decision is a concrete code defect consistent with the asymmetry, not a retrospectively proven diagnosis of every Critical Edge freeze.

## Native audit
Fresh Ghidra headless outputs: work/ghidra-sc6-focus/critical-edge-handoff-audit and critical-edge-exit-writer; scripts SC6RoundEndAudit.java and SC6CriticalEdgeExitAudit.java. Pinned binary unchanged.
RVA386530 adjudicates player result state (+3D0), writes shared result4846408 and pending state4843ED8. RVA384EE0 reads shared result/playback to choose Normal update behavior. Earlier audited native loader301B10 restores current/pending state; shared snapshot payload includes result/playback. The post-battle observation therefore must be re-read after restoration/replay. The camera swap itself is not sufficient proof of a round boundary.

## Change
NativeCombatSession now settles first, then reads fresh RoundFightReadiness. If Normal fighting is restored, it resumes the same fully settled session, retaining snapshots, input routes, history and identity. It does not claim an outer retirement receipt in this case. If fighting remains ended, only then request raw-route handoff, claim continuation and retire. Unreadable state, failed reconciliation, wrong identity/frame/thread, unresolved inputs and failed verification still fail closed. End-of-round playback remains excluded. Adaptive delay/prediction limits and optimization paths unchanged.

PostBattleSettlement resume is allowed only when no wait/poll is entered, its exact endpoint/owner/identity is unchanged, and the session is fully reconciled. No partially restored state may resume. Regression covers a corrected speculative exit, pending truth, wrong identities/endpoints, repeated genuine exit and terminal failures. Diagnostics add candidate/resumed/committed counts and pre/post status, vtable, handler, playback and round-result fields (state0..4 before,5..9 after).

Live Critical Edge retest required; diagnostics will distinguish a corrected exit from a genuine exit or a different failure.
