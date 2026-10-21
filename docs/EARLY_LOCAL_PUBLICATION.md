# Committed local input before admission

Goal: reduce prediction-limit stalls without adding input delay or expanding the three-frame prediction/restore limit.

Evidence: friend steam-inputs-24356 continuous-events contains 6,392 ready admissions (detail 0) and 904 withheld admissions (detail 5). BoundedRollbackSession::admission_evidence identifies reason 5 as an exhausted prediction budget. This matches the 904 active-round zero-update gaps found using dispatcher deltas across adjacent outer timing rows. These are simulation updates, not measured rendered FPS.

Previously the mod published a local word only when the native combat input filter appended the frame, after scheduler admission. The new path publishes the next frame's already-committed local native word at the prediction gate before scheduler admission, including while admission is withheld. This can give the other peer confirmation earlier without simulating another frame.

Ghidra evidence: early-local-audit, RVAs 3F2B60 and 3FDB80, pinned image F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553. 3F2B60 skips writing an already-valid entry with matching identity/frame; the native online service precedes our prediction gate. NativeFilterAccess consumes that tagged local ring word after clock advance. The new path neither samples controller state again nor modifies native rings/cursors.

Admission requires an active compatible session, owner thread, matching clock and expected battle frame, fight readiness, matching native entry identity/frame/validity, and the route's next delivery index. No arbitrary future entries are published. Repeated publication must match exactly. Filter append verifies the same raw value and reuses its original publication timestamp without a second send. Changed words fail explicitly; outgoing history remains immutable. Native wait checks, scheduler prediction budget, all restoration verification, round identity and handoff checks stay intact.

The immediate sender wakeup and direct-target restoration remain included. New continuous_raw_early_publications and continuous_raw_early_rechecks diagnostics use the existing owner-to-report atomic snapshot. Compare stall count/time, publication age and confirmation age in the next match. This is a tested optimization candidate, not proof of sustained 60 updates per second.

Tests cover both slots, no delivery-head movement on early send, duplicate equality, changed-word rejection, append equality, skipped-frame rejection and handoff/retirement. Existing route, filter, replay, transport and round tests run in the full build.
