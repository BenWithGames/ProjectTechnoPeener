# Per-input timeline, schema 1

Automatic reports on both PCs append timeline rows to the existing continuous-events CSV. No additional script is required. The existing correction/advance journal remains unchanged. Match ID in the report filename pairs reports; session nonce, round, round event, absolute frame and side pair inputs. PCs have independent QPC clocks: never subtract one PC timestamp from another without clock-offset uncertainty estimation. Compare local durations and matching frame identities instead.

For kinds 100–111, columns are sequence (timeline-local, separate from journal sequence), session (local nonce), round, frame (absolute input frame including agreed delay), head (round event), kind, detail (input side), qpc (local timestamp), aux0 (raw input), aux1 (context). Sort by QPC, not file order; deferred drains interleave journal and timeline batches. Missing sample timestamps are omitted, never invented.

| Kind | Meaning | Context |
|---|---|---|
|100|Sample begin|0|
|101|Sample end|0|
|102|Local publication|agreed input delay|
|103|First successful send begin|0|
|104|First successful send return|0|
|105|First accepted packet read timestamp|0|
|106|Confirmation enqueue|local publication count|
|107|Owner truth drain|prediction head|
|108|Remote input selected|1 confirmed, 0 predicted|
|109|Both native consumers complete|local raw word|
|110|Scheduler confirmation admission|scheduler head|
|111|Drain deferred|1 means native consumers incomplete; frame names current selection, raw is 0|

Repeated identical retransmissions and idempotent publication calls do not duplicate input events. Failed send attempts remain covered by existing transport counters. Packet read time precedes route enqueue; it is not wire arrival time. Selection raw is recorded before later confirmation recomposes history. Compare selection with admitted truth to distinguish actual wrong predictions from unchanged inputs. Existing correction journal and outer timing CSV supply execution and replay boundaries; correlate logical journal frame with timeline absolute frame using the route origin.

A fixed 8192-entry buffer is allocated with each route, before use. Producers attempt one atomic flag acquisition and drop on contention/full; no spin, sleep, allocation or file I/O occurs in capture. Report worker copies at most 128 rows under the flag and formats outside it. Summary includes contention/full drops; incomplete timelines must not be treated as complete evidence. The report worker has bounded work per drain. Diagnostic acquisition cannot reject input, affect scheduler decisions, or alter safety checks. Adaptive delay and prediction limits are unchanged. Instrumentation still has a small CPU/memory cost; this is not a claim of zero overhead.

Validation: route regression covers ordered stages, duplicate suppression, all delay policies, bounded overflow/reuse and concurrent producers with report draining. Full native suite is required before packaging. Native boundaries preserve the earlier Ghidra audit in CONFIRMATION_ADMISSION_AUDIT.md: never change raw words between the two player consumers.
