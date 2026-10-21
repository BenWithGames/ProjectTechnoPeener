# Overlay ping worker

The overlay now uses dedicated-channel RTT probes (27864). Native gameplay channel 20001, raw-input channel 27863, and the existing diagnostic protocols are untouched. Both players need this build; no reply is shown as unavailable, not zero.

Five probes are sent 20 ms apart every three seconds. The minimum round trip minus the responder's measured handling time is displayed. The worker uses a local high-resolution waitable timer targeting 1 ms polling, with a Sleep fallback. This removes dependence on the mod on_update cadence, but does not remove Steam/OS queueing or guarantee pure wire RTT. It measures the mod Steam P2P route, not rendered latency or input latency.

The automatic compatibility handshake supplies the expected Steam peer and both session nonces. The worker accepts only fixed-size packets from that peer with matching nonces, protocol version, kind and probe ID. Replies are rate-limited, reads are bounded, and disconnect/session replacement clears the displayed reading. Configuration expires after three seconds without an update. The worker is joined before shutdown. No game-state writes, native packet reads, or global timer-period changes are introduced.

Existing ping_rtt_ms diagnostics remain the old main-loop measurement for comparison. New overlay_ping_* fields report the worker estimate, raw RTT, measured handling, accepted sample count and maximum worker polling gap. Both estimates are explicitly network_only=0.

Ghidra: work/ghidra-sc6-focus/ping-import-audit.txt is a fresh read-only import/reference audit of the pinned SC6 binary. It exposes Steam callback imports but no named direct networking RTT accessor. This is limited negative evidence, not proof no internal RTT exists. The source audit establishes that current legacy ping handling runs in Mod::on_update and uses SteamNetworking005. The improvement therefore stays at the supported Steam API boundary rather than guessing a native ping field or changing game callbacks.

Validation: SteamPingTest covers burst timing, handling subtraction, duplicate/late/invalid replies, failed sends and stale expiry. OverlayPingTest covers peer/session/shape validation, channel separation, disconnect clearing and threaded loopback collection while the game update/configure caller is idle. A real two-PC comparison is still needed to measure the accuracy improvement and scheduling overhead.
