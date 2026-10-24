# Round-start reservation acknowledgment recovery

## Paired evidence

Match `9fd3b146ecb56746db2148dcbf3fb83f`, local PID 84252 and peer PID 2688,
entered round 5 / event 10284 at the same reserved battle frame 10404.
Both retained a native reservation. Local transition preparation was agreed;
peer preparation was not (6 received, 641 rejected). Local start exchange
imported the reservation; peer exchange did not. Both exchanged accepted held
messages, but no round-5 release was published/taken on either report. Local
ended with peer_disconnected; the peer report was recovered after exit.
The prior report lacks preparation rejection reasons, so the cause of the
missing early ACK cannot be narrowed to packet loss versus rejection.

## Protocol defect and repair

A reserved leader does not send the timed fallback proposal. A follower that
missed the early reservation ACK could remain in that fallback indefinitely,
even after receiving an exact authenticated held-frame acknowledgment.

The start exchange now recognizes that stronger evidence: kind 3 acknowledges
both current tickets and states that the peer reached its physical hold. It
can recover the reserved path only when the local owner installed a fresh,
nonfailed exact reservation for the same offer. Native preparation still binds
the physical local hold and validates Fight, frame, generation and ownership.
Publishing still requires the existing bilateral activation-ready agreement.

Recovery validates Steam identity, session/build, round/event, first frame,
complementary slots, delay range, rollback limit, record count, both tickets,
packet timing, reservation freshness and any previously observed peer offer.
Intent-only or conflicting preparations cannot recover. The ordinary timed
fallback remains for sessions without reservations. Existing timed failures
remain failed; no snapshot, retirement or round-transition guards are relaxed.
No new input delay or combat wait is added. Both peers must update (signature
bump) so an old peer cannot remain in the broken fallback.

## Diagnostics

All existing diagnostics remain. New fields:

- start_sync_round_<event>_held_reservation_recovered
- start_sync_round_<event>_held_reservation_rejected
- round_preparation_<event>_last_rejection_stage:
  1 peer identity; 2 expired/backward-time/failed preparation; 3 packet kind;
  4 envelope/timing; 5 incompatible offer; 6 changed established peer offer;
  7 acknowledgment ticket mismatch.

## Validation

The protocol regression recreates a missing transition ACK on either side,
no cached clock samples, delayed local hold, 90 ms RTT, duplicate packets and
loss of initial held packets, for all adaptive delay proposals 0 through 2.
It also exercises both ACKs missing. Removing the new recovery reproduces the
failure at `assert(lt && rt)`; enabling it passes. Negative checks cover intent
only, expiry, backward time, changed tickets, session, build, frame, round,
slots, delay, prediction limit, record count and failed preparation.
Existing timed-fallback, deadline, packet-loss, offset and native reservation
regressions remain enabled. A live round transition is still required to verify
the fix in SC6; local tests cannot certify native network scheduling behavior.
