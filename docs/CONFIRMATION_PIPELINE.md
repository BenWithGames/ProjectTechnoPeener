# Confirmation pipeline follow-up

Receipt-grace live validation is pending. No post-ReceiptGrace paired match was
available when this build was prepared; the older match cannot establish its
effect. Keep its attempts/recovered-admissions/wait-time counters for comparison.

The paired match 366150f812ff85b3593de1b9baa5106c shows a roughly 33 ms peer phase
gain after frame 120, relative to the first 32 common advances. This agrees with
the earlier isolated prediction-limit miss. The new offline comparison script
uses same-frame advances, per-round baselines and matching shared match IDs.
Negative phase change means the peer gained lead. This is not absolute clock
synchronization, one-way network latency, or proof of a particular packet loss.
Both reports must have the same explicitly supplied QPC frequency.

The native input path already drains at RawPredictionRoute::append, before
selecting raw inputs, and defers drains between the two player consumers. The
coordinator also drains before the scheduler step and after completion. Adding
another identical drain would add work without closing a new gap. These paths
remain unchanged. Existing filter tests exercise receipt use and half-frame
exclusion. No new pacing delay, native clock increment or catch-up loop is added.

The fast sender now repeats the two preceding immutable inputs after fresh
progress, on the dedicated fast channel. It does not resend continuously when
idle, does not advance a failed fresh cursor, and resets at route replacement.
Existing authenticated ingress, duplicate coalescing, conflicting-input checks,
old-round validation and control-channel retries remain intact. This adds at
most two small datagrams per fresh worker batch; it is a loss-recovery
opportunity, not a guarantee that internet packets arrive sooner.

New receipt-frontier bins compare the received input index with local publication
count. The endpoint bins include values beyond +/-4. This is deliberately labeled
NOT simulation lead: network transit, packet order and different publication
rates all affect it. Unique-arrival gap maximum is local QPC ticks; retransmitted
duplicates do not count as new arrivals. Use paired advance events to establish
actual relative phase change. Existing sampling, send, receive, drain, correction
depth and outer-update diagnostics remain available.

Ghidra was rerun read-only using SC6ConfirmationSkewAudit.java, output directory
work/ghidra-sc6-focus/confirmation-pipeline-audit. Disassembly at RVA 3F5D20
confirms service(+620), wait(+628), conditional advance(+630). The focused
project has incomplete function extents; this does not prove every native path.
No native instructions or admission/verification/safety rules are changed here.

Validation: fast-sender repeat bounds, route replacement, idle suppression and
existing authentication/retry/retirement tests; paired-phase synthetic tests;
full native build/test suite. Live paired validation remains required for fewer
corrections, lower depths, and absence of added networking overhead.

Run Compare-ConfirmationTiming.py LOCAL.zip PEER.zip --qpc-frequency 10000000
only when both reports specify that frequency. Compare matched conditions and
corrections per second as well as depths; input patterns affect correction counts.
