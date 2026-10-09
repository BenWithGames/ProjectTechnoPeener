# Animation-step helper inspection

Examined the hash-verified SC6 executable on disk, 2026-10-07. No debugger attachment or game-memory writes.

## Findings

Helper RVA 0x30A8C0 spans through RET at 0x30A976. It is a leaf routine without a matching PE unwind entry; all branch paths were disassembled, not just the first return.

Its input RCX is a player-state object, distinct from the animation/timer object. It reads the player index byte at +0x23C. Depending on conditions, it returns the float at player +0x3500, zero, a product involving player +0x3510, or a product of a global multiplier and player +0x3500.

Important branches and candidate fields:

| Evidence | Candidate meaning (unverified) |
| --- | --- |
| Global RVA 0x4846458 is signed; nonnegative values select the index allowed to receive player +0x3500, other indices receive zero | Player-specific update selection |
| Global base RVA 0x48462D0, opposite-index dword at +0x18C equals 3 | Override choosing player +0x3500 |
| Player word +0x19EC compared with 2, plus linked-player pointer +0x973E8 | Paired-player state condition |
| Player float +0x3510 is nonnegative | Alternate scaling branch |
| Opposite-index float at global base +8 limits +0x3510 | Paired-player scale limit |
| Global byte 0x48462D0 nonzero in that branch | Zero returned instead of scaled progress |
| Global float 0x48462D4 | Multiplier when that byte is zero |
| Global float 0x48462F4 | Multiplier for the negative +0x3510 branch |

In the timer routine (RVA 0x2FFEB0), the helper result is multiplied by animation-object float +0x30 and added to progress at +8. A separate branch near 0x2FFFA4 can also force the increment to zero when player index is nonzero, player byte +0x16E6 is nonzero, and its global condition is zero. The increment at animation-object +0x458 remains separate. Thus animation progression and update counting can diverge by design.

The saved A+G and 4A+G captures show animation-object +0x30 equals 1.0 for both players throughout samples with progress 2.0 and the corresponding throw move IDs. That sampled multiplier dropping to zero does not explain these holds. Previous-progress values include 2.0 during the holds, consistent with the progress state being updated without advancing, but polling cannot prove which helper branch ran. The routine may also be skipped or other writers may participate; the exact cause remains unverified.

## Next targeted measurement

Read the actual player pointers from the previously traced pointer table at module RVAs 0x470F1A0 and 0x470F1A8, validating every read. Do not confuse these player objects with the timer objects at 0x4759BE0 and 0x47F0FD0. Sample the helper's inputs above alongside progress during one known A+G hold. Include counter values before and after the reads and reject transitions where possible; this still does not create an atomic snapshot.

Compare the helper conditions before/during/after each hold, then select a specific branch for bounded execution tracing if needed. Do not patch these fields, label them as verified hitstop flags, or infer deterministic replay support from this analysis.

## Helper-input A+G capture

step-ag-01.csv: 6,743 samples over 29.999 seconds, shared counter +1800. 107 rows rejected on counter/pointer consistency checks (legacy summary key rejected_timer_changed includes these rejections). Read-only player pointers resolved and indices were 0 and 1. Ten simultaneous progress-2.0 intervals on moves 487/10041 lasted about 75–80ms.

Of 186 samples in those intervals, 149 had global float RVA 0x48462F4 equal to 0.0; the other 37 had 0.5, consistent with transition timing rather than exact freeze boundaries. During the zero-global samples, player +0x3500 was 1.0 for both. Player +0x3510 was -1.0 for both in 148 samples; in one P2 was 0.0. Selector was -1; opposing mode values were 0; paired state words were 0. The decoded helper therefore follows the negative-override branch and returns global 0.0 times base step 1.0 in the 148 common samples. In the remaining sample P2's nonnegative-override branch also produces zero. The animation-object multipliers remained 1.0.

Outside these progress-2.0 intervals but within the same throw move, the global multiplier was 0.5 or 1.0. This gives a concrete code/data explanation consistent with the observed holds: the helper's global multiplier suppresses progress while the outer update continues. It is not proof of the exact live instruction path, a general hitstop flag, or of the writer's semantics. Next inspect writers of RVA 0x48462F4 to determine how throw state sets and clears it. No further repeated throw captures are needed before that inspection.

## Multiplier writers traced

Static inspection found a direct initialization write at RVA 0x2FB562. More relevant runtime writes use the containing structure pointer (base RVA 0x48462D0), which a direct RIP-relative search alone misses.

- Helper 0x31D750 writes structure +0x24 at instruction 0x31D7BB.
- Helper 0x31D800 writes structure +0x24 at instruction 0x31D8AD.
- Both select the minimum of the default float constant and structure floats +8, +0xC, +0x34, multiply by structure +4 (or zero if the byte at +0 is set), and store the result in +0x20, +0x24, and +0x28. +0x24 resolves to the observed multiplier RVA 0x48462F4.
- 0x31D800 additionally decrements positive signed counters at +0x10 and +0x14. On expiry, each corresponding scale (+8 or +0xC) is restored to 1.0, its counter becomes -1, and its source tag (+0x18 or +0x1C) is cleared. The chosen source tag is written to +0x30; the +0x34 scale uses tag 3.
- The outer update 0x2DBC60 calls 0x34D500 at 0x2DC0C8, after the player-update loop. That routine passes the structure base to 0x31D800 at 0x34D665. A separate call at 0x2DB407 invokes 0x31D750 with the same base.

This identifies runtime recomputation and timed clearing of the multiplier. In the saved throw holds, side1_limit (structure +0xC) was zero alongside the zero output, while +8 was 1.0 and gate byte was zero. This is consistent with a timed per-side scale request feeding a shared minimum. The exact request-setting path and countdown values during throws have not yet been captured, so a specific throw handler and exact hold length are not proven.

Search candidates were initially generated from byte positions and included overlapping decodes; only instructions re-disassembled from validated function starts or explicitly inspected leaf routine boundaries are used above. The search is not an exhaustive inventory of indirect or bulk writes.

Next: inspect the setters of structure +8/+0xC and +0x10/+0x14, or sample those counters and source tags during a known throw to correlate expiry. These fields are candidates for rollback state coverage; they are not safe to omit merely because visible animation is frozen.

## Countdown request path and expanded validation plan

Static inspection located the pending-request transfer in 0x34D500's chained range. For each player, a nonnegative float at +0x3510 triggers copying:
- player +0x3510 -> shared scale +8 + 4*index;
- player signed +0x3514 -> shared countdown +0x10 + 4*index;
- player +0x3518 -> shared source tag +0x18 + 4*index.

Instructions 0x34D6A5, 0x34D6AF, and 0x34D6B5 perform these stores, then 0x34D6B9 calls the multiplier recomputation helper. The pending request is consumed by resetting player fields to -1.0/-1/0. Existing countdowns are updated earlier through 0x31D800. This distinguishes queued requests from active countdowns and explains why periodic external reads can miss requests.

In the function range 0x376B20..0x37C0AC, instructions 0x379403/0x37940F/0x379419 populate those pending fields from a record addressed by RSI: the 16-bit value at +2 passes through helper 0x2FC560 to produce the float; signed words at +4 and +6 supply duration and tag. Nearby variants at 0x37942D and 0x37946F set related requests, including a -1 duration variant. This resembles data-driven move-command processing, but the command identity, record format, and actual throw execution path remain unverified. Stack-offset matches elsewhere in the scan were excluded as unrelated. The scan is not an exhaustive inventory of indirect writes.

An additional paired routine observation: 0x31DC00 and 0x31DE50 pass the first 0x58 bytes of this shared structure to virtual methods at slots +0x28/+0x30. This resembles serialization in opposite directions but is not yet verified as usable snapshot/restore functionality.

For separate Soul Attack, Soul Charge, and Critical Edge captures, record active per-side countdowns/tags, pending player duration/tag, the third scale at shared +0x34, selected source tag +0x30, the selector, multiplier, animation progress, and outer counter. Test one mechanic at a time and explicitly confirm activation/contact. Do not assume cinematic freezes use the throw path. Read-only polling can correlate active states but cannot reliably capture every transient request; bounded instruction tracing may be needed to establish the exact setter.

## Confirmed Soul Charge capture

step-soul-charge-01.csv: user confirmed Soul Charge activated without attacks/resets. 6,716 accepted samples over 29.998 seconds; outer counter +1799. 173 samples rejected by consistency checks.

At about 14.201 seconds, selector changed from -1 to 0. At 14.219 seconds, side0 scale and output multiplier became zero, with source tag 1; both sampled countdowns remained -1. At 16.203 seconds the selector, scales, and source returned to their initial values. The observed zero-output interval lasted about 1.979 seconds and spanned 118 outer-counter increments.

During that interval P1 progress had 111 distinct values, while P2 progress stayed at 56.0. Both player base steps were 1.0; side0 mode was 2, side1 mode 0. This matches the helper's player-selection branch: with selector 0, P1 receives its base step and P2 receives zero. A zero shared multiplier therefore does NOT imply both players freeze. This is a stronger distinction than the throw captures.

No positive countdown or transient pending request was sampled; this capture does not establish the setter instruction or an expiry-driven release. It demonstrates a selector-based Soul Charge state alongside shared scaling, ending via an untraced release path. Soul Attack and Critical Edge still need separate captures. Do not generalize Soul Charge behavior to those mechanics.

## Confirmed Soul Attack capture

step-soul-attack-01.csv: user confirmed Soul Attack activated and hit. 6,710 accepted samples over 29.996 seconds, outer counter +1800, 200 rows rejected on consistency checks.

Selector changed -1 -> 0 near 15.040 seconds; side0 scale/output multiplier became zero with source 1 at 15.044 seconds. Initial values returned at 15.777 seconds. Across the sampled zero-output interval (15.044–15.772 seconds, about 728ms), the outer counter advanced 43 counts. P1 progress had 42 distinct values, while P2 progress remained 354.0. Both base steps stayed 1.0. Both active countdowns remained -1 throughout the capture. A pending P1 zero-scale request was sampled, but its exact setter instruction was not observed.

This matches the same selector-based pattern found in Soul Charge for this run, with a shorter observed interval. It does not prove all phases or characters use identical logic. The frozen interval precedes the later opponent reaction in the trace; user-confirmed eventual hit does not identify the precise contact timestamp. Critical Edge remains to be measured separately.

## Confirmed Critical Edge capture

step-critical-edge-01.csv: user confirmed activation, hit, and completed cinematic. 8,950 accepted samples over 39.998 seconds, outer counter +2400, 166 rows rejected on consistency checks. P1 moves 655/656/657/659/65535; P2 89/8503/10058/65535.

At 14.305 seconds, selector became 0, side0 scale/output multiplier zero, and source tag 1. At 15.806 seconds these returned to initial values. Within the sampled zero-output interval (14.305–15.802 seconds), the outer counter advanced 89 counts; P1 progress had 88 distinct values, while P2 stayed at 230.0. Both base steps remained 1.0. Both active countdowns remained -1 throughout. The same-move P2 hold spans about 1.510 seconds/90 outer increments including boundary samples; no qualifying P1 hold was found.

This reproduces the selector-based startup freeze seen in Soul Charge and Soul Attack. It does not imply the whole cinematic is frozen or fully explained by this mechanism. No further sampled scale/selector transitions occurred later in the completed cinematic. Camera, effects, animation layers, and complete battle-state coverage remain unverified.

Comparison for this Yoshimitsu session: throws reproduce simultaneous progress holds (~80ms), while Soul Charge (~1.98s), Soul Attack (~0.73s), and Critical Edge (~1.50s) exhibit selector-0 intervals with P1 progressing and P2 held. These are observed intervals, not universal move frame data. All retain outer-update progression. Next implementation work should retain these distinctions in state-capture requirements instead of using a single freeze flag or animation progress as the frame clock.

## Reversal Edge captures

step-reversal-edge-01.csv is excluded from the mechanic comparison: user reported the wrong move and requested a retry. Player timer decreases also occurred in that run.

step-reversal-edge-02.csv: user confirmed Reversal Edge connected and the clash finished. 8,933 samples over 39.998 seconds, outer counter +2136, 196 rejected reads. Capture began with P1 move 662 already active; it may omit activation onset. Do not infer which player initiated solely from input polling (P1 input neutral throughout; P2 neutral/B).

During the first eight seconds the shared output multiplier alternated between normal 1.0 and approximately 0.05, 0.06, and 0.10, with selector -1 throughout and source tag 1 during slowdown. Both side scales contributed at different times. Positive side0 countdown values 20 and 10 were sampled, but intermediate countdown values were not; this does not establish a continuously observed decrement sequence. Pending scale values were also observed. This differs from the selector-0 freeze pattern of Soul Charge/Attack/Critical Edge and the zero-multiplier throw holds.

At 18.461 seconds the outer counter and both sampled progress fields held for 4.416 seconds; multiplier was 1.0 and countdown -1. This later hold accounts for approximately the missing wall-clock updates. Its cause is unconfirmed: do not attribute it to Reversal Edge or assume the outer function continued. Ask whether Training was paused or focus changed then. The earlier slowdown evidence remains useful independently of that later hold.

### Reversal Edge repeat (03)

User denied pausing or switching windows in run 02 and requested a repeat; the previous 4.416-second hold remains unexplained, not attributed to user action.

step-reversal-edge-03.csv: 8,935 accepted samples over 39.996 seconds; outer counter +2400. No outer-counter plateau >=25ms was detected. The earlier long stop did not recur. Capture begins with moves 187/193 and scale already about 0.05, so activation onset was not recorded. User subsequently confirmed that the repeated Reversal Edge connected. Subsequent move and scale changes were recorded; clash completion was not separately reconfirmed in that reply.

Observed multiplier values approximately 0.05/0.10/0.15/0.20/1.0, selector -1 throughout, side0 source 1 during slowdown. Side0 countdown values 20 and 10 appeared, along with a pending duration 10 sample, but not a full decrement sequence. Compared with run 02, different move IDs/outcome phases and scale values occurred. This supports phase-dependent scaling without a player-selector override in these runs, not fixed universal Reversal Edge speeds. No game-memory writes or debugger attachment were used.

