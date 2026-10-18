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

## 2026-10-14 camera readiness investigation, report 68200

Cycle 1 completed three corrected replay comparisons without measurable divergence and retired. Cycle 2 rejected preparation (combat rejection 16, pass mask 125) before native restoration. Input equality bit 2 is explicitly exempt for recorded combat; the remaining missing bit is camera 128. Camera reason 14, record 1, packed target offset 120 maps to owner +0x2A8, with 26 -> 27.

Pinned native helper 31AC60, called at 31A4E5 with the camera as RCX, increments +0x2A8 at 31AD14 or resets it to -1 at 31ACF0 according to player flags +0x1732/+0x1733. It compares the counter against 6 at 31ACFB. The parallel counter +0x2A4 has corresponding increment/reset logic. Thus +0x2A8 is temporal decision state, not a resource pointer. Exact gameplay meaning of the flags remains unproven.

Artifacts in workspace work/: camera-counter-callers.txt, camera-selection-counters.txt, camera-2a8-references.txt. The first few instructions of camera-selection-counters start inside an unrelated instruction; the aligned helper entry 31AC60 onward is the relevant evidence.

CameraSnapshot already captures +0x2A4..+0x2AF, but its combat-readiness rule demands unchanged values and its restore writes do not include these counters. No direct native source coverage for record-1 owner +0x2A8 was found in this failure's saved/expected streams. There was no restoration on this attempt, so the empty restored stream cannot establish native load behavior. Do not merely relax readiness: restoration and post-load validation must handle these counters as well. A narrow increment-only contract should reject reset, wrap, selection changes and unrelated bytes until separately supported. No runtime guard changed during this investigation; timer-restore build remains installed. Baseline timing remains the user's preference.

## 2026-10-14 guarded camera-counter implementation

General combat now opts into decision-counter handling in both readiness and restore validation. Packed offsets116/120 (owner2A4/2A8) permit unchanged words or a nonnegative signed-range +1 increment. Changed reset-to--1, negative-to-zero, overflow and jumps remain rejected. Post-load values must equal the captured source or adjacent value and match a fresh live read; all existing owner/vtable, selection, payload, smoothing and response guards remain. After all checks, only the two four-byte counters are copied from the source and the existing complete target-range verification checks the result. No resource pointers are written. This runtime load check is deliberately fail-closed; no claim is made that all native loading paths preserve these fields.

Audit of nearby temporal fields: camera smoothing count/index (29C/2A0) and smoothing history were already rewound; animation slot cursor restore remains enabled. Camera2AC is a player selector, not a timer, and remains equality-guarded. Animation slot rate/state/callback/resource fields remain strict identity/state guards, not generalized timer writes. Further unobserved timers are not assumed covered.

Regression coverage adds both counters26->27, default-policy rejection, third-loaded-value rejection before writes, validate-only no-write, successful rewind, reset/jump/overflow rejection. Suite protocol revision5 separates this build from timer-only revision4. Baseline remains100ms with a separate immediate launcher. Live verification is pending.

## 2026-10-14 pre-restoration peer-stop cleanup

Reports local60856 and partner28828 diverged before replay: local transform-topology readiness rejected after four captures; partner had three captures and confirmation hold state5/reason1 after110ms, no restoration or injection. Partner invalid-context reason previously combined native validity with worker stop/runner failure, so the report cannot prove which constituent failed.

Cycle run_hold now separates core native context from administrative stop. A stop/runner/exchange failure permits cancellation only at runner stage0 with restored/injected/restart all false; the native hold still checks identity, frame and hold.ready() before returning the original dispatch. Resource cancellation, stale observations, thread/object mismatch and post-mutation failures remain fail-closed. The existing safe neutral cancellation is retained. This does not convert transform rejection into success or retry it.

Entry/confirmation diagnostic context_mask bits:1 invalid observation,2 object mismatch,4 current-thread mismatch,8 owner mismatch,16 stale/future observation,32 retired,64 resource-preparation failed,128 resources cancelled,256 resources not ready,512 stop requested,1024 runner failed,2048 exchange failed. Cancellation reports the existing neutral_cancel reason6 even for clean administrative stop; context bits distinguish the trigger. Regression cases cover pending peer stop, mutation/restart exclusion and invalid-context cancellation rejection. Combat signature revision6 requires both PCs to update. Live resolution remains unverified; baseline timing retained.

## 2026-10-14 topology attribution from existing report60856

The existing consolidated report resolves the structural change without another live test: cycle1 preparation stage3/rejection11, topology mask6, record count2053->2055, removed global nodes0, added1. New node2024031902208 has vtable RVA65572072 (0x3E88CE8), next2024331166464 (the prior first mismatching node), parent140700151450464. Its single captured child is kind24 at node+24, size456. Thus a new global wind/effect node was prepended; its24-byte header plus456-byte payload accounts for both additional records. A disappearing old pointer is not what this report shows.

Existing CATCH_UP_RESEARCH allocation tracing identifies this exact type's 0x1E0 allocation at334A19 and prepend operations334A6B..334A7E. This supports object creation/list insertion as the reason for the topology rejection. It does not identify which player move/event caused the insertion, establish visual-only semantics, or prove lifetime restoration safety. No restoration ran on this attempt. No new DLL is required to capture these fields: current suite already records node address, type, links, child details, and added/removed counts. Any deeper causal tracing should target the known creation path and record game frame/caller/input context rather than adding another generic mismatch counter. Keep existing topology guards and cleanup fix.

The accompanying passive wind capture declares coherent_snapshot=false and has389 samples with errors of720; do not use it alone to infer exact frame ordering or object absence.

## 2026-10-14 wind lifecycle implementation and suite extension

Added EffectLifecycleWindow over the existing generation-tracked EffectRetentionRuntime. The bounded owner-thread transaction holds consecutive reference images/pins, restores earlier/later list membership through the existing gated writer and external-reference callback, verifies replay images using ordered type/full-byte payload equality, and explicitly retires every pin. Replay allocations may have different addresses: only validated next/previous list links are excluded from the semantic comparison; no scalar, non-list pointer, flag or padding exceptions are added. Shape validation checks full linked-list topology before comparison. Failed reference validation performs no writes. Partial native writes retain the runtime restart latch; cleanup cannot falsely report success.

The existing EffectUpdateObserverTest (already in Build-Native) now covers exact CE8/0x1E0 creation -> capture -> rewind/detach -> revive -> rewind -> recreation at a different address -> semantic verification -> exact-once release. Also covers counter mismatch, broken linkage, bounds, external-reference refusal without writes, and rematch/frame-reset rejection with pin cleanup. Existing runtime tests cover native removal/revival, unexpected free, stale generations and write faults.

Consolidated Summarize-TestSuite now appends Wind object lifecycle rows for combat profiles and structured windLifecycleEvents from existing topology evidence. It explicitly says native lifecycle restoration is not connected, and does not infer peer success from transport/correction bits. Test-TestSuite checks report60856-shaped addition evidence and that an unconnected stage cannot pass.

IMPORTANT: this is not a native combat integration release. Production still has no external-reference verifier for full wind images, no published lifetime runtime spanning the captured game window, and the native serializer/replay path still requires matching transform topology. These are concrete integration prerequisites; do not enable the route or relax same_topology to claim completion. The controlled memory tests validate the list-lifecycle core, not arbitrary SC6 wind objects, native replay duplication behavior, stage worker synchronization or latency improvements. Installed CleanStop DLL remains unchanged. No new live test or ZIP requested at this point.

## Native wind snapshot integration (2026-10-14)

The owned combat snapshot path now captures a bounded NativeEffectSession binding alongside each NativeBoundarySnapshot. Dynamic wind nodes are removed from the fixed transform manifest only when this owned binding is present; player transforms and the manager's non-list fields remain checked. Reverse loading applies the retained wind list before native loading, and every replay comparison checks the complete wind allocation bytes separately (except validated list-link addresses when allocation relocation is legitimate).

External reference validation uses strong resource ownership extents, exact saved resource bytes, query-root identity and bounded traversal of cell/interval/candidate pointers. It does not infer ownership from readable memory. Only the known module-resident manager/scratch configuration is supported. Changed stage resources, unowned graph pointers, unsupported types and capacity/thread/generation violations reject restoration. This is bounded known-type support, not arbitrary object resurrection or support for changed breakable-stage resources.

The release route is published only for the owned session and withdrawn with callback quiescence. Snapshot pins retire before their external stage references. Comparison-token cleanup is retained on failure. Allocations born and freed before a snapshot are not treated as dangling retained objects.

The existing Evidence suite gains native wind capture/load/compare/retirement counters and failure/object/offset fields; failure bundles include wind.lifecycle saved/current images. Its lifecycle row requires an actual list change, successful comparisons and complete retirement; transport PASS and no observed change do not prove wind lifecycle restoration. The 100ms baseline and separate immediate runner remain unchanged. Protocol revision 7 requires both PCs to update.

Local tests cover owned external graph validation, revoked ownership, changed payload/root, out-of-range candidates, native snapshot binding/pool retirement, new-node removal/revival, relocated replay, transient unrecorded allocation release, and preservation of player/manager transform guards. No live gameplay validation has occurred for this integration yet.


## Stage-reference graph budget correction (2026-10-14)

Run 52308 stopped before wind capture/restoration with failure 105 (external graph validation). Peer 38264 did not reach wind capture. Both retired resources; gameplay remained normal. Read-only inspection of the still-running local process found selector 0, 64 columns and 16,350 interval/candidate visits, with only 325 distinct candidate objects. The old cap of 8,192 counted repeated shared references and therefore rejects this graph. This demonstrates a concrete validator defect, although the old report did not identify the precise first failed branch.

The bounded cap is now 65,536; resource-ownership extents, complete candidate extent and signed column limits remain enforced. The native query's explicit inactive OR null-descriptor early return is matched. Every graph rejection now records check ID, visits, column, interval, candidate, address and extent in the existing suite report and collector. Checks: 1 root read, 2 descriptor ownership/read, 3 column limit, 4 column pointer read, 5 cell header, 6 interval table extent, 7 interval budget, 8 interval header, 9 candidate table extent, 10 candidate budget, 11 candidate ownership/read. No pointer is accepted merely because it is readable.

Regression fixtures exercise 17,280 visits with shared references, a bad candidate inside that dense graph, hard budget rejection at visit 65,537, and an active null-descriptor root. Existing lifecycle, ownership-revocation and payload-change tests remain. Protocol revision 8; both PCs update. Live restoration remains unverified; this fix does not claim every stage graph will fit the new bound or that later replay checks will pass.

## Camera validation evidence (2026-10-14)

Runs 76964 and peer 39016 cleared stage-reference validation. A no-divergence three-frame prediction/correction retired, followed by a later failure at FullStateProbe's post-load camera smoothing validation (pass mask 127). Both required restart. Wind capture/load/compare counters were successful but no list addition/removal was exercised.

The validation-only camera call now retains RestoreDiagnostic instead of passing nullptr. Existing suite cycle reports append camera_restore_line/record/section/offset/saved/next/loaded/live, without changing prior diagnostic indices. Failure bundles add camera.source, camera.loaded and camera.restore_diagnostic alongside the existing expected/replayed states. The analyzer decodes the new diagnostic and remains compatible with older bundles. Smoothing reset and progression rejections now record their specific offsets/values; section 8 = reset flags, 9 = reset payload bytes, 10 = counter progression. Validation decisions, restoration writes and timing remain unchanged.

A regression test checks a rejected smoothing-counter transition reports the expected source/next/loaded values without applying restoration. Full native build, suite tests and analyzer compatibility checks are required before packaging. Both PCs need protocol revision 9. This is diagnostic coverage, not a camera restoration fix.

## Fresh startup clock observation (2026-10-14)

Local 68968 and peer 16316 did not reach corrections. Rechecking all 120 startup rows identified exactly one differing peer row: battle 175 was paired with cached clock 117 instead of 118. Adjacent rows were (117,174) and (119,176), producing two discontinuities. Earlier interpretation that all rows matched or discontinuities occurred after the recorded window was incorrect; StartupBoundaryPairs freezes after 120 entries.

NativeClockDiscovery previously inspected the live object but could skip cache publication when TryAcquireSRWLockExclusive failed; NativeClockHook then read the cache back, accepting values up to 250ms old. This mechanism can produce the observed stale row; the old run did not log lock contention, so it is not proven as the historical cause.

observe now returns the fresh local observation regardless of cache publication success, and the game-thread startup consumer uses that return directly. Cross-thread readers retain the existing nonblocking cached interface. Invalid inspections remain invalid; StartupBoundaryPairs and StartupBoundaryAgreement discontinuity guards are unchanged. No inferred/interpolated clock values are used.

A deterministic two-thread test holds the shared cache lock across observe, proves the cache remains 117 while the returned value is 118, verifies fresh pairs 117/174, 118/175, 119/176 have no discontinuity, and verifies a genuine repeated clock still increments the counter. Full build and suite checks required; protocol revision 10. Existing camera diagnostics and 100ms baseline remain included. Live confirmation pending.


### Timer compatibility correction (2026-10-14)

The 78624 / 1772 failure isolates the timer readiness branch by elimination; the exact live failing timer bytes were not retained, so no particular field is claimed as the root cause. Split captured source-to-next compatibility, pre-load live-next validation, and post-load live-source validation. General all-hop preflight now rejects unsupported timer transitions before any wind/native restoration. Per-hop preparation repeats the check and validates the live endpoint before wind restoration. Only the existing validated cursor write remains permitted; pointer, slot-state and non-cursor changes remain rejected.

Failure bundles now retain source, next, live and phase/reason/side/slot/offset/expected/actual timer diagnostics. This prevents a known incompatible captured pair from first being discovered after wind mutation; it does not establish support for arbitrary animation-slot transitions or eliminate unrelated freezes. Baseline timing remains 100ms. Live validation pending.


### Reference-first testing method (2026-10-14)

User requested the six-step workflow. Combat correction now requires original-input replay against the same immutable reference before any wrong-input injection. Successful reference replay recycles only validated restore workspaces while retaining snapshot ownership, then performs the controlled prediction/correction. Failure in the reference stage prohibits injection. Three bilateral changed-prediction corrections are required, with the existing 12-attempt budget and 180-frame spacing; neutral, no-divergence and clean pre-mutation rejection windows remain inconclusive. Baseline minimum hold remains 100ms.

The only new deferred intermediate comparison is group3 inactive weights: 15 equal positive sub-threshold floats at player+970C0, zero mask, preserved valid slot and identical control at +2030, exact pinned writer/reader/source identity, loaded weights zero. Native 349290 clears them; 3495B0 rebuilds before 2F5E60 evaluates the mask. Endpoint native comparison remains exact. Unknown differences and active/invalid groups still reject. Both source/loaded raw payloads remain in failure bundles. Prior run 78908/37944 had the same 60 bytes, with control 34AEB852, slot20, mask0, unchanged adjacent reference weights.

Reports distinguish reference replay, prediction correction, and stop outcome. Unsafe post-mutation stopping is explicitly a failed recovery result, not claimed safe continuation. General recovery and cross-round correction are still unavailable; round/rematch observations do not certify those paths. The new workflow exposes these gates rather than labeling the current controlled suite full-match continuous rollback. Live validation pending.


### 2026-10-14 camera +258 restoration
Both 77700 and 31376 cycle 6 rejected camera record 1, target offset 112
(owner+0x258): source=next=1, loaded=live=0, identity stable.
Pinned initializer 31A2B0 writes QWORD [rbx+254]=0x3F800000 at
31A3DD: low float is 1.0, high DWORD (+258) is zero. Update 31ADB0
can retain previous +258 at 31AEC3, so treating it as disposable is unsafe.
Added general-combat-only admission for stable saved 1/1 and loaded zero,
then saved-field restoration after all existing validation. Unknown values,
changing source pairs and live-memory differences remain rejected.
Reference-first replay remains exact; live validation pending. Tests cover
validate-only nonmutation, wrong next/loaded rejection, opt-in and writeback.


### 2026-10-14 corrected camera/throw flag verification
81712 and partner 10260 cycle 6 both passed camera validation (mask 255)
then rejected record 1044, length 0x41E0, offset 0x1350, saved 0 / captured
loaded 1. FullStateProbe corrects the live flag before NativeAdjacentRestore
compares its older `restored` capture. The raw capture is intentionally retained.
Native comparison now accepts this specific normalization only with completed
restoration, successful ThrowCameraFlag verification, matching source and pinned
writer/vtable, exact 0->1 DWORD pattern, all other captured bytes equal, and a
fresh full-record live comparison against saved bytes under exception handling.
Unknown captured differences, live differences and missing verification reject.
No replay endpoint checks were weakened. FullStateProbe regression exercises
real flag correction, stale evidence preservation and negative live checks.
Live verification remains pending.


### 2026-10-14 RNG caller diagnostic
5756/37424 cycle8: original frame1914->1915 seed stayed31886133;
source/load-preservation/restored-save samples confirm31886133 before replay.
First reference replay reached229734901: exactly four16807/mod2147483647
advances. Both player scalar arrays matched; prediction never began.
Added pinned34F550 no-argument float helper hook, result/LastError preservation,
bounded8192-event rolling trace with sequence/overwritten/contention counts.
Includes caller RVA, thread, frame, seed before/after and thread-local scopes:
0natural/unclassified,1restore,2reference,3prediction,4correction.
Failure evidence embeds rng.callers; analyzer decodes without dereferencing.
Coverage is helper-only: inline writes remain unobserved, so zero events cannot
prove no RNG consumption. No seed correction or replay acceptance changes.
Local tests cover invoke once/result/error preservation, phase nesting,
capacity/sequence and dropped records; analyzer valid/malformed tests pass.
Live caller comparison pending.


### 2026-10-14 RNG observer ABI regression and assembly fix
Loading crash PID72332, exception C0000005 reading0x1F40 at game RVA2FA1B5.
No correction cycles started. Pinned caller2FA080 keeps object inR9,
mode inR10, zero inR11 across call34F550. Original leaf preserves them;
a standard C++ replacement does not. New bridge calls the trampoline directly,
saves its resulting volatile GPRs/XMM0-5/flags/MXCSR/x87 control, calls the
observer, then restores exact outputs. Seed/frame sampled by direct loads;
RA/caller preserved separately. No C++ call before native helper.
Regression compares unhooked native stub against wrapper and1000 actual
MinHook-trampoline calls with an observer deliberately clobbering volatile
registers, flags and FP controls. Output/state equality and once-only invocation
passed. LastError preservation remains in observer callback and core test.
Protocol bumped; both PCs need new package. Live loading validation pending.

## Shared-state preflight evidence (2026-10-14)

The Reversal Edge attempt failed before the next native load: preflight bit 16
was absent on both peers. The old wind detail described an earlier preparation,
not this rejection. `combat_replay_preflight` now passes a dedicated diagnostic
into the shared-state validator. Failure evidence retains the predicate, source
line, record/kind/address/size, first byte offset, and saved/next/current/live
bytes. A validity bit distinguishes unobserved live memory from a zero value.
The bundled analyzer decodes this separately from the existing restoration
and RNG evidence. Protocol revision 0x11 requires both peers to update.

Acceptance conditions, restoration writes, timing, and retry policy are unchanged.
Tests exercise next/current and current/live rejection with exact recorded values;
the analyzer fixture verifies decoding, unread-live handling and size validation.
The next live run is needed to identify the actual rejected shared record; this
change does not claim to fix the freeze.

## Signed-zero investigation: local 69724 / peer 15652 (2026-10-14)

Both cycle-3 evidence bundles contain exactly one native payload byte difference:
record 356 length 0x370, offset 0x1CF. Word +0x1CC is 0x80000000 in the
original and 0 in replay. Both shared-state preflights and native restores pass;
RNG equals 918405611 at source/next 1023/1024. Cycle 2 original-input replay
passed and its no-divergence prediction was cleaned up, before cycle 3 failed.

Static pinned-executable trace:
- Save return RVA 30AA80 follows transfer of player+45230, length370.
  Thus differing float is player+453FC, slot2 of five B0-byte blocks,
  subobject+6C. 2FDB50 binds player+45390 into animation group1's first
  pointer; load30AE80 rebuilds that pointer at30B13F/30B14D.
- 306530 computes subobject = player + (slot+649)*B0. It calls2F54C0
  for a four-component animation-motion vector, scales all four components
  by player scale and optional slot scale, keeps it in XMM11, and stores
  16 bytes to subobject+60 at306A0D (also306844 on one branch).
- The fourth component is read at30687B (SUBSS from subobject+6C), then
  weighted and retained in the temporary vector's fourth lane. The examined
  position update uses the first three components; this fourth lane is not
  a pointer, camera flag or RNG word. It is not justified to call the whole
  field padding or universally irrelevant.
- 2F54C0 initializes output W=1, then obtains an animation sample via2E7780
  and scales its fourth component at2F55F5. 306530 applies further scaling.
  This supplies plausible arithmetic sites for signed zero; the evidence
  does not contain the intermediate operands that would identify which site
  diverged. No exact producer/root-cause claim is made.

No acceptance relaxation, game writes or new live test performed. A full
later-frame equivalence claim remains unproven because exact comparison stopped
this replay at the mismatch. Next useful diagnostic is original/replay values
at motion-vector sampling/scaling, with slot and operands, rather than allowing
all signed-zero differences globally.

## Bounded motion signed-zero comparison experiment

Replay comparison now allows only a sign-only zero difference at record offset
1CC (player+453FC). Requires general-combat mode, completed restoration, length
370, writer30AA80 on both records and the same known player+45230 source.
Neither saved payload nor game memory is modified. Nonzero/NaN values, other
fields, other writers/sources and post-load restoration comparisons stay strict.
Reference replay retains accepted-event counts across workspace recycling;
report fields motion_zero_* expose count and last record/value. Failure evidence
keeps raw payloads and a decoded motion.signed_zero section. Protocol12 requires
both peers to update. This is a bounded diagnostic experiment, not proof that
all later frames are equivalent or continuous rollback is ready.

## Camera +258 value-domain investigation (2026-10-14)

Both reports 88304/26160 stopped with saved=next=2, loaded=0 at targets+112,
which is EBV camera owner+258. Existing exception accepts stable1 only.
Pinned code establishes a broader data-derived domain, rather than a counter:
- 391088 reads a byte from animation/event data +F, masks with F at39108D,
  rejects F at39109B/39109E, and stores the remaining 0..14 at global context
  RVA470E890 + playerIndex*4 + C8 (3910A0). The D8 flag gates replacement.
- 38E227 and38E2A5 clear these per-player values during context initialization.
- 31ADB0 reads both indexed values, substitutes zero when context is inactive
  or distance exceeds its threshold, takes signed max at31AE9F..31AEA8,
  and stores camera+258 at31AEC9. For selected-player animation field+4450E
  nonzero,31AEC3 reuses the camera's old value instead. This explains why
  blindly accepting native reset-to-zero would lose state.
- Constructor/native-reset31A3DD writes QWORD+254=3F800000, zeroing+258.
  Thus the observed reset2->0 is consistent with initialization, just as1->0.
- The established producer/selection/retention path is closed over0..14.
  F is a source sentinel, not an admitted camera value. This is a static domain
  argument, not live coverage of every mode/asset nor proof of no alias writes.
  Other classes also use offset258 for unrelated fields; do not generalize by
  offset alone. No restoration code changed in this investigation.

Proposed bounded policy: for the exact known camera type and general-combat
path, validate both saved and next mode values within0..14, validate the observed
native reset pattern, then restore the saved value (not max or next). Preserve
all existing owner/topology/fresh-read guards and reject15/out-of-range values.
Tests should cover all225 saved/next pairs, reset/unchanged cases and invalid
sentinel/negative/large values; the exact transition policy remains to implement.

## Camera mode-range implementation

Implemented camera_mode_pair(0..14) in combat readiness and restoration.
Loaded value must be zero (native reset) or unchanged saved value; next value
alone does not authorize a changed loaded state. Existing exact camera identity,
fresh live reads, topology and all-before-write checks remain. Field+258 writes
only the saved value after validation. Existing camera diagnostics retain both
snapshot values, loaded/live values, offset and rejecting source line.

FullStateProbeTest exercises all225 pairs through validate-only (asserting no
writes) and actual restore (asserting exact saved mode). Invalid15,16,INT_MAX,
0x80000000 and UINT_MAX on each snapshot side reject without writes. Existing
opt-in and unexpected loaded-value tests remain. Protocol13 requires both PCs
to install. This extends the pinned EBV field only, not every object at offset258.


## 2026-10-14: guarded timer completion restoration

Both 86208 and 11672 cycle 5 rejected P2 slot 0 at +48: state 2 to 1.
The only slot changes were cursor +40 (56.0 to +0) and state +48 (2 to 1).
The completion callback at +58 was null and all ownership/resource bytes matched.
This matches the native completion/reset pattern described above; the specific caller
was not dynamically traced.

Permit this pattern only for source transition and restoration checks: positive finite
source cursor and rate, exact destination +0 cursor, state 2 to 1, null callback,
and every other slot byte unchanged. Newer endpoint validation does not gain this
exception. Validate all six slots before writing cursor +40 and state +48; verify the
complete slots afterward. Never rewrite callbacks, pointers, or reference counts.
Tests cover actual memory restoration, reverse activation, callback halves, resource
identity changes, invalid cursor values, and a late-slot rejection before any writes.
This is a narrow bounded replay change, not general completion-callback undo or proof
of continuous rollback. Prior raw timer diagnostics stay in the consolidated suite.


## 2026-10-14: base-camera +144 investigation (no restoration change)

Reports 87428 and 37760 both fail completion section 5, record 0, offset 324,
line 96. Source versus native-loaded camera record differs only at DWORD +144
(0x3DF439C0 = 0.11925077438354492 to +0) and +154 (0x3FE1D63F to +0).
The existing completion patch admits only +154; +144 therefore rejects.

Pinned executable static trace: base reset 318640 zeros +144/+148 at 318832;
update 318960 resets +144 at 319802 on its reset branch. At 31983C it reads
+144; computes target minus current, compares absolute delta against 0.1,
then if above threshold adds delta * 0.04 for negative delta or * 0.06 for
positive delta and writes at 319884. At 31988C it adds +144 to the camera
quantity in xmm12. Target is normally zero, or xmm12 * table[index] - xmm12
when index >= 1 and xmm12 < 4.5 (31980C..319837). Index is selected from
per-player global values at 470E958 and may retain camera +2FC. Table starts
3E84410 with 1.0, 1.05, 1.15, 1.30; adjacent words are not proven table entries.

This is a history-dependent floating-point smoothing offset, not an integer
side/mode enum. No fixed clamp is applied to the stored offset by this path.
The complete runtime index/input domain has NOT been proven; therefore no
claim that every finite bit pattern is reachable or that a numeric interval
such as 0..1 is authoritative. NaN/infinity are not justified valid states.
A prospective restoration should copy the exact saved finite scalar only for
the pinned base-camera identity and observed native-reset pattern, retaining
all other identity/live-byte checks. Do not use broad integer acceptance or
infer safety for other classes sharing offset +144. Direct operand scan and
focused disassembly retained in work/camera-144-references.txt and
work/camera-144-update.txt. No DLL or restoration behavior changed here.


### Saved-value camera offset restoration

Implemented the requested saved-value policy in ImmediateIdleCorrection: exact
finite archived +144 bits are restored when the pinned base camera's native loader
reset them to +0. No guessed lower/upper range or next-frame value is used.
The unchanged-value case needs no patch. Existing +154 restoration remains, with
same owner across all snapshots, pinned vtables, no smoothing subclass, exact
captured/live match and every other byte unchanged. The complete patch list is
validated before writes. Existing completion diagnostics remain in the suite.

FullStateProbe tests restore the observed 0x3DF439C0, negative one, negative zero,
smallest/largest positive subnormals, smallest normal, and both finite extremes
bit-exactly. Tests reject infinities, quiet/signaling NaNs, arbitrary loaded changes,
neighboring changes, wrong owner and smoothing-class mismatch without writes.
Protocol revision 0x15 requires both peers to install the new build. Live verification
remains pending; tests establish patch behavior, not reachability of all tested floats.


## 2026-10-15: retry scheduling after inconclusive windows

12296 and peer 17824 each completed five reference/prediction/correction windows,
all with no measurable divergence and verified cleanup. They already retried those
windows. The actual stop was attempt 7 entering an invalid clock at round end
(context mask 1, clock 0, both holds rejected); no restoration occurred in that
attempt. Generic restart wording was misleading (restart_required=0).

Combat suites now negotiate fresh clock/battle targets between attempts using owner-
thread paired observations (four adjacent samples, age <=250ms), authenticated
proposal/ack/commit messages, cycle-isolated packets, and both peers' retired results.
Targets are 600 frames ahead of the proposing observation; no old round offset is
reused. Unstarted rejected holds retire on the owner thread before publishing a safe
skip. Each new cycle establishes its own round key; snapshots never cross rounds.
Failed resources/cleanup or post-mutation failures still stop. Bounded 12-attempt
and session time budgets remain: exhaustion is inconclusive, not correction PASS.
Fresh-pair protocol is version 0x16. Live verification pending. New tests cover
bilateral agreement across changed identity, no activation from stale samples,
late/future-cycle packets, owner retirement, and unsafe restart rejection.


## 2026-10-15: retired-cycle round guard; retry experiment paused

Both reports 39612 and 40660 show attempt 2 reference replay and restoration passed,
three predicted/corrected frames completed, stop_outcome=1 and retired=1. The later
round change re-entered CombatResourcePreparation::replay_boundary and overwrote the
terminal attempt with failure_reason=8. No unsafe restoration was reported.

Return before round checks/resource access for retired, failed, or restart-latched
cycle runners. Keep the same-round rejection for still-active runners. A regression
test drives the actual dispatcher with a completed old-round runner and a new-round
key, checks unchanged terminal evidence, then verifies an active old-round runner
still rejects/cleans up before any resource access or native call.

User requested moving away from this repeated synthetic prediction test; do not
schedule another RoundRetry match as the next milestone. Preserve the suite and
its evidence rather than deleting the research. Next-milestone clarification asked:
targeted object retention discussed earlier versus continuous combat rollback.


## 2026-10-15: targeted retention milestone selected

User selected targeted object retention, not continuous combat rollback. Existing
EffectRetentionRuntime/NativeEffectSession already support four pinned wind/effect
allocation sites, snapshot pins, native unlink/free deferral, list/payload revival,
and external stage-resource validation. Do not claim this is a new retention design
or extend free suppression to unknown object types.

Added thread-safe cumulative lifetime evidence to the existing reports:
suite_retention_native_deferred, snapshot_protected, allocator_releases, revivals.
These distinguish natural native release deferrals from replay's list-change counts.
A snapshot-protected event requires an existing ledger pin at the actual native
release interception. Revival counts only an object that was retired before the
successful image/list restoration. Allocator releases count actual forwarded cleanup
calls, not queued requests. Zero events means not exercised, never PASS.

Local runtime tests verify a pinned native removal does not call the allocator,
expiry releases once, duplicate expiry does not double-free, restoration revives
original memory/list membership, and the replaced object is collected once. These
are isolated native-memory fixtures, not a live combat lifetime proof.

Next live runner must isolate snapshot retention/expiry and natural creation/removal
from synthetic wrong-input prediction, with explicit lifecycle-event coverage and
stage dependency validation. No retention-specific live runner was armed or installed
in this change; the old RoundRetry procedure remains paused by user request.


### 2026-10-15: dedicated TargetedRetention suite wired

Profile 11 selects a capture/retire-only path. It uses NativeEffectSession bindings,
not full combat serialization, prediction, native load, replay or RNG caller tracing.
Up to 12 windows capture four effect images each across three frames, separated by
60 frames. External stage references remain validated and owned by the existing
resource session. A round identity change terminates the window and releases its
bindings; terminal evidence remains immutable. No arbitrary classes are retained.

The suite exchanges the retention lifecycle stage bilaterally. Passing requires
at least one intercepted native removal while snapshot-pinned, successful owner
cleanup, equal acquired/retired effect snapshots, and equal deferred/actual native
frees. Missing events are SUITE_RETENTION_NOT_OBSERVED, not PASS or a correction
failure. Revival is intentionally not exercised by this capture-only profile.

Local coverage includes 12 bounded windows, capture failure cleanup, round-change
cancellation, terminal immutability, profile separation and missing-event summaries.
The earlier ledger/runtime fixtures cover delayed frees, revival and exact release.
Live coverage remains pending. Warmup/lobby initialization is still required.


### Targeted retention coverage revision 2
Both first live reports (Ben 15452 / partner 2408) completed 12 windows and 48
snapshots, with 23 deferred/final native releases and zero snapshot-protected
removals each. User confirmed normal gameplay. This is cleanup evidence, not
proof of survival past native unlink.

Increase to 300 consecutive four-snapshot windows (1200 sampled frames, about
20 seconds at 60 Hz), ending sooner on round change. Every window still releases
all bindings at its fourth boundary: maximum hold remains three frame advances,
with at most four bindings. The next window starts on the following frame; no
reference spans windows. The old 60-frame wait is removed. No wall-object support,
restoration or prediction is added. Coverage revision is included in reports and
handshake signature so peers cannot silently mix old/new schedules. Local tests
check every captured frame, every four-frame release and the finite stop budget.


### Retained object reference-replay stage (profile 12)
The coverage run passed bilaterally: Ben 29144 / partner 27836 each reported
35 snapshot-protected removals, 44 deferred/final native frees, and 1200 captured
and retired snapshots, with normal gameplay. Revival was deliberately not run.

New separate suite profile captures at most 32 four-frame general-combat windows.
An object present in an owned earlier effect image but absent at the recorded
endpoint gates restoration; generation and dependency validation remain mandatory.
No-removal windows are discarded before mutation and retried with fresh snapshots.
A round identity change ends the attempt and releases bindings. On removal, native
state/effect lists are rewound and the three ORIGINAL recorded-input frames replayed.
PASS requires a positive revival counter delta, verified reference replay, and
completed cleanup on both peers. No prediction/no false input injection is used.
The existing full mismatch diagnostic array and failure binary evidence are emitted
under suite_retained_replay_* and a report-stem retained-evidence.bin file.
Unsafe restoration/replay failures request restart and stop further updates. This
is not continuous rollback and does not add support for wall objects. Live proof
of restored-object reference replay is pending; bounded retention alone is proven.

## 2026-10-17: slot-1 sample and delta W signed zeros

Live EndState reports 47160 and partner 45096 stopped after restoration succeeded,
with endpoint mask 511 and native record 8 (0x370 bytes, writer RVA 0x30AA80).
Ben differed only at sign bytes 0x11F and 0x12F; partner only at 0x11F.
These are exact -0.0/+0.0 words at 0x11C and 0x12C, not nonzero drift.

Focused Ghidra results are in workspace work/ghidra-sc6-focus/motion-zeros:
306530 MotionUpdate, 2F54C0 MotionSample and 2FDB50 MotionBinding. The
0xB0 slot stride maps 0x11C to slot 1 +0x6C (sample W), 0x12C to slot 1
+0x7C (weighted delta W), and the earlier 0x1CC to slot 2 +0x6C.
The update subtracts previous sample W at 30687B, multiplies by the blend
weight, and stores the delta W. Rotation and player-position/collision
updates use XYZ separately. Sampling scales W numerically. The inactive
branch clears XYZ but leaves W. These are floating-point values, not
pointers, tags, or a discovered sign-bit branch.

Aligned machine-code operand scan in readers.txt cross-checks the targeted
animation region (0x2F0000..0x310000). This is supporting evidence, not an
exhaustive proof of every indirect consumer in the executable. We do not
claim W is padding or normalize all floats.

TwoFrameIdleReplay now permits exact opposite zero signs only at the three
observed words. It retains the general-combat/restoration, writer, record
size and both player-relative source guards. No load/save bytes are changed.
Existing motion_zero_evidence counts accepted words and records the last
word; raw evidence retains every byte. All nonzero and other-byte endpoint
comparisons, diagnostics, round and match-end guards remain in place.

Regression coverage exercises both directions, all three simultaneous zero
pairs, subnormal/nonzero rejection, neighboring-byte rejection and the
existing serializer/source/mode/restoration guards. FullStateProbeTest passed.
Live validation of the extended rule remains pending; the latest failures
never reached match-end, so they do not validate the EndState change.

Archived-payload cross-check: Ben47160 record8 differs at 0x11F/0x12F;
partner45096 record8 differs at 0x11F. Applying only the exact signed-zero
predicate at the allowed words leaves no other differences in either record.
This checks the reported mismatch, not subsequent live replay behavior.
`nFull Build-Native.ps1 completed successfully; DLL staged locally. See build-native/motion-zero-build.log. Not installed or live-validated in this change.

## 2026-10-17: secondary resource path cleanup

Reports Ben1264 and peer35372 both verified two original-input replay windows
(826..829 and 2351..2354), then cancelled on scoped-clear reason5. No replay
mismatch was reported. GameSet was recorded; this is not sufficient to infer
whole-match completion. The match-end and round guards remain unchanged.

Focused Ghidra exports (workspace work/ghidra-sc6-focus/secondary-ownership and
secondary-strings) establish the secondary collection at container+0x150:
- 8A1D20 copies a collection into it via 8638E0, then resolves copied paths.
- 85B720 inserts a 32-byte entry: DWORD key at+0, copied path at+8,
  hash-chain bookkeeping at+0x18/+0x1C.
- 409440 reads the path count at+8 relative to its string argument, allocates
  separate storage via 47C020, copies count*2 bytes, and constructs the path
  via E64270. E64270 examines UTF-16 slash characters and normalizes/copies
  the path; the resolved object is obtained separately via E62CA0.
- 89A700 copies the secondary path before resolving it. The buffer is not
  itself the loaded asset, a strong-reference control block, or a descriptor.
- 716430 iterates allocated entries and passes entry+8 directly to D46A00
  (allocator free), without traversing pointees or calling an asset destructor.
- 47C020 stores capacity at string+0xC and allocates capacity*2 bytes.

machine-code.txt cross-checks the actual entry+8 copy call at85B76D,
count*2 copy at40948C/409492, free at71646F/716478 and capacity store47C07E.
Ghidra's inferred names/types are research annotations, not original symbols.

Fix: replace the unconditional secondary-pointer rejection with validation of
this pinned path-string layout. Validate signed count/capacity, a conservative
capacity limit, pointer alignment/range, readable committed allocation and the
terminating UTF-16 zero for nonempty strings. Compare the entire capacity range
with retained payloads, control blocks and known resource-object headers.
Overlap remains related/cancellation; malformed or unsupported entries remain
unknown/cancellation. Empty strings and unallocated sparse slots are handled.
No game data is modified and native clear still executes exactly once under
its existing lock. Primary resource checks, unknown-thread rejection, exact
replay checks, diagnostics and round/match-end safety remain unchanged.

Scoped-clear report schema2 adds validated count, last secondary buffer,
allocation bytes, length, capacity and classification step (1 layout, 2 overlap,
3 readability/terminator, 0 validated). Old fields/reason numbers are preserved.
Tests cover valid strings, empty allocations, malformed lengths/capacities,
terminators, unaligned/unreadable/overflowing pointers, retained payload/control/
object overlap, sparse slots and the preexisting lock/exception guards.
Focused ScopedResourceClearTest passed. Live validation is pending: the old
reports did not retain the string header/content, so this investigation proves
the native collection semantics, not the exact runtime buffer's contents.

Full Build-Native.ps1 passed; see build-native/secondary-clear-build.log. DLL staged locally, not installed or live-validated.

## 2026-10-17: repeated bounded combat replay milestone

Bilateral SecondaryPaths run Ben39100 / peer44428 completed all three rounds
with endpoint masks1023. Both reports classified11 clears unrelated; the last
had9 validated path strings. User reported some stutter. Signed-zero exception
was not exercised in the reported cycle endpoints; continuous rollback remains
unimplemented.

Existing suite profile15 now enables repeated_combat scheduling: three
original-input three-frame replay cycles with120-frame spacing within a round.
Every cycle still captures four snapshots, verifies all replay hops, and retires
snapshots/dependencies before another capture. No prediction/injection is added.
Fight readiness is required at every capture. Legal next-round changes discard
unmodified partial windows and reset admission; rematches, session/peer/slot/
thread changes and unsafe replay failures retain existing stop behavior.
Between completed windows, clean End-state cleanup can report incomplete
coverage; partial windows and GameSet alone cannot qualify as match completion.

Per-cycle snapshots, native payloads, animation/RNG traces and all existing
reports remain. Added suite_repeated_combat_replay; summaries describe cycles
rather than claiming three distinct rounds. Compatibility signature changed
for profile15 to reject older one-per-round peers. Timing baseline unchanged.

NativeRollbackWindow tests passed for three spaced cycles within one round,
no early capture, immutable cycle frame metadata, partial-window round discard,
fresh fight gate and later replay failure stopping further work. Existing
one-per-round policy regressions remain through explicit constructor selection.
Test-TestSuite summary checks passed. Full build/live validation recorded below.

Full Build-Native.ps1 passed (build-native/repeated-combat-build.log). DLL staged, not installed or live-validated.

## 2026-10-17: repeated combat timing diagnostics

ReplayTiming uses QPC wall-clock measurements and fixed-size atomic count,
total/max tick aggregates for each of three cycles plus session cleanup/report
work. Instrumented phases: game-thread replay-runner step, capture, snapshot
archive copy, restoration, reference replay/verification, endpoint evidence
copy, trace snapshot copy, recycle, final release, partial discard and round
cleanup. Callback arrival gaps provide contextual cadence, not GPU/present
frame time. Session report_io covers report processing and binary evidence I/O,
not just device write latency or the outer launcher's text-file write.
Nested step/child times must not be summed as independent costs.

Reports include QPC frequency and per-phase aggregates; suite summary converts
to milliseconds. No sleeps, scheduling changes, normal-game hooks or disk I/O
were added to the timing scopes. Measurements are observational wall time and
can include OS preemption. Three cycles give bounded samples, not a claim of
continuous recording or definitive diagnosis of every stutter.

Regression coverage verifies count/total/max aggregation, preservation of a
false return and thrown exception, exactly-once invocation, out-of-range bucket
routing, QPC availability and summary conversion without invented peer timing.
All existing diagnostic evidence and round safety policies are retained.


## 2026-10-17: bounded replay workspace buffer reuse

The prior bilateral repeated-combat run verified all three cycles (120-frame
spacing), but retirement took 23–24 ms locally and 55–56 ms remotely. Each cycle
reconstructed ten 35,343,856-byte FullStateProbe objects: two workspaces per hop
and four pool snapshots. The transform record arrays dominate this storage.

Retirement now copies pristine defaults around those ten count-gated record
arrays. Counts, validity, failures, all stream callbacks/cursors, and every
other diagnostic/default field reset. Each appended record is fully overwritten
before its count is published; stale inactive records cannot become active.
A shared immutable defaults object is allocated/warmed during pool construction
(about 33.7 MiB additional process memory), not on first retirement. This trades
modest fixed memory for less game-thread memory clearing. Existing native
ownership drain, failed-slot quarantine, replay verification, effect cleanup,
thread/round/session guards and timing diagnostics are unchanged.

A fresh read-only Ghidra SC6ResourceClear pass completed for RVA 899110,
1C14E80, 29B9940 and 2DDA440. ResourceEntriesRelease decrements strong references
and invokes native disposal at zero, then weak/control disposal. ContainerClear
also frees secondary entries under its native lock. Therefore none of those
release calls or native resource lifetimes were skipped or moved. This change
only reuses C++ diagnostic storage, not native objects. Ghidra output is at
work/ghidra-sc6-focus/buffer-reuse-ownership (workspace root).

Regression coverage poisons every probe byte, verifies all non-buffer defaults,
checks invalid/empty transform state, verifies complete record overwrite and
zeroed unused record tail, then repeats reset. Existing retention, stale handle,
quarantine and restore-reuse tests remain required by Build-Native.
An optimized standalone benchmark with ten separate probes measured old resets
28.1–30.3 ms and reuse 0.57–1.10 ms on this PC (while the build was running).
This is not an end-to-end gameplay timing claim; restoration, evidence copying
and native cleanup still cost time and must be measured in the next match.


## 2026-10-17: buffer reuse live result and first-match suite wiring

Ben report steam-inputs-43892 completed SUITE_COMPLETE with local/peer pass255,
fail0, skip0, mutual result acknowledgement and three completed replay cycles.
Retirement measured 0.6741, 0.6738, 0.6742 ms (previously 23–24 ms).
Maximum step callbacks measured 40.6993, 39.6598, 41.1924 ms (previously61–62).
Restoration remains20.69–21.07 ms and replay4.29–4.46 ms. User reported normal
play and less stutter. Peer timing ZIP not yet received; peer result bits confirm
stages, not her timing. Local archive is SC6Rollback-Report-steam-inputs-43892-8b34aed2.zip.

Per the user's request, profile15 now selects the existing guarded first-match
resource preparation path rather than requiring a prior game-frame observation.
Loading callbacks nominate the owner; readiness remains blocked until a valid
battle boundary confirms that thread. Unknown/foreign-thread checks, native
reference ownership, fight readiness, round identity and cleanup remain intact.
No continuous prediction or clock-advancement bypass is enabled. The profile15
handshake signature changes to reject warmup-only peers. Launcher/README now
require starting in the first private lobby before loading the match. First-match
live validation is pending. All older suite profiles retain their startup policy.


## 2026-10-17 first-match failure investigation: 51084 / partner34944

Both first-match owners were verified; Ben cycle1 verified all1023 comparison
bits, then cycle2 failed effect comparison at object0 offset8 after restoration.
Partner34944 failed before restore: one effect capture, no completed cycles,
check2, stopped/window_failed sampled0, no object mismatch indices, resources
retired. Capture check2 brackets EffectMemoryGate::try_access: failure can be
try-lock contention OR stopped becoming true after the pre-call diagnostic.
The saved report cannot distinguish those causes. Every allocator release route
acquires this mutex for bookkeeping, including unrelated addresses. Do not call
this a proven resource corruption or make the gate block around native calls.
Next diagnostic should record the actual gate outcome and owner/caller context;
only proven clean pre-mutation contention is a candidate for bounded retry.

Local raw wind.lifecycle evidence is two32912-byte images plus external-check
field/padding. Object0: vtable RVA3E88D18, size384, expected address1E8247D0400
(generation10), replay address1E825798080(generation11). Differences:8..B,24,
34,3C,E4,E8,EA,110,114,115,118,11C,12C. JSON in
work/first-match-effects/51084-object0.json (repository work folder).

Fresh read-only Ghidra passes and PE disassembly reviewed allocator4A61C0,
spawn fragment2FC713 (call return2FC720), setup331E90, initialization3322C0,
field query332B60 and tick332400. Allocator delegates to an allocation interface;
spawn/setup do not zero the full allocation or write offset8. Several differences
are holes outside initialized fields. This supports allocator-residue mismatch,
not a conclusion that all differences are safe to mask. Further field-use audit
is needed before changing exact effect comparison. Initial research script names
were corrected:332B60 is a field query, not a destructor;331180 is a noise step.
Unwind entries may be function fragments; the decompiler's larger control flow
and exact PE instruction bytes must be considered together.
Ghidra outputs are under workspace work/ghidra-sc6-focus/effect-mismatch,
effect-mismatch-verified and effect-mismatch-setup. No runtime changes, guards,
comparisons, diagnostics or round safety were altered by this investigation.


## Exact capture-gate diagnostics and effect-field audit

EffectMemoryGate now optionally returns entered(1), contended(2), stopped(3),
with0 meaning no attempt. Contention is checked first exactly as before; when
both lock and stop are present it reports the immediate lock rejection, not a
claim that stop is absent. Capture records that result, requester/owner threads,
capture serial, cumulative attempts/contentions/stops and last rejection. Raw
suite reports and summary include these; no blocking, retry or masking added.

Ghidra audit for the 3E88D18 /384-byte effect: creation at2FC70E..2FC8DE,
setup331E90, init3322C0, tick332400, spatial field query332B60, virtual getters
331070/331080 and manager update/removal333FD0. PE instructions verify manager
uses links10/18, manager28, status68/6C and virtual callbacks; not field08.
The 17 differing bytes group into08..0B,24,34,3C,E4,E8,EA,110,114,115,118,11C,
12C. No semantic reads/writes of those fields were found in the reviewed paths.
Their containing gaps are08..0F,22..27,34..3F,E4..EF,110..12F. Init writes
lifetime30, spawn writes a WORD at20, and active transform data starts130 after
movement controls100..10F. Full-allocation snapshot/equality reads them anyway.
This is strong evidence of comparison of allocation residue for this exact type;
it is not a whole-program proof about external aliases or other effect types.
No comparison exclusions are enabled. Ghidra artifacts:
workspace work/ghidra-sc6-focus/effect-field-audit and effect-mismatch-setup.

## Recorded-only workspace copy (2026-10-17)
GateDiagnostic run 11724 and peer 15524 passed all three cycles without warmup. Restoration dominated measured work (~19.4â€“20.4 ms local, ~25.4 ms peer); peak callbacks ~37â€“39 / 49â€“51 ms. Gate contentions/stops were zero on both sides.

Ghidra focused audit of native save RVA 3841E0 and load 384540 is saved under workspace work/ghidra-sc6-focus/replay-copy-audit. The loader passes the stream to native readers and invokes its read callback at vtable +30. It does not receive the auxiliary TransformSnapshot arrays. We keep native serialization, read ordering and game-state restoration unchanged. Mod-side consumers iterate count; append initializes each full record before publishing count.

NativeAdjacentRestore preparation previously assigned a full 35,343,856-byte FullStateProbe for each hop. copy_recorded_from now copies all fields outside record arrays plus all count-published records, including their complete payload and metadata. No fields within a live record are excluded. Invalid counts reject before mutation; self-copy is supported. This is independent of the unresolved wind padding audit. Ownership retirement, round/fight gates, verification and all diagnostics remain intact.

Optimized standalone microbenchmark, illustrative 2,100 records in one populated transform snapshot: 35,343,856 -> 5,113,776 bytes; 0.5405 -> 0.0730 ms per copy on this PC. This is not a live-game timing prediction; live DLL build configuration and memory contention differ. Full regression tests and next peer run must establish live improvement. No compiler optimization flags or hold timing were changed.

## Additional copy/evidence optimization (2026-10-17)
NativeBoundarySnapshot preparation now copies only count-published records for both adjacent transform snapshots, preserving all record bytes and all metadata. Invalid counts reject before copying, and source/target aliasing is handled. Tests cover valid entries, unused storage, self-copy, invalid count rejection and empty snapshots.
ReplayFailureEvidence reserves its existing 32 MiB evidence budget once before capture. This avoids geometric vector growth and repeated moves of already-appended diagnostic sections. Section names, layout, contents, bounds and publication ordering remain unchanged; allocation failure still records state 2. Native restore and hold timing are unchanged. No threaded live-memory access was added. Expected benefit is lower game-thread copying/allocation cost, not elimination of all stutter; live timing is still required.

## Wind emitter countdown integration (2026-10-17; runs 35316/23580)
Both peers: restoration passed, replay matched frames 665->666 and 666->667. At natural frame667 inline site5 (RVA3330A4) advanced RNG281471009->881491317; replay667->668 made no corresponding store and kept281471009. No inline trace drops. Ben owned images show effect count5,5,5,6; the new final-image object matches RSI from the natural site5 event, type3E88CE8, size1E0. Previous snapshots did not include the emitter list or countdown.

Ghidra focused outputs are workspace work/ghidra-sc6-focus/wind-rng-audit, wind-spawn-audit and wind-emitter-audit. 2DBC60 traverses list rooted at470F1C0, invoking334960 with node+10 object, before333FD0 effect dispatch. 334960 branches on float object+5C<=0: resets countdown from RNG and config58/A4, allocates1E0, links3E88CE8 object, calls333E80 then332D60 setup; that setup performs the observed inline RNG store3330A4. Otherwise countdown decreases by global48462FC. The loop consumes pending/count50/54 and clears both. Shared global48462D0..+58 is already native-serialized. Research name WindRngTick in the first output is inaccurate: the function is setup, not the ordinary tick.

New WindEmitterSnapshot captures up to128 linked emitter nodes and their A8-byte data, preserving exact root/node/object identity and configuration. Restore validates the entire current list and all nonmutable bytes first, then writes only50..57 and5C..5F, and verifies an exact recapture. It does not recreate emitters or overwrite links/configuration. Changed roots/lists/configuration and unreadable memory reject. NativeEffectSession includes this state in binding capture, restoration and post-replay comparison. Existing strong resource and owner-thread/round guards remain. New wind.emitters binary sections retain source snapshots and failed expected/actual states; suite_emitter counters identify the stage. Protocol signature for first-match suite changed to reject older peers.

Focused tests cover countdown/pending restore, unchanged configuration, mismatch rejection before writes, self-cycle, unreadable object and empty-list identity. Existing copy-reduction and evidence-reservation optimizations are retained; no RNG value is forced after replay and no comparisons are weakened. Live verification of emission recreation and RNG convergence remains outstanding. The old logs did not capture the countdown itself, so the missing emitter state is established by code audit plus the matching spawn/RNG event; runtime values will now be recorded.

## Deferred diagnostic serialization (2026-10-17)
ReplayFailureEvidence now supports a preallocated immutable arena plus bounded section descriptors. CombatResourcePreparation prepares four32MiB arenas (three cycle artifacts and the separate failure artifact), one shared trace scratch (~10MiB), and per-cycle animation/RNG trace outputs on the network/preparation thread before release-publishing retained_replay_enabled. Pages are touched during preparation. Capturing records raw bytes into each artifact's owned arena; no arena is reused during the suite. Shared scratch is only read/copied on the game owner and is never read by the report worker.

The existing report worker reconstructs identical section headers and writes captured arena spans after acquire publication. It never reads game objects or recycled probe memory. All diagnostic sections, including wind emitter data, remain. Verification/timing/round rules are unchanged. Capture still incurs bounded memcpy and trace-lock cost; this is not zero-overhead diagnostics. Archive snapshot packaging and additional trace copies remain measurable future targets. Memory commitment moves earlier (~155MiB aggregate for this suite), rather than being silently unbounded.

Regression captures both legacy and deferred representations, mutates original probe and trace scratch, writes from another thread, and compares the complete file byte-for-byte. Bounds and failure publication remain covered. The first implementation put descriptors inline and exceeded test stack budget; descriptors now allocate on heap during preparation and tests pass.


## Full-match reference replay (2026-10-17)

Added a separate profile 16, FullMatchReplay. Profile 15 retains the existing three-cycle behavior. Profile 16 runs the existing four-snapshot / three-frame verified reference window at the existing minimum 120-frame cadence until the pinned native match-end state. It does not predict remote inputs or bypass the native network wait.

The three evidence slots are now a bounded producer/report-worker queue in this profile. The game publishes each immutable archive only after replay verification and ownership retirement. The report worker writes all snapshot, replay, animation, RNG and numeric cycle files, resets the existing evidence arena and publishes slot availability last. A busy slot skips capture and increments a counter; it never blocks the game or overwrites pending evidence. I/O failure is sticky and prevents PASS. Failure evidence remains a separate channel. Timing after cycle 3 aggregates in the session row; individual cycle diagnostics remain in numbered files.

Match-end handling discards any unmodified partial window, finishes effect ownership cleanup and then retires. Between-round fight readiness, identity/generation checks, exact endpoint comparisons, scoped resource clear handling and restoration failure latches remain. Full-match success additionally requires confirmed match end, at least one verified window, clean resource retirement, all completed cycle evidence flushed and bilateral suite results. Cancellation/disconnect/timeout do not set match completion.

Regression coverage includes more than three cycles with queue wraparound, writer backpressure, worker-thread file publication, round transition with a partial window, partial-window match end, empty-match non-PASS, failed retirement publication, explicit abort, evidence-write failure and deferred arena reuse. Live full-match validation is pending.

Full build, full-match regression tests and summary tests passed. Installed and SHA256-verified A8349893C27EEFD904CE735FCCD883627A3B306553AC2E45BD9B2192A657F17B; partner package SC6Rollback-FullMatchReplay-Suite-20261017-164953.zip. Cleared leftover process 40080 with elevated taskkill. Both PCs must restart/install; first-lobby test has not run yet.
