# Camera decision-counter sentinel transition

Both AllWindBirth reports completed two cycles and rejected cycle 3 during reverse restoration, hop 1, source 899 / next 900. No round transition was reported. Combat preflight mask was 127: all bits except camera readiness passed. Timer diagnostic phase 2/reason 0 also passed. Therefore the broad combat rejection (16) was not an animation timer rejection.

Ben PID 58432: camera reason 16, record 1, section 3, packed offset 116 (owner+2A4), FFFFFFFF -> 00000000.
Partner PID 43780: same reason/record/section, packed offset 120 (owner+2A8), same values.
The failing cycle recorded one birth but applied no birth seed; it did not exercise the new stage seed restoration. No wind preflight failure was reported.

Fresh Ghidra read-only decompilation of RVA31AC60 completed, body 324 bytes, in workspace work/ghidra-sc6-focus/counter-sentinel-58432. It reads player flags +1732/+1733, resets each counter to -1 when its condition is absent, otherwise increments the integer. Signed comparisons against 5/6 then gate camera decisions. Thus -1 -> 0 and nonnegative -> -1 are native transitions. Nothing in this evidence identifies neutral controller input as invalid.

The old decision_counter_pair allowed equal values and nonnegative increments only. The fix permits reset and sentinel activation for both counters while rejecting other negative values, skipped increments and signed overflow. Existing exact saved-value writes, live identity/provenance validation, endpoint comparison, diagnostics, and round/fight guards remain unchanged. No camera bytes are masked from verification.

Regression tests exercise both offsets with source -1/0/1/26, increment and reset transitions, validate-only nonmutation, exact restoration and stale-live rejection. FullRestore tests passed. Suite summary tests passed. Full build and installation recorded in CATCH_UP_RESEARCH.md once complete. This is locally validated, not yet verified by another live match.
