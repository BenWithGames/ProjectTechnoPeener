# Completion delivery and lobby restart

Match 05324fe4462dda7a940e84255ff57dae: local completed native cleanup and accepted 71 peer completion packets, sent 121, never saw peer acknowledgement. Friend completed native cleanup, sent 89, accepted zero completion packets. Both timed out after 30 seconds. No restoration failure was recorded. Send enqueue success is not delivery proof. The logs do not prove whether completion was delayed in transport or receive queues.

The old completion path shared 27861 with historical input retransmissions and used reliable enqueue as the final-send guarantee. Control polling drained at most 128 packets, and continued archive retransmission while waiting for match completion. Once complete, it stopped sending acknowledgements and eventually forgot the old nonce on reset. These are separate delivery and final-ACK lifecycle weaknesses.

Completion now uses dedicated channel 27865, drained before timeout decisions, diagnostic output and match reset. Messages are retried with UnreliableNoDelay at 250 ms; existing bilateral local-clean/peer-clean/peer-ack/sent-ack requirements remain. Native cleanup is still required before local completion and before release. Historical input retransmission stops once local completion is established. No bypass of owned-resource, snapshot, round or verification checks.

After a completed exchange, a control-thread-only responder retains peer and both old nonces for 60 seconds. It holds no native objects, cannot authorize new-match cleanup, and responds to valid old-session retransmissions after returning to lobby or starting a new match. Four bounded slots never evict an unexpired entry; exhaustion fails closed. Replies are rate-limited to 100 ms per retained session. The old responder also runs after data_done and before session reset. All native retirement/report-saved checks remain prerequisites for resetting automatic play.

The completion wire protocol version changed: both peers must update. Dedicated ingress reports wire receives/rejections/oversize and send failures; retired response counts are process cumulative. Existing completion state/counters remain.

Regression tests cover lost final ACK after local reset, new-match isolation, delayed completion at timeout, wrong peer/nonces/payload, duplicate rate limits, responder expiry, send failure/retry, bounded capacity and refusal to retain an incomplete exchange. Native lifecycle guards remain covered by the full suite. Live lobby/character-change validation is still needed.
