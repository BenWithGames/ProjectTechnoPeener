# Paired confirmation-delay audit: 9fd3b146ecb56746db2148dcbf3fb83f

Reports: local PID 84252, peer PID 2688. Both provide local-QPC event and outer
update CSVs at 10 MHz. The peer summary was recovered incompletely, so this
comparison uses per-round event receipts and corrections, not missing aggregate
counters or current round-5 zero counters.

| Round | Local corrections | Peer corrections | Local receive-to-drain ms | Peer receive-to-drain ms |
|---|---:|---:|---:|---:|
| 1 | 162 | 142 | 10.116 | 3.446 |
| 2 | 155 | 140 | 9.875 | 6.741 |
| 3 | 241 | 63 | 7.211 | 1.150 |
| 4 | 222 | 173 | 9.437 | 6.249 |

Local correction depths: 622 one-frame, 158 two-frame. Peer: 507 one-frame,
11 two-frame. Round 3 contributes 178 of the 262 extra local corrections (68%).
Most receipts land between outer callbacks (local 7816, peer 7849); none of the
measured receipts remained undrained beyond the next completed live outer
update in the recorded trace. This conservative test does not claim every
intermediate native instruction is an eligible admission boundary.

First-send publication-to-return aggregate: local about 0.072 ms, peer about
0.166 ms. Both fast receivers report no rejected packets, queue-full events or
faults. The already-present post-send receive pass caught 259 local / 54 peer
packets. No evidence here supports a receive-queue backlog or a missed whole
combat-update admission opportunity.

Matched-frame relative phase is mostly stable within each round. Round 1
shifts about 4.7 ms after its initial window; round 3 changes only fractions of
a millisecond. These are phase CHANGES, not absolute initial lead or network
one-way latency. Arrival phase versus consumption differs between sides;
without full raw-input histories and complete peer timing aggregates, these
reports cannot separate all effects of transport phase and input patterns.

Existing Ghidra evidence in CONFIRMATION_ADMISSION_AUDIT.md establishes
sequential player input formation and pressed-edge dependencies. The current
code already drains before sampling/formation, before execution and after the
completed revision is acknowledged. Mutating between consumers or from the
transport worker is not a safe optimization. A shorter transport poll cannot
be credited with removing a 7–10 ms interval measured AFTER packet receipt.

No runtime timing, adaptive delay, prediction policy, native hook, safety guard,
or installed DLL was changed for this audit. No supported latency-neutral
runtime fix was identified in this pair. Do not claim a reduction in corrections
from the analysis-tool improvement. The round-start reservation recovery build
remains a separate change, previously blocked from installation by SC6 PID85332.

Compare-ConfirmationTiming.py now includes event-based per-round correction
depths and receipt/outer-update attribution, with missing timestamps separated
from invalid timestamps and missing capture coverage. Four Python regression
tests pass. Output for this pair: build-native/confirmation-comparison-9fd3.json.
