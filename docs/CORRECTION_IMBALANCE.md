# Correction imbalance and late-receipt grace

Historical 2 ms implementation notes. The later measured-arrival adjustment to
8 ms and its tradeoffs are documented in TIMING_IMBALANCE_RECOVERY.md.

Paired evidence: match 366150f812ff85b3593de1b9baa5106c, local 58676 and peer 20364.
Round 1 corrections: 22 versus 136; replayed frames: 24 versus 370.
Round 2 corrections: 158 versus 196; replayed frames: 160 versus 196.
Both used agreed 2-frame delay and the same 3-frame prediction limit.

During round 1 the local scheduler refused head 161 because confirmation 158
was missing. Its enqueue timestamp is 1.3342 ms after the admission check, but
owner draining happened 16.6904 ms after that check. The live completion gap
ending at frame 161 was 53.96 ms. Comparing same-frame completion timestamps
with the measured start clock offset shows the peer roughly aligned initially,
then about 31.5 ms ahead through most of the round. Clock-offset estimates are
not an exact one-way delay measurement; the within-round change is the stronger
evidence. Round 2's offset was approximately 6 ms instead.

The friend's round-one confirmations arrived 3 frames behind in 1616 of 1780
measured admissions. Local sample-to-publish and publish-to-send averages were
already below 0.2 ms. Logs establish the admission miss and subsequent lead,
but do not prove whether the initial packet delay was loss, transport batching,
or an OS/network scheduling interruption. Different input patterns also affect
correction counts; equal counts are not a correctness requirement.

Fix: only when an eligible owner-thread clock update is blocked by scheduler
reason 5 (prediction budget), allow one event-driven receipt wait per clock
identity/frame. A high-resolution timer requests 2 ms, with a 3 ms kernel wait
timeout backstop. OS descheduling can exceed these nominal budgets; actual time
is reported. Pending receipts skip sleeping. Producer notification is enabled
only while waiting, and publication precedes notification. No busy-spin loop.

After waking, drain through the existing raw coordinator and re-run the unchanged
reservation/admission checks. Missing truth still withholds advancement; conflicts
still stop. Handoff, native-ineligible states, failed routes and the baseline mode
do not use grace. Native finish revalidates identity/frame and invokes at most one
advance. No prediction depth, input-delay policy, snapshot or restoration rules
are changed. This prevents near-deadline misses from needlessly costing a full
update; it is not a general frame-pacing or catch-up mechanism and cannot remove
all asymmetric corrections under persistent latency or loss.

Ghidra: work/ghidra-sc6-focus/correction-imbalance-audit. Machine-code sequence
at RVA 3F5D20 is service (+620), wait (+628), conditional advance (+630).
3FDF30 samples raw input before consumption. 3FDFA0/3E9EE0 govern native waits;
3FE520 consumes the clock delta. These functions were decompiled and the clock
sequence checked against the emitted disassembly; function body extents in the
focused project are incomplete, so decompiler output alone is not proof of all
paths. No native instructions are patched by this change.

Diagnostics: receipt_grace_attempts, recovered_admissions, waits, wakes, timeouts,
faults, total_us and max_us. Existing outer timings include the actual grace cost.
Regression coverage includes eligible receipt recovery, timeout, repeated calls
to the same frame, failure containment, native wait/handoff exclusion, pending
receipt and wake races. Live paired-match validation remains required.
