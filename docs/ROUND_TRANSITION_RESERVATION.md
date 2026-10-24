# Next-round boundary reservations

The next round can negotiate its exact first fighting frame while native Start
updates continue. This replaces the post-Fight timed handshake only when an
authenticated reservation is acknowledged. The first-match extension is documented
in FIRST_ROUND_INTRO_RESERVATION.md.

## Native evidence

The Ghidra audit of the pinned executable covers RVA 2D9CD0 (mode update and
counter increment), 384040 (Start update), 3840E0 (Start transition), 3835B0
(durations 3/120/156), and 301460 / 384DC0 (Normal entry). The three read-only
captured transitions reached Normal at battle frames 207, 1992 and 5950 after
120 Start updates. External sampling can see a partially completed native
update; it is not used for reservation.

On the clock owner thread, two distinct countdown updates must project the
same battle boundary. At least 30 countdown updates must remain.
The exact round, event, generation and clock identity are retained. RVA 3FE520
can consume multiple battle updates, so a projected clock-frame number is
diagnostic only: the actual clock frontier is freshly bound at activation. A short
countdown or an unavailable stable projection uses the existing protocol.

## Agreement and ownership

New packet kinds 9/10 describe a future reservation, not an already-held clock.
Both offers must match the first frame and round, authenticate the peer and
session, and agree on complementary sides. Delay remains the larger frozen
proposal from the two peers, within 0–2; prediction remains capped at 3.
Only an acknowledgment echoing the local ticket authorizes importing the
peer's reservation. An offer alone cannot activate anything.

The owner continues Start updates and must stop at its promised frontier.
Every waiting return from the rearm provider checks that it has not reached
or crossed that frontier. A changed advertised projection, expired reservation,
wrong identity, or inability to prepare at the reserved boundary fails closed.
It cannot silently continue native play past an advertised boundary.
The existing battle-entry hook checks the reservation as well as the clock
hook, so a multi-step native outer batch cannot cross it between clock polls.
An unexpected crossing retains the caller chain and records transition failure
11 / rearm failure 19; it does not skip a battle step and run its delegates.

At Fight, fresh resources and the exact battle/clock boundary are validated.
The session is prepared and a physical local hold is bound. Only then may
the acknowledged peer reservation satisfy remote readiness. It does not fake
the physical peer-held flag. Existing activation, playback/result, snapshot,
ownership, settlement and exact replay verification checks remain in place.

The reservation synchronizes simulation frame identity; it is not proof that
both CPUs reach that frame at the same wall-clock instant. Native transition
progress continues unchanged. A late peer or delayed acknowledgment can still
cause a bounded wait or prediction-limit stall. This build must be evaluated
for both pause duration and correction imbalance, not just successful activation.

## Validation and reports

Protocol tests simulate three rounds with a 90 ms RTT, independent clock
origins, duplicate/reordered packets and dropped control packets. In that model,
completed reservations remove the next-round agreement hold (0 us versus
230,000 us for the prior transition-intent path). These are synthetic results,
not live game timing guarantees.

Regression coverage includes short countdown fallback, duplicate observations,
changed countdowns, overflow, wrong frame/event/generation/clock identity,
unauthenticated packets, wrong tickets, expired reservations, mismatched delay,
and rejection of activation without a physical local hold.

Reports retain the old timings and add:

* `continuous_transition_reserved`, `continuous_transition_arrived`
* `continuous_transition_target_battle`, `continuous_transition_projected_clock`
* `continuous_transition_seen_battle`, `continuous_transition_seen_clock`
* `continuous_transition_elapsed`, `continuous_transition_duration`, readiness
* `round_preparation_<event>_reserved` and `_first_frame`
* `start_sync_round_<event>_transition_reserved`

Both peers require the new compatibility signature. Automatic play, adaptive
delay, immediate input sending, diagnostics, zipped reports and retention remain.
Live validation is pending; no zero-pause guarantee is made.
