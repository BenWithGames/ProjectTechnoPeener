# Dedicated raw confirmation receive

Ghidra clock-dispatch/advance audit at 3F5D20, 3E2000 and combat delta loop 3FE520 remains unchanged: native input processing, scheduler admission and corrections stay on the owner thread. No clock increment or native wait is bypassed.

The existing one-millisecond transport worker now sends fresh raw packets on Steam virtual channel 27863 and drains only that channel (up to 32 packets per wake). It checks Steam peer, magic, both nonces, complete raw envelope and route round/event/slot identity before queueing. No snapshots, native memory or scheduler are accessed from the worker. The existing channel 27861 keeps control packets and fallback raw retransmissions, avoiding competing readers. This is a Steam virtual channel, not a router port.

The raw route serializes producer publication using its existing atomic gate. Identical retransmissions coalesce; conflicting words still reach owner validation. Full queues rely on retransmission. Routes live for the process, and retired identities retain existing handling. Oversized packets disable the fast receive path and retain fallback polling; failed worker startup likewise leaves fallback intact. All prior diagnostics and safety remain enabled, including peer-context rearming and the ten-minute limit.

New counters: fresh_raw_receiver_received/rejected/full/faults. Received includes coalesced valid repeats; not unique simulated frames. Existing admission, enqueue-to-owner timing, sender-first-send, correction and whole-outer timings remain. The previous roughly 8ms queue-to-owner mean cannot itself be eliminated by receiving earlier: the owner still consumes at native boundaries. The improvement sought is removal of the earlier transport polling delay, with live validation required.

Tests cover actual worker receive/shutdown, wrong Steam identity/nonces/magic/reserved words, concurrent fast/fallback producer publication, queue backpressure and deduplication. No claim of 60 updates/sec is made before live measurement.
