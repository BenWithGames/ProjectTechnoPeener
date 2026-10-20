# Scheduler and confirmation trace

The previous outer trace proved scheduler refusal (about 1,500 calls per PC) but did not distinguish prediction-budget exhaustion from handoff. This update changes diagnostics only; the prediction limit and admission decisions remain unchanged.

The continuous-events CSV retains its original seven columns and appends qpc, aux0, aux1. Timestamps are local QPC ticks, using outer_timing_frequency; never subtract timestamps from different PCs.

Event kind 8 is an admission observation at the actual reservation attempt. frame is the contiguous confirmed frontier; head is the simulation head; aux0 is the settled frontier; aux1 is the earliest dirty frame or UINT64_MAX. detail reasons: 0 ready; 1 invalid identity/state/head; 2 round draining; 3 invalid dirty range; 4 missing dirty history; 5 prediction limit exhausted. Logging occurs only at real admission attempts, not report-thread forecast reads. Invalid/hand-off checks preserve their previous precedence; frontier is not scanned on early returns.

Event kind 9 records delivery of a new truth frame to the scheduler. aux0 is the receiving transport thread's accepted-enqueue QPC; aux1 is its first owner-thread drain QPC; qpc is scheduler delivery time. Kind 2 remains the scheduler's accepted arrival. Duplicate packets do not replace first-arrival times. Queued timestamps are published with the existing SPSC release/acquire protocol. They measure local queue and coordinator delay, NOT packet transit time or remote sampling delay. Missing native-confirmed timestamps must not be interpreted as network latency.

All records use the existing bounded journal and worker-side disk writes. First-arrival storage is allocated once at route construction, never in a per-frame callback. Tests cover budget versus handoff refusal, confirmed prefix advancement without settlement, observation without state mutation, timestamp ordering, duplicate preservation and invalid owner/index. The multi-route unit-test fixtures use heap allocation like production to avoid exhausting the test executable's default stack.

Ghidra is not needed to identify these scheduler decisions: they are in our source. The prior audited native clock/outer behavior is unchanged. This build also retains the effect-restore contention retry and exact diagnostics, all native checks, ownership checks and round safety. Live evidence is needed before choosing a pacing fix.
