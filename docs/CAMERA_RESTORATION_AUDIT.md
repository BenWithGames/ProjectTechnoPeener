# Camera restoration audit — 2026-10-17

Scope: every camera-specific restoration helper currently wired into the replay runner, its native serializer/reset dependencies, and endpoint comparison. This is an audit of the supported implementation, not proof of every possible SC6 camera state or every indirect game call.

Executable SHA256: F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553.

## Evidence and method

Ghidra focused decompilation used PE exception-table function extents, including chained ranges where present. Machine-code reset/load listings were checked separately. Research names are annotations, not original symbols. The earlier address 324942 is a serializer return address, not a function entry; the containing writer is 324900. The audit re-decompiled that writer at its correct entry and loader 3249E0.

Research outputs under workspace `work/ghidra-sc6-focus`: `all-camera-audit`, `camera-base-audit`, `camera-writer-audit`, `camera-loader-audit`. Repository `work/camera-all-audit` contains decoded differences, machine code, and archived repair validation.

## Restoration inventory

| Path | Audit result |
| --- | --- |
| CameraSnapshot::capture / verify_live | Captures 456 base bytes per object using serializer provenance; separately captures smoothing/history, targets/counters and response for type 3E87EB0. Identity and live-memory checks retained. This is not a complete copy of every derived camera allocation. |
| CameraSnapshot::restore_smoothing | Validates first, then restores selector, smoothing scalar/history, target vectors, response, mode and decision counters. Validate-only does not write. Existing counter -1 reset handling agrees with native 31AC60. Existing mode domain and adjacency guards remain. No new broad exception introduced. |
| ImmediateIdleCorrection::apply | Queues all patches before writes. Existing type 3E881C8 reset-scalar restoration retained. Found and repaired the four omitted limits for type 3E885E0, described below. |
| ThrowCameraFlag::restore / restore_general | Exact serializer source/shape, type and live checks; only audited 0-to-1 normalization is repaired. Unchanged records have a no-write path. Rechecked native writer 324900 and loader 3249E0: loader preserves root vtable and backing allocation, reads the 41E0 pool, then invokes child loaders. Do not replace this with a raw pointer-block overwrite. |
| ThrowCameraFlag::corrected_record_matches | Revalidates corrected live bytes without changing raw evidence. Unchanged except for this audit documentation. |
| FullStateProbe / NativeAdjacentRestore | Preflight, native load, post-load camera validation, smoothing restoration, throw-flag handling, completion patches and recapture remain ordered. No diagnostic or round gate removed. |
| TwoFrameIdleReplay::cameras_equal and callers | Identity plus exact base bytes and captured extended state. The repaired limits remain included; no camera byte masks added. |

## Native camera type inventory

All six captured base-camera types share the common save/load pair 33FBC0/33FF40 and reset 33E640, directly or through derived serializers.

| Vtable RVA | Init/update examined | Save/load examined |
| --- | --- | --- |
| 3E87D90 | 3179E0 | 317BD0 / 317C70 |
| 3E87EB0 | 31A2B0, 31A490, 31AC60 | 33FBC0 / 33FF40 |
| 3E881C8 | 318640 | 318860 / 3188A0 |
| 3E88328 | 341010 | 341110 / 3411A0 |
| 3E88488 | 31BC50, 31BD50 | 31C190 / 31C1E0 |
| 3E885E0 | 3402C0, 3403A0, 340C00, 340C60 | 340ED0 / 340F70 |

Derived serializers cover additional fields outside the base capture. Common loading omits 144..157 and 188..1C7. Omission alone is not permission to overwrite the entire range: reset values differ between derived types, and several fields have separate native update behavior. Existing repairs and the new repair stay type-specific. Other unexpected differences still stop with evidence.

## Latest failure and correction

Both reports 62172 and 37016 completed nine cycles before restoration completion rejected camera record 5. The failing object had vtable RVA 3E885E0. Both contained the same four changed active float fields:

| Offset | Saved bits | Loaded bits |
| --- | --- | --- |
| 188 | 38A06EA0 | BE7FFFBC |
| 18C | 3D9CAC08 | 3E7FFD60 |
| 1B8 | 3FC00000 | 3F000000 |
| 1BC | 40900000 | 437A0000 |

33E640 establishes the angular limits; 3402C0 establishes the scalar bounds. 33FF40 and 340F70 do not deserialize these four fields. Camera update code consumes them for clamping. These are active state, not unused bytes.

The new completion branch accepts only the exact type on saved/next/loaded objects, matching owners, unchanged live evidence, finite archived values and the audited loaded defaults for changed fields. Every other byte must match. It queues four-byte writes from the saved snapshot; all queued restoration patches validate before any are applied. Exact post-write and endpoint verification remain in place.

The archived failed records on BOTH PCs become byte-exact after these four repairs. That validates the captured mismatch explanation, not the subsequent unperformed live replay.

## Validation and remaining limits

Focused FullStateProbe tests pass, including 13 new cases: success, each of four incorrect loaded defaults, unrelated changed byte, owner/type changes, non-finite source, smoothing-type confusion, stale live memory and exact negative-zero restoration. Failure cases verify no memory writes. Existing 225 mode-pair and decision-counter tests still pass.

This audit does not prove all camera behavior is restored under every move, stage or lifecycle transition. In particular, omitted ranges in other derived types remain exact-checked, external allocation lifetimes remain required, and the throw child-loader graph is not exhaustively re-proven here. A full match using this build is still required. No claim that future camera failures are impossible.

Additional native-loader observation: 317C70 reads a small selector and reconstructs its +1F0 pointer from the current player table, while resetting +1E8. 3411A0 and 340F70 discard a serialized word and clear +1E0. 3188A0 reads a 140-byte derived region at +1D0, and 31C1E0 reads its smaller derived region. These differences are why this patch does not indiscriminately copy all omitted or reconstructed bytes across camera types.

Full Build-Native.ps1 validation passed, including the focused restoration regressions and native DLL link. CameraAudit package generated; installation and live-match verification are pending.

## Follow-up: bilateral 66428 / 46296 selection failure

Both PCs failed first-cycle restoration at CameraSnapshot target offset 124 (object +2AC), type 3E87EB0. Saved=1, next=1, loaded=0. No replay hop ran, so this is not evidence of an RNG replay mismatch. Frame/round guards remain unchanged.

The previous audit overlooked active selection state that was already captured. Fresh Ghidra outputs in `camera-selection-audit` cover 19 update/reset/selection/serializer helpers. `camera-virtual-audit` decompiles all 74 distinct methods in the six captured camera types' contiguous virtual tables; `work/camera-virtual-inventory.json` maps their slots. PE unwind ranges define non-leaf bodies; short leaf bodies are bounded by their terminal return/tail jump. Decompiler output is checked against actual memory instructions for the failing field (`work/camera-selection-machine.txt`). These are focused native-camera paths, not every indirect engine path or every possible uncaptured camera type.

### Confirmed cause and repair
- 31A450 writes zero to object+2AC during initialization.
- 31A5E2 reads it as a two-player table index; the complementary player is selected with 1-index.
- 31A5F7/31A601 read selected player+23C and remember that index at +2AC.
- The native base save/load pair 33FBC0/33FF40 does not serialize +2AC. Our extended capture does, but the old restoration policy exact-checked the reset value and omitted its write.

The corrected opt-in camera decision policy accepts source/next indices 0 or 1 and only an unchanged or audited zero loaded value. It restores the saved +2AC after all identity, vtable, live-memory, history and field guards pass. It permits either legal adjacent selection, while exact replay endpoint verification still requires the native update to reproduce the next state. Validate-only never writes; values outside 0..1 and unrelated loaded values remain rejected.

### Related omission found proactively
Virtual methods 31BB90 and 31BC10 clear +1E8; 31BBA0 and 31BBD0 explicitly assign -1 or +1. Update31A490 interprets these as forced player ordering, distinct from the remembered index at +2AC. The previous special case admitted only stable -1 in one idle path. The opt-in decision policy now supports the proven 0/-1/+1 domain and unchanged/zero loaded provenance, restoring the exact saved word with the existing +1E8 write. No other target field comparison was relaxed.

### Broader audit disposition
| Area | Disposition |
| --- | --- |
| Six native virtual-table families | 74 unique methods decompiled; constructor/reset/save/load/update and scalar accessors mapped. Some leaf wrappers tail-call shared interpolation code. |
| Native base serialized vectors/scalars | Native save/load remains authoritative; raw evidence and exact endpoint checks retained. |
| +154, flags +1E0/+1E4, history +25C..2A3 | Existing proven reset repair retained; saturation/index bounds and active-fight admission unchanged. |
| +258 modes; +2A4/+2A8 counters | Existing 0..14 mode and -1 counter reset handling retained, including all prior regressions. |
| +2AC previous player; +1E8 forced ordering | Two confirmed omissions corrected as described above. |
| Special target/transition cache +1F0..220 | 31AF00/31B760/31B880 consume/update these during special-camera branches. They are captured, but unsupported reset/transition patterns still fail admission or exact validation. No blanket overwrite introduced. |
| Derived camera limits, pointers and child pools | Existing type-specific four-limit repair and native reconstruction retained. Native pointers are not replaced with archived raw addresses. |
| Render-matrix helpers 33E7B0/33E840 | Consume camera vectors/angles to produce caller-provided matrices; no new independently owned snapshot object inferred. |
| Runtime coverage | A decompilation is not proof every move/round/stage path executes identically. Live replay must still pass. |

### Validation
FullStateProbe regressions exercise all four previous-player pairs and all nine forced-order pairs, exact writes, validate-only behavior, invalid domains, unexpected loaded provenance, stale live data, opt-out, and no-write rejection. Existing 225 mode pairs and counter reset checks remain. The old synthetic capture-layout fixture contained arbitrary bytes at +2AC; it now initializes that field to a valid index before restoration tests, retaining its raw-layout assertions.

Archived target blocks from both reports match exactly after the existing six vector-word restorations plus the new +2AC restoration (`work/camera-selection-archived.json`). This verifies the observed rejection explanation, not an unperformed replay. Shared RNG caller tracing, deferred evidence, independent effect verification, all earlier diagnostics and round safety remain enabled.
