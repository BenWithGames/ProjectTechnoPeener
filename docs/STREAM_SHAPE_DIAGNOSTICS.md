# Stream-shape rejection evidence

This build diagnoses the Reversal Edge stop; it does not claim to repair an unidentified native layout transition. All restoration acceptance rules, exact verification, ownership checks and round safety remain enabled. Fast raw receive remains enabled.

## Observed failure

Ben PID 74124 completed 192 corrections and then rejected a three-frame correction during all-hop preparation (hop 1, rejection 8: stream_shape). No load was invoked for that attempt. Friend PID 11172 completed 141 corrections with no local failure and stopped on the peer notification. Neither report contains the failed snapshot streams. Consequently the exact count/length/writer/validity condition cannot be recovered retrospectively.

## Ghidra evidence

Pinned executable SHA256: F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553.

Focused root save/load audit: RVAs 3841E0 / 384540. Machine code at 3843E8 tests pointer RVA 470DEE8 and conditionally writes 0x360 bytes (return address 384403). Loader at 384757 tests the same live pointer and conditionally reads 0x360 bytes. This proves that blindly removing stream-shape validation is unsafe; it does not establish that this particular branch caused the observed failure. Player save/load RVAs 30A980 / 30AE80 were also decompiled. The top-level player save has fixed blocks followed by a nested serializer and pointer remapping. Identifying the first differing writer is needed before selecting a nested native function to change.

Audit outputs in workspace work/ghidra-sc6-focus/stream-shape-audit and player-shape-audit. Decompiler inferred function bodies were cross-checked against the emitted root disassembly for the conditional block described above.

## Added report evidence

continuous_restore_shape_* records reason, first differing record, both record counts, lengths, writer return addresses, source addresses, sealed flags, failure counters, byte lengths, module base and both native frame numbers. Subtract module base from writer addresses to obtain RVAs. Missing records have zero length/address. Invalid stream-level failures use record UINT32_MAX.

Reasons: 1 unsealed; 2 count exceeds capacity; 3 stream failure; 4 payload position outside capacity; 5 record count mismatch; 6 record length mismatch; 7 writer mismatch. If counts differ, the common prefix is scanned first to identify an earlier inserted or changed record. No source-address equality requirement is introduced.

The diagnostic is fixed-size and is emitted through existing deferred reporting. It reads existing archived metadata, not live source pointers. Passing shape checks still accept changing payload values and temporary source addresses. A failed correction still stops; it cannot safely be skipped once predictions require correction.

## Validation and next test

256 guard-equivalence combinations preserve the old acceptance decision. Additional cases cover count changes, inserted lengths, writer changes and temporary-source changes. The full native build executes the regression suite.

Install on both PCs. First private lobby, Ben P1 / friend P2, no warmup; run Steam-TestSuite.cmd and play normally including Reversal Edge. On failure close both games and collect both reports. The newly identified writer/record will determine the native restoration fix; no claim of a freeze fix is made by this build.
