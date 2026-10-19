# Live composition across round transitions

## Failure and evidence

Ben 79392 stopped with `composition_local_counter_reset`: the final round-one sample was outer frame 1228, round 1, boundary 97; the first round-two sample was outer frame 1607, round 2, boundary 1487. Round settlement and rearming succeeded. No native restoration was invoked on Ben's PC. Partner 47932 received the stop packet; her first generation completed 25 corrections and replayed 41 frames without reported failure.

Fresh read-only Ghidra output is in `work/ghidra-sc6-focus/round-composition-audit`, using SC6ClockCombatSequenceAudit.java, for clock dispatcher 3F5D20, outer caller 3FBF30, update loop 3FE520 and clock advance 3E2000. Existing `live-startup-admission/002dbc60_BattleEntry.c` and its disassembly show the native outer counter at RVA 470D0C4 incrementing at battle-update completion. This counter is not a count of samples in our owned recording. OwnedInputPublication publishes admitted live frames only, retires across handoff, then appends the next round through bind_after without deleting prior samples. Native updates continue while the owned publisher is retired. Consequently adjacent recorded samples across a round boundary need not have consecutive outer counters.

## Fix

LiveInputComposition's authoritative-peer mode now admits a forward counter gap only with a bound session and known next-round metadata: the round increases by exactly one, the boundary advances, and the new boundary lies after the previous sample and no later than the new sample. Both local and peer streams perform the check independently. Pair staging still requires identical peer round and boundary metadata. Comparison-only tests retain their strict contiguous-counter policy.

Same-round gaps, backward/reset counters, wraparound, missing boundaries, skipped rounds and conflicting peer boundaries remain rejected. This change authorizes input staging only. NetworkReplayEligibility still rejects any replay window crossing a round boundary. No native clock, restoration, ownership, round admission or verification rules are relaxed.

Reports retain every existing diagnostic and add local/remote accepted-gap counts and the most recent before/after/round/boundary tuple. Existing counter-reset failure names remain available.

## Validation

LiveInputCompositionTest covers the recorded 1228-to-1607 case, both player slots, delayed/reordered peer records, a third round, idempotent polling, cross-round replay rejection, and invalid boundaries on either stream. It also verifies that independently valid but conflicting peer boundaries fail staging. Full build includes existing round handoff and restoration regression tests.

Logs: `build-native/round-composition-tests.log` and `build-native/round-composition-full-build.log`.

This fixes the identified composition rejection. A new live run is required to validate continued operation into subsequent rounds. It does not address or claim to improve the reported stutters.
