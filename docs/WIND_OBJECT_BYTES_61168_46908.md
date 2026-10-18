# Wind object byte audit: full-match runs 61168 / 46908

The initial investigation made no runtime changes. Its baseline DLL SHA256 was A8349893C27EEFD904CE735FCCD883627A3B306553AC2E45BD9B2192A657F17B. See the implemented comparison policy below for the subsequent change.

## Captured evidence

Ben's local run 61168 and partner ZIP SC6Rollback-Report-steam-inputs-46908-6d76e736.zip both stopped at replay 668 -> 669. Each reported restoration verified, three wind loads, eight completed wind comparisons, failure=4 at object 0 / offset 9. RNG and the checked player scalar values matched. No gate contention or evidence-writer errors occurred.

Both expected and actual effect lists contain nine objects. Only object 0 differs after the comparator's existing list-link relocation allowance. Its type RVA is 0x3E88CE8 and allocation size is 0x1E0.

| Side | Original object | Replay object | Raw differing bytes excluding list links |
| --- | --- | --- | --- |
| Ben | 0x22C5D54A400 | 0x22CCAB72E00 | 37 |
| Partner | 0x251E4D24C00 | 0x251E5A8B600 | 5 |

The new original object is absent from the first two saved list heads and appears in the final two. Replay recreates it at a different address, as supported by the existing lifetime/relocation design. The original future object is retained; replay does not have to receive its exact address.

The expected +08 qword is zero on both PCs. Actual is 0x22CCAB7E200 on Ben and 0x251E5A88E00 on partner. These look like process-local addresses, consistent with allocation residue. The exact allocator metadata interpretation has NOT been established. Partner's older original head already contained a nonzero +08 qword before this new object was born.

## Ghidra and instruction audit

Read-only focused Ghidra analysis, with explicit complete function ranges verified against PE exception fragments (rather than treating a single unwind fragment as the full function):

- 334960..334B78: emitter creation and partial object initialization.
- 333E80..333FC2: specific spawn setup.
- 332D60..333124: base wind setup / RNG initialization.
- 333130..333550: initial update, vtable slot +08.
- 333550..333C1D: per-frame advance, vtable slot +10.
- 333C20..333E80: spatial wind query, vtable slot +00; this is NOT a destructor.
- 331070 and 331080: flags +68/+6C, vtable slots +18/+20.
- 333FD0..334293: manager dispatch, list unlink/deallocation and wind-force aggregation.

Generated research names are annotations, not original symbols. Machine disassembly corroborates the relevant stores and operand widths. Constructor call 334A19 requests 0x1E0 from 4A61C0, which jumps to D4F760 with zero alignment argument. The subsequent constructor stores specific members, not a full-object zeroing. For example 334A5E stores only a WORD at +20, and 334B1A stores only a DWORD at +148. It does not write +08.

All observed differences are within these half-open byte ranges:

| Range | Nearby live fields / audited use |
| --- | --- |
| 08..10 | Gap between vtable and linked-list fields at 10/18 |
| 22..28 | Six bytes following the two-byte field at 20; manager pointer starts at 28 |
| 34..40 | Gap after the lifetime scalar at 30; force/output vector begins at 40 |
| F4..F8 | Gap after lifetime/config at F0; mode at F8 |
| 11C..120 | Gap after scalar parameters at 110/114/118; generated values start at 120 |
| 144..148 | Gap after reciprocal at 140; loop counter at 148 |
| 14C..150 | Gap after counter at 148; vector starts at 150 |

No reads/writes of those object-relative gaps were found in the complete audited initialization, advance, query or removal paths. Apparent assembly operands +8/+24/+34 in these functions also refer to stack slots, caller input vectors, manager storage, vtable entries, or the matrix alias object+160; they must not be mistaken for these object gaps. The inverse-matrix helper receives object+160, outside all candidate gaps. Dynamic setup/advance loops address arrays at 70..BF, also outside these gaps.

The per-frame path does use other members, e.g. +138, +148 and matrices. Uninitialized-looking data is NOT a general permission to ignore a field. The manager feeds this object's query result into character wind-force storage at +29310..+2931C, so ignoring the entire effect object as purely visual would be incorrect.

## Conclusion and limits

Offline classification places all 37 Ben differences and all 5 partner differences inside the seven audited gaps. All other compared allocation bytes across all nine objects match. This strongly supports a raw-allocation comparison false positive for these recordings, rather than missing RNG advancement or an emitter-countdown restoration failure.

Recommended next change: a pinned type-RVA/size-specific comparison policy for just these gaps, keeping full raw evidence and strict comparisons for all used fields, list shape, ownership, external references, emitter state and other effect types. Restoring retained object bytes does not need to be weakened. Regression coverage should prove every candidate gap is handled while adjacent active members and unknown types still fail, and replay the two captured comparisons offline.

This audit does not establish that every byte of every effect type is padding, nor prove every possible indirect consumer across the entire executable. It does not establish that subsequent replay checks or a complete match will pass after this narrow comparison change. No new live test or changed DLL was produced for this investigation.

Artifacts: work/wind-object-61168/comparison.json and analyze.py in the repository; focused decompilation under the workspace's work/ghidra-sc6-focus/wind-object-verified; PE-backed disassembly under repository work/wind-object-61168.


## Implemented comparison policy

EffectLifecycleWindow now excludes only the seven audited half-open gaps for
vtable RVA 0x3E88CE8 with size 0x1E0, after normal environment and topology validation.
Raw snapshots, evidence serialization and restoration bytes are unchanged.
The observer regression mutates every one of the 480 payload bytes independently,
checks combined gap differences, rejects an adjacent active field, and rejects
other types, sizes and module identities. Both archived failure image pairs pass
the production comparator; an added active-field mutation fails for each pair.
This resolves these recorded comparison differences, not proof of a full live match.
