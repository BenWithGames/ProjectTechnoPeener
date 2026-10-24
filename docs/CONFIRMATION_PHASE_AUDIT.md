# Confirmation timing and startup release — 2026-10-22

Matched reports: match ID `81d09084f97ccc22eba181702f57d587`, local PID 46124,
peer PID 22452. Both passed five rounds, 12,116 live updates each. Per-round
continuous counters were summed; cumulative startup counters were not summed.

## Evidence

The peer predicted 11,598 updates and corrected 895 times (690 depth one,
204 depth two, one depth three). Local predicted 855 updates and corrected 99
times. Both selected two input-delay frames in every round.

Peer restoration averaged 3.932 ms, maximum 8.080 ms. Active outer updates
averaged 1.253 ms; only one exceeded 16.667 ms. Most rounds advanced near 60 Hz.
Round three contained prediction-budget admission waits and 50–106 ms gaps,
while the CPU time measured inside those gaps was much shorter. There were no
fast-receiver faults, queue-full events, or rejected fast inputs. These data
do not identify a broken CPU restoration path.

Peer same-frame publication-to-receipt means were 43.745, 43.587, 43.378,
41.574, and 57.178 ms. Local means were 23.346, 23.698, 29.613, 25.838, and
10.144 ms. These are **not one-way network latency**: each compares that PC's
publication of frame N to its receipt of the other PC's frame N. They include
relative simulation timing. Persistent simulation lead is consistent with
the asymmetry, but the old logs lack paired activation timestamps to prove
startup caused the entire difference. Network asymmetry and later scheduling
variation remain possible contributors. Receive-to-drain phase also costs
several milliseconds; future-frame waiting is not automatically avoidable work.

## Ghidra check

Fresh read-only audit: `work/ghidra-sc6-focus/confirmation-skew-audit`, project
SC6_AnimationFocus, executable SHA-256
F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553.
Functions/RVAs: 003FDF30 fresh sample, 003FDFA0 native wait, 003F5D20 dispatch,
003E2000 advance, 003FE520 consume, 003E9EE0 pause. Research names are annotations.

Dispatch services the clock, tests waiting, then advances through vtable
slots 0x620/0x628/0x630. Fresh sample fills 0x3B8/0x3BC according to the local
slot mask. Native reason 3 uses local cursor/lead/frame arithmetic; it is not
evidence of synchronization for our separate startup barrier. Preserve the
native service, pacing, and single-update reservation checks.

## Change and limits

Previously each PC released upon receipt of the other's held notification;
the protocol guaranteed the same frame but no common wall-clock release.
The revised startup protocol measures clock offset using four timestamps,
subtracts peer handling from RTT, selects the lowest-RTT observation after
three distinct samples, and agrees on a future release time. A deterministic
coordinator proposes, the follower acknowledges, then a reliable commit
authorizes release. The owner thread retains the native hold until its local
deadline. Tickets, round identity, build, frame, slot, and bilateral hold
checks remain mandatory. Late commitment cannot authorize activation.

The schedule has at least 150 ms lead (or twice measured RTT plus 50 ms).
This is startup/round-boundary time, **not an increase in input delay**.
Adaptive 0–2-frame delay and the three-frame prediction limit are unchanged.
Raw inputs still send immediately; transport threads do not mutate simulation.
Protocol compatibility changes require both peers to install this build.

Offset estimation assumes roughly symmetric paths: one-way asymmetry leaves
residual error, and frame polling can add up to a callback interval. This is
a targeted correction of an unsynchronized release, not proof that all jitter
is fixed. No render smoothing, extra combat waits, frame renumbering, or
verification relaxation was added. New per-round RTT/offset/target/actual
release/commit/failure diagnostics make the hypothesis testable in live logs.

Regression coverage includes different clock epochs, either coordinator,
1/33/70 ms one-way paths, preparation skew, dropped preliminary packets,
asymmetric paths, duplicate echoes, malformed timing, target changes, late
acknowledgements/commit, native mailbox ownership, and stale-round traffic.
