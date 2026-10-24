# Peer freshness during round rearming

## Evidence: match 2596edd4284f74ff91d5e560edf533bd

Both peers used the first-intro reservation; four rounds ran. Friend reported rearm peer read/reason 3 (stale), tick 5477671, now 5478218: 547 ms against the unchanged 250 ms limit. Last fully inspected intro sample was battle 8718 / clock 86; target battle was 8751. These are NOT the final failing reservation sample: the stale-peer early return occurs before those fields refresh. Outer timing subsequently reaches clock 119. Local reached the fifth-round boundary and stopped on the remote stop message.

Failure 18 / transition failure 10 aggregated memory-read failure, changed event/round/generation/clock identity, target arrival/overrun, clock regression and time bounds. The old report cannot prove which individual clause failed. Reaching the target while freshness remains unavailable is plausible, not established fact. No guard is relaxed on that assumption.

## Native audit

Fresh read-only Ghidra run: work/ghidra-sc6-focus/peer-stale-reservation-20261023. All five targeted functions decompiled. 384040 performs Start updates; 3840E0 exits only when elapsed +8 reaches duration +C. 3F5D20 calls service/readiness/advance virtuals. Holding before the countdown completes risks blocking native progress. These functions do not publish our peer status; that belongs to the mod control loop.

## Supported fix

Previously CombatPeerContext was published only by SteamHandshake::poll on the UE4SS control/update path, using a timestamp sampled at the beginning of that potentially long poll. The independent fast-input network worker did not renew that evidence, and raw input stops during round retirement. Consequently the freshness check measured control-loop servicing as well as connection liveness.

The existing fast worker now exchanges a bounded 96-byte heartbeat every 50 ms, including when no raw route exists between rounds. Receipt validates the Steam sender, both established match nonces, kind, sequence and zero payload. It refreshes only an already-established CombatPeerContext; it cannot establish identity, change side, grant activation permission, revive a cleared context, or apply inputs. Duplicate/reordered/old-session messages do not renew evidence. Control publication cannot overwrite newer received evidence with an older timestamp and now samples publication time freshly.

The worker still stops with the match. Old builds do not send the heartbeat, so bilateral testing requires both updated builds. The existing control path remains available. 250 ms freshness, 9 s peer recovery, 10 s reservation expiry, exact target, identity, ownership, verification and round guards are unchanged. No extra input delay or game-thread wait.

## Diagnostics

peer_liveness_sent/received/rejected/send_failed count the worker traffic. continuous_rearm_peer_control_tick and heartbeat_tick distinguish control and network updates. continuous_transition_wait_mask records every failed clause at the actual failure, with its battle, clock, identity, event, round, generation, current time and reservation time.

Mask bits: 1 failed projection; 2 event; 4 round; 8 generation; 16 clock identity; 32 target reached/passed; 64 clock regressed; 128 time regressed; 256 reservation expired; 512 battle read failed; 1024 clock read failed.

Regression coverage exercises liveness with no route/control polling, duplicate and wrong identity rejection, cleared context, fresh-to-stale expiry, late control publication, worker stop, each reservation reason and combined reasons. Live fifth-round validation remains required; old logs cannot establish why the control loop itself missed servicing.
