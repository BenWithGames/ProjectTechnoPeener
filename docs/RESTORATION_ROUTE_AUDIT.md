# Restoration destination audit

## Scope and evidence

This preventive audit follows the descriptor-padding audit. It does not attribute the last live freeze to a newly discovered cause, and it does not certify every dynamic serializer or gameplay path.

Pinned SC6 binary SHA256: F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553.

Fresh Ghidra decompilation and disassembly are in `work/ghidra-sc6-focus/restoration-route-audit`, produced by `SC6RestorationRouteAudit.java`. Reviewed root loader RVA 384540 against root save 3841E0, selected-bank loaders 391E10 and 390800, manager loader 321A30, and existing shared/selector restoration evidence. Split unwind ranges were checked against disassembly rather than treating a decompiler function boundary as complete coverage.

The root loader selects destinations using live globals outside its serialized 470D200..470DDF0 block:

| Selector RVA | Meaning | Saved writer anchors |
| --- | --- | --- |
| 470DEDC | Zero/nonzero selects one of two global state banks | 391DEA (2 bytes), 3906CC (0xC60 bytes), 38448D (4 bytes) |
| 470DEE8 | Optional destination buffer, present iff non-null | 384403 (0x360 bytes) |
| 470DF28 | Manager supplying fixed fields | 321861 (4 bytes at manager+0xA8) |

Comparing two archived snapshots alone does not prove the live loader still selects those destinations. A changed optional-buffer presence can also change the stream consumption order.

## Changes

`NativeLoadRoute` checks the live selections against the original saved writer records before native loading starts. Mandatory anchors must occur once with the audited size and destination. The optional anchor must occur exactly when the live optional pointer is non-null. Wrong destinations, duplicate/missing anchors, invalid metadata and inaccessible selector reads reject before restoration writes. The check is used by both immediate and delayed native restoration; injected mock readers remain independent of game-memory access.

The guard does not overwrite pointers to force an old allocation back into service. Ownership, external-reference, source, timer, RNG, payload and endpoint checks remain enabled. Existing round-safety checks remain enabled.

Four fields propagate through the existing restoration report: `route_reason`, `route_record`, `route_expected`, `route_actual`. Reasons: 1 invalid metadata, 2 duplicate/wrong-sized anchor, 3 destination mismatch, 4 missing/unexpected anchor count, 5 selector-read exception. For reason 4, expected identifies the writer and actual is its observed count; for reason 5 actual contains the exception code.

`IdleTrajectoryAudit::same_shape` now bounds record counts and payload positions and rejects failed streams before indexing them. Both restored-payload comparators inherit this hardening.

## Regression coverage

FullStateProbe tests both banks with and without the optional buffer; changed bank/manager/optional selections; changed writer, size or source; duplicate anchors; invalid base and unsealed streams. NativeRollbackWindow tests excessive record counts, oversized payload positions, and route diagnostic propagation alongside the descriptor-padding and raw mismatch diagnostics. These tests exercise the validation model; live integration remains to be verified in SC6.

Logs: `build-native/restoration-audit-fullrestore.log`, `build-native/restoration-audit-window.log`, and `build-native/restoration-audit-full-build.log`.

## Remaining limits

Matching addresses cannot establish object lifetime by itself; the existing ownership and identity checks are still required. This audit does not prove that every RNG consumer, effect subclass, animation transition or native callback is fully covered. The last report lacked the exact mismatching payload record; the retained payload diagnostics in this build are needed to resolve any remaining live mismatch.
