# Recovery from late-confirmation admission misses

Evidence: paired match 0bde3c8ec11f1c3aa0872c6b3bb7648c, local 70140 and
peer 5604. Both completed five rounds. The local PC completed 575 corrections
and replayed 650 frames; the peer completed 597 and replayed 1431. Delay was
1, 2, 1, 1, 1 on both peers. This is correction-depth imbalance, not just counts.

Same-frame timing isolates several persistent phase changes from transient
correction CPU cost. At local round 3/frame 264 and round 4/frame 280, a
prediction-budget admission miss precedes a 55.905/55.121 ms interval between
live completions. Local CPU work for the resumed outer update was only
6.729/5.946 ms. The peer continued advancing during the missing opportunity.
Its relative lead then persisted, making local truth older at peer consumption.
The longer correction work does not itself explain the persistent lead.

Times below are milliseconds relative to the preceding local live completion:

| Round/frame | Post-grace rejection | Required truth received | Next drain | Next live completion |
|---|---:|---:|---:|---:|
| 1/1067 | 17.774 | 22.654 | 32.324 | 55.112 |
| 3/264 | 18.334 | 20.491 | 32.404 | 55.905 |
| 4/280 | 17.818 | 18.870 | 32.352 | 55.121 |
| 5/137 | 18.293 | 18.866 | 32.507 | 33.310 |

The per-round timeout durations imply roughly 6.94, 4.64, 3.07 and 3.31 ms
from wait entry to required receipt. These are retrospective estimates; the
old journal logged admission after waiting, not exact wait entry. The packet
was already enqueued at these receipt times. The evidence cannot distinguish
internet jitter, sender scheduling or transport batching as its upstream cause.

Ghidra re-run, read-only: SC6PendingCombatAudit.java and
SC6ClockCombatSequenceAudit.java; outputs timing-imbalance-pause-audit and
timing-imbalance-sequence-audit. Native dispatcher 3F5D20 services, waits, then
advances. 3E2000 advances one clock frame. 3FE520 computes the delta against
outer+148C and consumes it later; 3FBF30 invokes that combat loop separately.
3E9EE0 also changes native pause state and notifies its owner; it is not merely
a boolean check. These paths support preserving the split clock/battle contract,
not invoking battle twice, forging inputs, or incrementing clock counters to
catch up. Ghidra alone does not identify the exact engine scheduling decision
that caused the extra outer-call gap in each instance.

Change: extend the existing event-driven prediction-limit grace from 2 ms to
8 ms, with a 9 ms OS wait backstop. This covers the observed late-arrival tail
without adding a full-frame delay. It is attempted once per clock identity/frame,
only after an eligible update is blocked specifically by the three-frame budget.
Pending receipts skip sleep; a notification wakes it early. Admission and native
identity/frame checks are rerun afterward. No receipt still means no advance.
OS descheduling may exceed the nominal budget and is reported. This can spend
up to 6 ms more at an otherwise blocked update; it is not a reduction of CPU
restoration cost or proof that all future stalls/phase drift are eliminated.

Diagnostics: grace schema 2 reports budget and backstop. Journal kind 10 is
grace begin, kind 11 grace end. Both record head and confirmed frontier, detail
is admission reason, aux0 is callback success (1 at begin), aux1 settled frontier.
This lets future analysis align exact grace attempts with receipt timestamps,
subsequent admission, native clock advancement and outer combat completion.
All older diagnostic fields/events retain their meanings.

Focused tests cover recovery, expiration, one attempt per frame, callback failure,
handoff/native-ineligible exclusion, balanced grace events and failure evidence,
pending receipt/race notification, and bounded timer termination. The full native
suite remains required. Live validation must compare phase drift and depth, plus
outer-update spikes and total wait time; this change is a targeted mitigation of
the demonstrated deadline misses, not a general frame-pacing controller.

Adaptive delay 0--2, prediction max 3, low-latency input sampling, fast redundancy,
snapshot/ownership optimizations, exact verification and all round guards remain.

Superseded in the nonblocking recovery experiment: the live grace callback is
not attached. See MISSED_UPDATE_RECOVERY.md, including counter-evidence showing
why the outer-entry retry alone does not cover the historical millisecond-late
arrivals. The current experiment must not be called a proven imbalance fix.
