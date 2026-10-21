# Restoration breakdown and confirmation age

Instrumentation only: no scheduler, prediction-depth, restore acceptance, verification, ownership or round-safety changes. Includes the preceding stream-shape diagnostics; that unresolved Reversal Edge rejection remains guarded.

## Native timing boundaries

Ghidra root save/load audit (workspace work/ghidra-sc6-focus/restore-timing-audit) verifies the existing native entry points: save RVA 3841E0 and load RVA 384540. Timers surround their existing indirect calls, not newly installed game hooks. The native save and load both perform subordinate serialization and conditional transfers. Native load is therefore distinct from the mod's later fixes, re-save and endpoint comparison.

continuous_timing_schema=2 retains all old fields and appends:
- restore_ownership: history dependency validation before mutation.
- restore_all_hop_checks: archived general-combat preparation checks.
- restore_snapshot_copy: current snapshot copy into private scratch.
- restore_prepare_adjacent: preparation including reader copies and readiness checks.
- restore_retained_objects / restore_retained_compare: effect ownership restoration and comparison.
- restore_native_load / restore_native_resave: native calls in delayed restoration.
- restore_state_capture: repeated capture_blend calls in delayed restoration (includes transform capture).
- restore_combat_preflight / restore_source_guard: guard work.
- restore_wind / restore_sway / restore_constraints / restore_camera / restore_blend / restore_rings: selected post-load fixups and dry-run checks; each category includes both validation and application calls in this path.
- restore_payload_compare / restore_completion / restore_selector / restore_endpoint_compare: exact payload verification, completion fixes, selector restoration/verification, final endpoint checks.

Spans are inclusive and overlap. Do not sum all old and new buckets. A three-frame correction can call an adjacent loader three times; means are per span call, not per correction. Existing outer timing and restore total remain the per-correction comparison. Some glue work remains outside a named detail span. Exception-aborted native calls may not finish their span; the existing failure diagnostics remain authoritative.

No per-frame formatting, heap allocation or disk I/O is added by timing samples. QPC and atomic aggregation add overhead which is not claimed to be zero. Report formatting stays on the existing deferred reporting path.

## Confirmation measurements

All values use the receiving PC's local QPC. No timestamps from separate machines are subtracted. These metrics are not ping, one-way network latency, physical controller latency, or rendered input response.

confirmation_age_* reports count, mean, maximum and eight histogram bins for:
- receive_to_drain: first accepted route enqueue to owner-thread drain.
- drain_to_scheduler: owner drain to accepted scheduler submission.
- local_publish_to_receive: local publication of this frame to receipt of the corresponding remote input, for nonnegative intervals.
- local_publish_to_scheduler: local publication to accepted scheduler submission.

Histogram upper bounds in microseconds: 1000, 4000, 8000, 16666.6667, 33333.3333, 66666.6667, 100000; last bin is larger. behind_frames_0 through _3 count head minus confirmed frame when submitted; _4 means four or more. early counts remote receipt before the local counterpart was published. unpublished means the counterpart had not yet been published. unmeasured covers native-authoritative truth without transport timestamps; invalid catches reversed/missing clocks. Those cases are never reported as zero network latency.

Measurement occurs once after successful coordinator submission; retransmissions/coalesced duplicates do not add samples. Existing per-confirmation journal records retain frame, head, enqueue/drain QPC and scheduler timestamp for correlation with outer timings. Existing fresh-sender publication-to-send-accept timings remain available separately.

## Validation and use

Deterministic tests cover local intervals, early/unpublished/native truth, invalid timestamp order, depth bins, preserving timed function return values, exception propagation and timing bindings. Full native regression build follows.

Both install. First private lobby, no warmup, Ben P1 / friend P2. Run the existing Steam-TestSuite scripts and play normally. Send both report ZIPs after completion or failure. This is a measurement build, not a claimed speed improvement or Reversal Edge fix.
