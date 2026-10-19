# Descriptor padding comparison and live mismatch evidence

## Observed failure
Ben 81592 and partner 14304 independently reached restore stage 4/load stage 6, hop 2, depth 3, with source-identity checks and timer checks passing. Native loading ran; the immediate restored-payload comparator rejected. No correction replay completed. The old continuous report omitted the already-populated PayloadDiagnostic, so these reports cannot identify the mismatching byte. Padding is a demonstrated comparator defect, not a proven attribution of those two failures.

## Ghidra evidence
Pinned SC6 SHA256 F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553.
The descriptor save routines write a DWORD reference-table index at offset 0 and a QWORD offset/pointer at offset 8, then serialize 16 bytes. Bytes 4..7 are uninitialized local padding. Matching loaders read the 16-byte local, use its DWORD index and QWORD offset, and do not use bytes 4..7. Normal capture and restoration/re-save use different stack frames. Consequently comparing this padding can reject otherwise equivalent state.

Evidence: work/ghidra-sc6-focus/record339-full (30A980/30AE80), temporary-writer-audit and root-writer-audit (31DC00/31DE50), temporary-load-audit and fresh padding-load-validation (301B10,390800), rollback-contract-audit (32AC00/32ACE0). The full paired return-RVA inventory is NativeReferencePadding.hpp. Each writer's DWORD/QWORD stores and matching loader's DWORD/QWORD reads were checked against disassembly. The earlier exception covered only 32AC76/32AD1B.

## Change
A shared whitelist of 18 writer/reader return-address pairs now covers the audited descriptors in both immediate restored-payload and replay endpoint comparison. It requires 16-byte length, matching original/re-save writer, completed restoration, a sealed source record, and the exact observed reader. Only differing bytes 4..7 are exempted. Raw bytes remain untouched. All active fields, unknown sites, owner identity, timers, round safety, and unrelated payloads remain checked.

Existing immediate-load mismatch diagnostics now propagate through NativeHistoryRestore, scheduler storage, session termination and CombatRuntimeStatus into continuous reports: structural flags, record/byte counts, retained/overflow counts and up to 16 records with length, writer, source addresses, first/last changed offset and first expected/actual byte. This is bounded report evidence, not extra per-frame file I/O.

## Validation
NativeRollbackWindowTest changes each of all 16 bytes for every audited pair, checks both comparisons, rejects an incorrect reader/writer and missing restoration, and verifies raw bytes remain intact. It also tests diagnostic copying and report formatting, including the final retained slot and overflow. Existing restoration and round-safety tests remain in the full build.

A live correction is still unverified; any remaining mismatch must be evaluated from the new report rather than presumed to be padding.
