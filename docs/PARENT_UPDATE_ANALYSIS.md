# Parent update analysis — 2026-10-07

User confirmed movement and attacks still work after the short debugger test.
Analysis below reads the executable on disk; no new debugger attachment occurred.
Verified executable SHA-256: F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553.

## Findings

The unwind range at RVA 0x34DCE1 is a fragment, not a function entry. Its chained unwind metadata points through 0x34DA9F to the entry at 0x34DA70.

Within that function, calls at 0x34E0A2, 0x34E0B6, and 0x34E0CD invoke 0x2FDEA0 with selector values 0, 1, and 2. These explain the three return addresses in the captured trace.

The next outer function starts at RVA 0x2DBC60. At 0x2DC07F it sets a loop count of two; the loop at 0x2DC090 calls 0x34DA70, advances its pointer by eight bytes, and repeats. This explains the two player passes observed in the trace. It also calls several other routines before and after this loop, so it is a stronger candidate for a broader battle update than the timer routine. Its full semantic scope and replay safety remain unverified.

Near its return, instruction RVA 0x2DC34C increments a global 32-bit value at RVA 0x470D0C4 (next instruction 0x2DC352 + displacement 0x4430D72). This is a candidate invocation counter, not yet a verified simulation-frame counter.

## Next observation

First sample the new counter read-only alongside the existing player timers during idle, pause, and reset. If it correlates, a bounded debugger trace can compare entry 0x2DBC60, player update entry 0x34DA70, and timer writes. Expected idle ratio is one outer entry to two player entries, but that remains a hypothesis until captured. Do not install a rollback hook or assume this function can safely be called repeatedly yet.

## Read-only counter observations

- outer-idle-01.csv: 1,155 accepted samples over 4.999 seconds; outer counter 20703 -> 21003 (+300). At 44.153 ms, P1 timer 2259 -> 1 and P2 timer 2245 -> 1 while outer counter 20705 -> 20706. User was unsure whether they used Training reset, so this is an observed timer decrease, not a confirmed reset test.
- outer-paused-01.csv: user confirmed Training paused. Across 1,155 samples over 4.999 seconds, outer counter stayed 22057, P1 timer stayed 1364, and P2 timer stayed 1355. This supports pause-sensitive update behavior.
- Reads are sequential polling, not an atomic state snapshot. Controlled resume/reset and hitstop behavior remain unverified.

### Resume and user-confirmed reset capture

outer-reset-01.csv contains 6,916 accepted samples over 29.999 seconds. The outer counter advanced 22606 -> 24406 (+1800, approximately 60/second). User clarified afterward that they used Training reset twice after the capture signal.

Two simultaneous player-timer drops were observed, consistent with the two reported reset actions:
- 21.388 seconds: P1 3212 -> 1; P2 3192 -> 1; outer counter 23889 -> 23890.
- 23.989 seconds: P1 157 -> 1; P2 156 -> 1; outer counter 24045 -> 24046.

The outer counter continued across both drops. The two observed drops are consistent with the corrected user report of two resets; exact input timing was not independently recorded. These results support a pause-sensitive outer update counter that continues through observed player resets. They do not establish deterministic replay or complete battle state. Next useful step: correlate bounded outer-function entry counts with this counter, then assess attacks/hitstop separately.


### Outer-function correlation trace

User-supplied outer-smoke-user-01.txt captured 100 events through 407 ms. All 25 complete cycles matched: one entry at RVA 0x2DBC60, two player-update entries at 0x34DA70, then one counter write with next RIP 0x2DC352. In every cycle the counter was unchanged at the three entries and increased by exactly one at the write. Totals: 25 outer entries, 50 player entries, 25 counter writes.

Cleanup reported restored=1, detached=1, failed=0. The post-detach counter advanced 30001 -> 30122 over the two-second check with the game alive. Manual responsiveness confirmation for this capture remains pending.

This establishes the 1:2:1 relationship for this short Training capture. It does not establish behavior during hitstop, full state coverage, or safe repeated invocation. Next: compare during attacks/contact and hitstop before considering an installed observation hook.

### Requested attack capture

outer-attacks-user-01.txt: 100 events from 6 to 407 ms. All 25 complete groups match outer entry, two player entries, then counter increment by one. Counts 25:50:25. Cleanup restored=1, detached=1, failed=0; post-detach counter 34367 -> 34487 (+120), process alive. P1 timer endpoints 10382 -> 10407.

The capture has no explicit contact/hitstop marker. User confirmation of attack contact during the approximately 0.4-second capture and post-capture responsiveness is pending. Do not label this hitstop validation based on the call counts alone.

### Longer read-only contact capture

User confirmed several attacks connected without pausing or resetting. outer-contact-01.csv: 6,907 accepted samples, six samples rejected for P2 timer changes during reading, duration 29.997 seconds. Shared counter 40302 -> 42083 (+1781); P1 timer 16400 -> 18190 (+1790); P2 timer 16299 -> 18082 (+1783). Varied inputs and move IDs were recorded for both players, with no counter decreases.

A scan for unchanged values lasting at least 25 ms found one common plateau of 333.147 ms starting at 336.749 ms. All three counters held together there; this cannot be attributed specifically to hitstop. No player-only plateau meeting that threshold was found while the shared counter advanced. These timer fields therefore do not isolate hitstop in this capture. Their unequal total advances also mean player timers must not be substituted for the shared counter. Sequential polling and unmarked contact timing limit interpretation.

Next research should identify an independent animation-progress or hitstop-state candidate and correlate it with controlled contact. This capture does not justify claiming hitstop support or deterministic replay.

### Animation-progress candidates

Static inspection of RVA 0x2FFEB0 shows a float at timer-object +8 copied to +12, advanced by a computed amount, and clamped against +16. The amount includes the float at +0x30. These are progress/previous/end/rate candidates, not verified named game fields. Objects: P1 RVA 0x4759BE0; P2 RVA 0x47F0FD0. The timer increment at +0x458 is separate and can occur even with no float progress. The routine also has a loop that can increment that timer more than once per invocation.

observe_training.py --animation now reads these floats without writing game memory. animation-contact-01.csv captured 6,865 samples over 29.997 seconds. Shared counter +1799. P1 progress range about 0.775..111; P2 1..375. No stable-progress/same-move interval >=25ms spanning >=2 outer increments was found. Recorded P1 inputs were neutral/directions only, with no attack-button bits; P2 inputs were neutral. This run does not provide evidence of attack contact. User contact confirmation is pending. It should be treated as a movement/idle baseline until clarified, not hitstop validation.

### Repeated 60-second animation capture

animation-contact-02.csv: 13,677 accepted samples over 59.996 seconds, 22 rejected reads. Shared counter +3599. Attack-button inputs and many move changes were recorded.

Both players' progress candidates stayed at 2.0 with unchanged move IDs simultaneously for three observed intervals while the outer counter advanced four counts each:
- 17.654 seconds: 75.46 ms, P1 move 487 / P2 move 10041.
- 20.067 seconds: 79.26 ms, P1 move 497 / P2 move 10048.
- 23.667 seconds: 82.79 ms, P1 move 491 / P2 move 10045.

This is evidence of shared animation-progress holds while the outer update continues. It is consistent with a contact-related freeze, but does not distinguish hitstop from scripted move behavior or prove these fields describe the complete animation state. Four additional P1-only holds of about 30-32 ms were observed. A controlled comparison of the same move whiffing and connecting is needed to isolate contact-specific behavior.

### Intended standing-A miss baseline

animation-standing-a-miss-01.csv: 6,856 samples over 29.999 seconds; outer counter +1800. P1 inputs neutral, A, and direction 1024. P1 moves 89/264/265/266/65535; P2 moves 8281/8582/65535. No simultaneous progress hold meeting the >=25ms and >=2 outer-increment criteria; one P1-only 30.72ms hold at progress 1.0 (move 264). Contact-free status is pending user confirmation because the opponent's move IDs also changed. Multiple player move IDs may reflect an attack string; their semantics are not verified. For the comparison, request single A presses separated by a full return to idle.

### Repeated isolated-A miss baseline

User reported approximately three hits in miss-01; exclude that run as a contact-free baseline.

animation-standing-a-miss-02.csv: 6,858 accepted samples over 29.996 seconds, outer counter +1800. P1 inputs only neutral/A; P1 move IDs only 264/65535; P2 input neutral and move ID 65535 throughout. This is consistent with isolated A attempts and an idle opponent, with no recorded opponent move transition. User confirmation that every attack missed remains pending. One P1-only progress hold of 30.09ms (two outer increments) at progress 1.0, move 264; no P2 holds or simultaneous holds meeting the >=25ms/>=2 increment threshold. Next capture should repeat the same single A attack at contact range, waiting for idle between presses.

### Single-A contact comparison

User confirmed every attack missed in animation-standing-a-miss-02.csv and moved into range for the contact run.

animation-standing-a-hit-01.csv: 6,860 accepted samples over 29.999 seconds, outer counter +1800. P1 inputs neutral/A/direction 2048; moves 89/264/65535. P2 inputs neutral; moves 8281/8283/65535, consistent with opponent reactions. User subsequently confirmed that every single A connected. Neither player's progress field showed a same-move hold >=25ms spanning >=2 outer increments. The confirmed miss baseline had one P1-only 30.09ms hold and no simultaneous hold.

This controlled comparison does not support using these progress holds as a general hitstop detector for standing A. The earlier simultaneous 75-83ms holds appeared with other moves and may be move-specific behavior. Shorter freezes, another animation layer, or other state fields remain possible; no verified hitstop flag has been identified. Avoid repeatedly collecting the same probe expecting a different interpretation. Next useful work is static inspection of the progress-step helper at RVA 0x30A8C0 and the gating branch near 0x2FFFA4 to select an independent candidate.


### Yoshimitsu bA intended miss capture

animation-yoshi-ba-miss-01.csv: 6,859 accepted samples over 29.997 seconds; shared counter +1800. P1 inputs neutral/A/B/direction 2048; P1 move IDs 89/298/302/303/65535. P2 input neutral and move ID 65535 throughout. The move-ID sequence is not independently mapped to bA; user was instructed to repeat only bA out of range. One P1-only progress hold at 1.0 for 29.60ms (move 303, two outer increments); no P2 or simultaneous qualifying holds. All-miss confirmation remains pending. Next compare the same attempted move at contact range.

### Yoshimitsu bA intended contact capture

User confirmed all attacks missed in animation-yoshi-ba-miss-01.csv before moving into range.

animation-yoshi-ba-hit-01.csv: 6,853 accepted samples over 29.998 seconds, shared counter +1800. P1 move IDs 89/298/302/303/304/305/65535; P2 89/8487/10054/65535. P2 input includes guard bit 8, so this run cannot be assumed to contain only unblocked hits. Neither progress field showed a same-move hold >=25ms spanning >=2 outer increments. The previous simultaneous 75-83ms freezes were not reproduced by this attempted bA comparison. User subsequently confirmed that all bA attempts landed. Keep the sampled guard bit as a separate observation; it does not establish that an attack was blocked. These results concern the sampled fields and threshold, not an assertion that bA has no hitstop.


### Yoshimitsu A+G isolated capture

User corrected proposed throw inputs to A+G and 4A+G, not B+G. This run requested only A+G.

animation-yoshi-ag-hit-01.csv: 6,856 accepted samples over 30 seconds, outer counter +1800. P1 inputs neutral/A/G/A+G; P1 move IDs 476/487/65535; P2 89/8481/10041/65535.

Eleven simultaneous progress holds were observed with P1 move 487 and P2 move 10041, both progress fields at 2.0. Observed durations 78.16–82.71ms; outer counter advanced four counts in ten intervals and five in one (sequential polling cannot assign exact frame boundaries). This reproduces the same 487/10041 pair seen in the first mixed-move capture. A+G is now a reproducible context for these holds, not proof of a general hitstop field. The holds may be scripted throw synchronization. Two additional P1-only ~30ms holds occurred on move 476. User subsequently confirmed that all A+G throws landed. Next compare isolated 4A+G.


### Yoshimitsu 4A+G isolated capture

animation-yoshi-4ag-hit-01.csv: 6,861 accepted samples over 30 seconds; shared counter +1800. P1 moves 478/491/65535; P2 89/8481/10045/65535. Nine simultaneous progress holds occurred at progress 2.0 with P1 move 491 / P2 move 10045. Eight complete observed holds lasted 77.78–79.68ms; the final hold lasted at least 69.80ms and was truncated by capture end. Each spanned four observed outer-counter increments. This reproduces another pair from the original mixed-move capture and supports a throw-associated progress hold. It does not prove general hitstop semantics. User subsequently confirmed that all 4A+G throws landed.

Both requested throws now reproduce shared progress holds while outer updates continue. Additional repetitions of these same captures are unnecessary for this finding. Next useful work is inspecting the progress-step helper and throw-related gating to explain the hold, while retaining the outer counter as the stronger simulation-update candidate.

