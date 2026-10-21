# Low-latency input publication

Local immutable raw-input publication now signals an auto-reset event after the archive release-store. The existing network worker wakes immediately, sends available local inputs before receiving a bounded batch, and retains its one-millisecond timer for receive polling and failed-send retries. No extra input delay, deeper prediction, game-thread network calls, busy-spin or scheduler wait is introduced.

The event is shared across route replacements and outlives the sender worker. Event counts do not own delivery: immutable archive cursors preserve identity, retries and retirement checks. If event creation fails, polling remains available. Stop remains the first wait handle. Existing confirmation authentication, owner-thread reconciliation, restoration verification and round checks remain unchanged.

Evidence: friend report steam-inputs-24356 recorded 44,670,127 publication-age ticks / 5,841 first sends at 10 MHz, approximately 765 microseconds average. Confirmation receive-to-owner-drain averages were 9.45, 11.04 and 11.07 ms by generation; this patch does not claim to remove that frame-boundary delay.

Ghidra audit: work/ghidra-sc6-focus/low-latency-audit, pinned executable SHA256 F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553. RVA 3F6600 publishes bounded native ring entries with identity/frame checks. RVA 3FDFA0 has state-dependent waits and mutates timeout bookkeeping: reason 2 is horizon waiting, reason 3 pacing, and transition/timeout reasons remain distinct. Therefore no extra native wait calls, forged native confirmations or wait bypass changes were made.

New sender schema 2 reports publication wake support, publication wakes and timer wakes alongside first-send age. Regression coverage checks persistent/coalesced auto-reset signals, sending an input published after worker startup, sender stop, send retry, route replacement, retirement, malformed packets and authenticated ingress.

Expected effect is reduced send scheduling latency, not a guaranteed 60-update match. Compare first-send age, confirmation ages, prediction-limit stalls and whole-outer timings in the next test. Existing direct restoration remains enabled.

Timing audit: dispatcher calls occur between measured outer callbacks. Comparing each row's after-counters against the preceding row's after-counters finds 904 phase-2 zero-live rows with one clock call and one unadmitted step, matching the 371+337+196 gaps in the three active spans (split on live-update gaps over 0.5 seconds). Within-callback counter deltas alone miss these calls. This supports investigating scheduler withholding; the sender change does not alter admission or prove which scheduler condition caused each withholding. Rendered FPS was not measured.
