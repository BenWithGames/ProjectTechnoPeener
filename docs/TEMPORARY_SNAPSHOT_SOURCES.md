# Native temporary snapshot source audit — 2026-10-18

Pinned executable SHA256: F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553.

## Failure and contract
Ben steam-inputs-11064 rejected record 656 (16 bytes), writer return RVA 31DCF1, before restoration. Its source moved by 2928 bytes between captures. The partner received the stop. Record indices are capture-dependent, not native type IDs.

Ghidra shows 31DC00 serializes four reference descriptors through one [rsp+20] local. A descriptor stores reference-table index at +0 and pointer/relative offset at +8. Loader 31DE50 reads a fresh local and reconstructs four global pointers. The stack address itself is not part of the serialized state. The patch accepts its relocation only for the exact four-record writer/length/reused-local sequence in both captures. Payload, including padding, is untouched.

## Writer inventory
All addresses below are module-relative writer RETURN sites (not call starts).

| Save / load | Temporary records | Return RVAs | Handling |
|---|---|---|---|
| 30EC90 / 30EDC0 | copied global scalar, 4 bytes | 30ED1F | Existing five-record owner-layout guard |
| 30A980 / 30AE80 | nine 16-byte reference descriptors | 30AB2F,30AB84,30ABDF,30AC34,30AC8F,30ACE4,30AD3F,30AD94,30ADEF | Existing exact group guard |
| 31DC00 / 31DE50 | four 16-byte global reference descriptors | 31DCF1,31DD48,31DDA1,31DDF5 | New exact group guard; record 656 |
| 301820 / 301B10 | two 16-byte global reference descriptors | 301A75,301AD2 | New exact group guard |
| 390690 / 390800 | two iterations of two 16-byte reference descriptors | 390765,3907B6,390765,3907B6 | New exact four-record group, including repeated writer sites |
| 317BD0 / 317C70 | sentinel and player index, 4 bytes each | 317C17,317C60 | Exact writer and length |
| 340ED0 / 340F70 | sentinel, 4 bytes | 340F59 | Exact writer and length |
| 341110 / 3411A0 | sentinel, 4 bytes | 341183 | Exact writer and length |
| 321750 / 321A30 | polymorphic type tag or absent tag, 4 bytes | 3217EC,321814 | Exact writer and length; native loader selects object using copied tag |
| 32AC00 / 32ACE0 | 16-byte reference descriptor; copied 8-byte field | 32AC76,32ACAC | Exact writer and length; existing pointer/loader verification retained |

## Audit scope and evidence
Targeted fresh headless Ghidra exports are in work/ghidra-sc6-focus/{temporary-writer-audit,root-writer-audit,temporary-load-audit,temporary-camera-audit,temporary-sweep}. Existing rollback-contract-audit includes the 32ACE0 loader. Full-function disassembly was inspected as well as pseudocode, including 321750's split unwind ranges.

The root 3841E0 writer, its static child writers, matching loaders, and previously exported camera/serialization functions were reviewed. A broader binary candidate sweep selected 108 unwind ranges in RVA 300000..3A0000 containing indirect +28 calls; Ghidra exports were checked for stack-local stream writes. Calls through global 470D188 are a different interface, not snapshot writes, and receive no exemption. The sweep is supporting coverage, not proof that every possible dynamic game serializer or future binary is covered. Unknown writers remain strict.

Direct globals, direct owned-object data, dynamic retained-object addresses and ring rotation checks are unchanged. There is no generic stack-address allowance and no record-index whitelist. No timer, ownership, round safety, source diagnostics or endpoint verification is disabled.

## Regression coverage
FullStateProbeTest exercises all added groups and individual sites, relocates temporary sources while preserving exact copied bytes, and reads the archived data into a fresh destination. Negative cases cover wrong module, overflow, wrong writer, wrong length, malformed or truncated groups, invalid sources, shape changes and invalid ring rotation. Existing arbitrary equal-byte/different-source rejection remains.

Local validation does not demonstrate a successful live correction; the next two-PC run is required for that.

Validation completed: Test-FullRestore.ps1 and full Build-Native.ps1 passed. Logs: build-native/temporary-source-tests.log and temporary-source-full-build.log. Installed DLL and friend ZIP DLL SHA256 both 14BF0C93922751FDA42726B2BBC35B99FDEA16AEE70AF41C44EA072F841E8D8A. Package: SC6Rollback-LivePrediction-Suite-20261018-224828-TemporarySources.zip.
