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
