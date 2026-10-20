# Confirmation duplicate coalescing

The latest local scheduler trace attributes 858 refused admissions to prediction-budget exhaustion, not handoff. Enqueue-to-owner delay averaged 8.3ms (p95 16ms). Those timings do not measure network transit. They do not prove queue overflow.

Transport sends recent input retransmissions alongside fresh inputs. Previously every identical retransmission occupied one of 32 queue entries. A burst of eight packets per poll can exhaust that queue before the next owner drain. The producer now remembers the first successfully queued raw word per authenticated route/frame. Identical retransmits are coalesced; different words still reach conflict validation. Full queue rejection does not populate the cache. Retired routes and identity validation precede coalescing. First-arrival timing is preserved.

New atomic report counters: continuous_raw_coalesced and continuous_raw_queue_full. Existing owner-side duplicate counters now count only duplicates that reach the owner, such as native-confirmed inputs. No changes to the three-frame limit, input identity, correction verification, native pacing, round safety, or snapshot ownership. Includes the preceding sway-link restoration fix.

Tests cover a 1,600-packet retransmit burst leaving room for fresh inputs, queue overflow then retry, conflicting retransmission failure, concurrent ring reuse, first timestamps and round handoff. This removes demonstrated synthetic queue pressure; live speed improvement is unmeasured. Cross-country latency exceeding the three-frame budget can still require waiting. Do not claim this solves all slow motion.
