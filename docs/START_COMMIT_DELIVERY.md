# Round-start commit delivery

Evidence: paired match 366150f812ff85b3593de1b9baa5106c, processes 58676 and 20364.
Both prepared and seeded event 4905. The coordinator committed and released;
the follower expired its proposal (reason 4), then sent the stop packet. Neither
reported a round-three restoration failure. Earlier reports do not contain
packet-level commit timestamps, so they cannot prove loss versus queue delay
versus rejection. The coordinator's successful send only proves local enqueue.

Proposal, acknowledgment and commit previously used only Steam's ordered reliable
mode (2). They now also send an identical immediate datagram (mode 1) at the
existing bounded cadence. Reliable delivery remains as a backup. Identical
authenticated round, ticket, target and offer validation handles duplicates and
reordering. A failed send on both paths cannot publish the coordinator's start.
No new input delay or longer release margin is introduced.

This corrects dependence on ordered retransmission for a short deadline; it does
not guarantee delivery under a partition. Expired or invalid commitments still
stop safely. Live validation of the reported failure remains pending.

New per-round delivery_1 through delivery_6 CSV values:
logical sends, received, accepted, first send us, last send us, first receive us,
last receive us, immediate enqueue successes, reliable enqueue successes,
transport send failures. Times are local steady-clock microseconds; use recorded
offsets for cross-PC comparisons. Counts measure enqueue/acceptance, not delivery
acknowledgments. Last rejection stages: 1 round/event, 2 offer, 3 ticket echo,
4 peer hold, 5 timing/target/deadline. Unauthenticated packets remain rejected
before these counters. Old completed-round packets retain their separate counter.

Ghidra read-only audit: work/ghidra-sc6-focus/commit-delivery-audit. Clock entry
RVA 3F5D20 calls service +620, wait +628, and only advances +630 if not waiting.
Start update 384040, exit 3840E0 and phases 3FC920 were rechecked. These native
paths are unchanged; bypassing their wait check would not repair network delivery.

Regression: delaying reliable commits 300 ms reproduces coordinator release and
follower expiry. Immediate duplicates pass the same scenario with two initial
commit datagrams lost and late reliable duplicates. Existing offset, asymmetric
path, packet-loss, stale-round, exact-target and deadline tests remain enabled.

Steam send semantics reference:
https://partner.steamgames.com/doc/api/ISteamNetworking#EP2PSend
