# Stage wind allocation-entry replay

Implements the creation-state gap established in EFFECT_ACCUMULATOR_76196_36908.md and WIND_ORIGINS_AUDIT.md. Fresh focused read-only Ghidra validation for this implementation is in workspace `work/ghidra-sc6-focus/stage-birth-implementation/`; all seven emitter/setup/update/query/allocator exports completed against the pinned executable.

## Native contract

The exact allocation return site 0x334A1E requests 0x1E0 bytes for Iw_WindForStage (vtable 0x3E88CE8). Its constructor, spawn setup and initial update leave +0x138 unchanged. Advance at 0x3336CF reads this accumulator before storing the sum; +0x13C and +0x140 derive from it. Spatial query uses the result. Neither padding exclusion nor endpoint replacement is appropriate.

## Change

EffectBirthJournal records all four recognized allocation event identities, in owner-thread order, during the bounded snapshot capture window. Only the exact stage site reads payload: the original four bytes at +0x138, immediately after the native allocator returns and before construction. Normal gameplay memory is unchanged.

Each binding stores its journal window and event cursor. Restoration seals the original journal. Immediately around each original-input replay update, NativeRollbackWindow opens the source-to-next event interval and closes it even when the update fails. Missing/extra/reordered events, wrong size/site, stale window/session/round, overflow, or inaccessible memory fail. Owner-thread and existing lifetime-gate checks precede access. The stage allocation receives only its original four-byte entry value; the native constructor and update still compute all later fields. Other effect types are recorded for order validation but never receive payload writes.

Original allocations pinned by snapshots remain separate from new replay allocations. No retired endpoint object is presented as a newly allocated object, no allocation result is redirected, and no expected endpoint bytes are substituted. Existing exact native/retained endpoint verification still determines success. Existing emitter identity/configuration checks and round/fight guards remain in place.

The event match is bounded site/type/order within an already verified original-input interval, not a general semantic identity scheme for arbitrary predicted branches. It does not authorize using original birth events in unrelated frames or prediction paths. Only replay_reference opens substitution intervals.

The journal is disabled after the last owned binding retires and reset before the next capture window. Capacity is 256 events and 256 substitution diagnostics. No heap allocation, file I/O, sleep or new native hook is added to the allocation callback.

## Diagnostics and checks

Reports add suite_wind_birth_{schema,events,applied,intervals,failure,cursor,end}. Evidence adds wind.births containing original allocation addresses/site/type/seed, replay addresses and raw before/saved/after words. Existing raw effect images and all other diagnostics remain. Failure code 8 in NativeEffectSession identifies journal interval failure; the journal has the detailed reason. Neither a zero substitution count nor a passed transport check alone proves the failing spawn was exercised.

EffectBirthJournalTest covers unchanged original allocations, exact four-byte scope, unusual raw bit patterns, native recurrence equivalence, non-stage pass-through, multi-interval and empty intervals, wrong round/window bounds, missing/extra/reordered allocation rejection, capacity exhaustion/reset and inaccessible payload rejection. EffectUpdateObserverTest exercises the actual observer-to-retention callback route, preservation of LastError, and wrong-thread refusal without payload modification. Tests are integrated into Build-Native.ps1.

FullMatchReplay signature changed so both PCs must install this build. This remains verified original-input replay through a match; it does not add prediction or bypass native network waiting. Live validation must observe a stage birth substitution and successful exact endpoint checks to establish the fix, including continued play and normal round transitions.
