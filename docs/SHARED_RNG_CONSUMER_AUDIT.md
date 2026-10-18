# Shared RNG consumer audit — 2026-10-17

Read-only native audit; no game/mod behavior changed. Executable SHA256 F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553.

## Scope and method
Scanned executable sections for direct E8/E9 transfers to RVA 34F130 and 34F3C0, then validated instruction boundaries with Capstone from PE exception-fragment starts. Found 280 first-stream and seven second-stream call sites across 74 containing functions (combined). This supersedes the earlier 274 first-stream count; it is not an exhaustive indirect-call or inline-access inventory. Ghidra decompiled ten selected functions using explicit PE body ranges, including chained unwind fragments. All completed.

Inventory: workspace work/rng-consumer-inventory-validated.json. Ghidra evidence: work/ghidra-sc6-focus/other-rng-consumers/*.c and status.txt. Names are research annotations, not original game symbols.

## Additional confirmed consumers
- RVA 341980 uses the first stream at 341B97 in a conditional player-state decision. In mode +1E50 == 2 a random threshold can change the decision; a later mode check can write player +1735, opponent +1736, and player +7C4. These are real player-object outputs, but their precise gameplay labels and applicability in the current online mode remain unproven. Do not label this damage/AI/blocking without tracing downstream consumers.
- RVA 34EA60 uses the first stream at 34EC76 to choose return code 6 versus 1 in a branch involving player +34BC == 14, +3480 == 0 and other player/opponent conditions. Caller 34E1F5 receives this code. Its exact gameplay meaning remains unclassified.
- RVA 38F180 uses the first stream at 38F216 for randomized selection when selection parameter == -2. It rejects the previous choice, retrying up to 100 times, writes previous choice at object +25C, and populates entries in a 24-slot array. Thus one selection can consume a variable number of draws. The selected content's gameplay/audio/visual identity is not established.
- All seven observed direct calls to the second stream lie in 3311F0,3315A0,331980,331E90,332400,332D60,333550. This includes initialization paths as well as already examined effect update paths. In 3311F0, random values populate oscillator/phase-related object fields, and this same initializer also advances scalar RNG 485EB28 inline. Therefore the three RNG states are distinct but their consumer sets are not necessarily disjoint.

## Snapshot and diagnostic implications
TransformSnapshot explicitly captures/restores both shared generators (104 bytes at 485EB30 and 24 at 470E2B0). The existing scalar RNG capture is separate. Generator coverage alone does not prove restoration of all state that decides whether/how often a consumer runs.

The player decision outputs and selector previous-choice/queue state above do not yet have a proven field-to-native-serialization mapping in this audit. They may be serialized already; absence of an explicit offset reference in source does not prove they are missing. Their coverage remains unverified.

Current entry/endpoint generator checks establish whether a replay starts from the correct generators and ends at the expected states. They do not identify all of these callers or their order. An extra cosmetic/selector call could shift the shared sequence used later by another consumer; this is a supported mechanism, not an established explanation of reports 59496/14308.

## Remaining work
Trace 341980/34EA60 downstream semantics and 38F180 ownership/serialization. Attribute shared RNG calls during original and replay execution, with bounded preallocated records if diagnostics are implemented. Audit remaining primary-stream consumers and direct inline state access before claiming exhaustive coverage. Preserve exact generator verification and round safety; do not mask RNG differences merely because some consumers are cosmetic.

## Follow-up: snapshot coverage and caller instrumentation

Ghidra `rng-coverage/0030a980_Coverage_30a980.c` and `rng-load/0030ae80_PlayerLoad.c` confirm native save/read of player+90 for 3510 bytes. The loader preserves pre-load 244,248,324 separately; these are not the six decision fields investigated here. Recorded transfer records 1 and 349 in Ben's cycle-2 snapshot 2 confirm coverage of +1735,+1736,+7C4,+1E50,+34BC,+3480 for both actual player pointers from measurements. Machine-readable mapping: repo work/rng-coverage.json. No additional restoration writes are justified for these six fields.

Ghidra callers 376B20 and 38BD60 pass player+95788 into selector 38F180. Its previous-choice integer is consequently player+959E4. Neither that field nor the 240-byte slot queue at +95788 is covered by a native.saved source range in this capture. Native player save/load also omit this region. This is a concrete coverage gap for this recording, not proof that the selector ran in the failed hop. The external selection tables and their ownership still need separate treatment; do not blindly copy pointers or restore the entire object.

The previous reports cannot identify the differing shared-generator call: rng.callers hooks 34F550 (scalar RNG), not 34F130 or 34F3C0. The two streams' endpoint evidence is insufficient to assign a caller. New SharedRngBridge/Hook/Trace instruments both actual helpers with pinned signatures and register-preserving assembly. The 8,192-event ring stores caller return RVA (call instruction is normally RVA-5), return bits, frame, thread, replay phase and all 32 generator words. No RNG state writes, allocation or disk I/O occur in the callbacks. The observer reads live generators after each call; concurrent native writers can make this observation ambiguous, so thread IDs and dropped/overwritten counts are preserved.

Before/after replay markers identify hop boundaries; binary evidence adds rng.shared_callers alongside all previous evidence. The analyzer reports the first caller/result/state difference against the retained natural-frame sequence. Missing/dropped/overwritten history is explicitly not a proven attribution. Inline generator writers are not intercepted by these hooks. Existing inline scalar diagnostics remain active.

Tests: detoured wrappers each exercised 1,000 times against a clobbering callback, comparing volatile GPRs, flags, XMM values and floating controls; original executes once. Trace tests cover full-state capture, replay phase, ring wrap, contention, invalid reads and LastError preservation. Decoder tests cover different callers, extra calls, mismatched state/results, incomplete coverage and malformed data. All round guards, entry RNG guard, endpoint checks and independent effect verification remain enabled. A fresh live capture is required to name the actual divergent call.
