# Saved replay 25084: blend producer trace

Offline investigation, 2026-10-13. No new live test or restoration change.

Evidence: `work/partner-25084-evidence.bin` and the derived blend-word JSON in the workspace. P2 blocks 0, 1 and 3 differ. Corrected input envelopes match the reference; this does not prove all animation inputs were restored.

The pinned executable's function RVA 2F90D0 provides a concrete producer for these differences:

- RBP is the wrapper, R13 is its second argument, and RDI is wrapper.owner + 0x1DD8.
- At 2F9439 it calls 3499D0 with R13 and stack output pointers when wrapper +0x38 is zero. The float output becomes XMM0.
- At 2F9450 it multiplies that value by wrapper +0x44. Block 0 +0x20/+0x24 receive its complement; block 1 +0x20/+0x24 receive the product.
- At 2F95CE it computes the unscaled complement. Block 0 +0x44/+0x48/+0x4C receive that complement; block 3 receives the unscaled value.

The saved block-0 values fit this relationship: unscaled complements are approximately 0.40938431 reference and 0.76939040 replay; scaled complements are 0.94093841 and 0.97693902. Both imply a multiplier near 0.1. This is consistent with a changed upstream blend parameter, rather than unrelated corruption of every output weight. It is not proof of where that parameter first diverged.

The wrapper +0x4C/+0x50 cached-value branch begins at 2F9A2C and targets owner +0x2098 (block 4), so it is not the direct producer of the observed block 0/1/3 pattern. Wrapper fields are already included in FullStateProbe's 96-byte measurement; +0x50 is among explicitly preserved scalars. Do not add a speculative copy of wrapper fields as a fix.

Next: trace 3499D0 and the second argument supplied to 2F90D0, then establish whether its source is serialized, reconstructed, or advanced outside replay. Preserve exact comparisons and the existing consolidated suite. The cause and restoration fix remain unresolved.

## Producer investigation

Pinned-executable disassembly of 3499D0 is saved in workspace `work/blend-parameter-source.txt`. It scans three slots at argument +0x97268, stride 0x80, selecting a slot whose +0x48 state is 2 and whose +0x4C matches argument +0x25E. It uses slot +0x38/+0x3C to select a descriptor through +0x08, and slot +0x40 as a cursor into the table through +0x10. The table supplies the output weight. Crucially, 349B02 adds slot +0x44 to that cursor and writes it back at 349B08. At the end threshold it resets the cursor, changes state to 1, and may invoke slot +0x58's callback. This getter has side effects.

The explicit FullStateProbe measurement ends at player +0x97248, before these slots. The saved evidence has no direct native source or destination coverage for any of the three four-byte cursors on either player in saved/expected/actual/restored streams (24 combinations; `work/blend-timer-coverage.json`). This does not exclude serialization through a temporary buffer or reconstruction elsewhere, and does not yet prove the second argument is the measured player in this execution.

The next concrete checks are caller provenance, serialization via temporary buffers, and cursor/state lifecycle. A blind 0x180-byte copy is inappropriate: these slots contain resource pointers and a callback. A restoration change must distinguish mutable scalar cursor/state from owned resources and callback side effects. No runtime behavior changed during this investigation.

## Caller and lifetime checks

The two direct calls found in the pinned .text section both use a player-relative wrapper and pass that same player as argument two: 2DCC66/2DCC6D/2DCC70 and 341F24/341F2B. This establishes the static player provenance for those call sites, though no dynamic call trace was captured in report 25084.

Slot setup at 349840 initializes the selected cursor to 3.0, writes the rate and selection, sets state 2, and resets other active slots to state 1/cursor 0. Player reset 2E213E..2E2174 likewise resets all three active cursors. Thus preserving the cursor alone may be insufficient when a replay crosses activation or completion.

Player construction creates three 0x80-byte slots using constructor 38B690. The constructor initializes cursor 3.0, rate 1.0, state 0 and identity -1. Destructor 38B6E0 releases a reference-counted control block at slot +0x68 (atomic decrement, destructor on last strong reference, then weak-reference release). Resource setup calls 38B740 from 2F7792. Any proposed restoration must retain/validate resource identity separately from mutable values; it must not memcpy these ownership fields.

Artifacts: workspace `work/blend-timer-references.txt` (direct-call and displacement reference scan), `work/blend-slot-lifecycle.txt` (constructor/destructor excerpt). The lifecycle excerpt's 2F7780 start is intentionally not instruction-aligned and must not be used as evidence; use the aligned 2F777E sequence in blend-timer-references instead.

No indirect serialization contract has yet been established. Absence of direct stream coverage remains a candidate omission, not proof that native loading cannot reconstruct it. The next implementation should first capture all three slots as diagnostic evidence at reference, restored, predicted and replay boundaries, while distinguishing pointer identity from scalar differences. Existing reports cannot recover those uncaptured bytes. Do not weaken replay verification to accept the animation mismatch.

## Paired evidence and guarded cursor restoration

Live reports 83400 and partner 21492 both captured the same failure on cycle 4 after three clean inconclusive retries. P2 slot 0 expected cursor 6.0 but the restoration/replay measurements remained 11.0. All six slots were readable, with zero dropped samples. The captured slot differed only at cursor +0x40; resource fields, rate and state were equal. Both had 515 changed bone-buffer bytes, 127 blend bytes, and no transform or camera differences.

The subsequent implementation requests timer restoration only for general-combat adjacent restoration. It retains the source snapshot's first timer sample, validates all six live slots before native load and again before preservation writes, and permits only cursor differences for state 2. Every other byte must match, all samples must be valid, and cursors must be nonnegative finite floats. It writes only +0x40 and verifies the complete slots afterward. It neither copies owned pointers nor invokes callbacks. State/resource transitions remain rejected. Evidence includes the source sample and restoration flags. Combat-suite signature revision is 4 so old builds cannot join this test.

This is a bounded fix for the observed cursor drift, not a general slot lifecycle restorer. Live correction success remains to be verified.
