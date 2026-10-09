# Simulation catch-up investigation

The Steam shadow run on both PCs stayed within three estimated correction
frames with four frames of simulated input delay. It did not resimulate SC6.
The existing native replay experiments advance through natural updates;
`OfflineRollbackSession` has a synchronous adapter contract but no SC6 adapter.

## Current diagnostic

`tools/audit_update_entry.py` reads only the pinned executable. It produces a
disassembly, direct-call candidates confirmed at instruction boundaries, and
address-reference candidates. On the pinned executable it decoded 376
instructions at RVA 0x2DBC60 and found no direct callers or supported address
references. This is not proof that there are no callers: indirect dispatch,
tail jumps, and other reference encodings remain possible.

The entry bridge now passes the original caller's return address to the
recorder. An ordinary offline Training capture writes `entry_module_base`,
`entry_return_address`, and `entry_caller_changes` to its metadata. Addresses
are decimal. Resolve the return address against the loaded module containing
it before disassembling; do not assume it is in the main executable.

This diagnostic does not add simulation calls or change the game's return
address. The bridge test compares the observed address with the target's
actual return address and verifies mixed register/stack argument forwarding,
input replacement, and the returned value.

Next live step: install with SC6 closed, enter offline Training, and collect
one untouched ordinary 600-frame capture. Inspect the caller and its surrounding
work before implementing a bounded extra-update invocation. A callable ABI
alone does not establish deterministic catch-up or suppression of repeated
sound, rendering, networking, and other side effects.

## Confirmed caller capture and static follow-up

Capture `frames-54692-15340000.txt` completed 600 frames, zero skipped entries,
zero faults, zero counter discontinuities, and zero invalid input envelopes.
The user confirmed untouched controls, responsiveness, and no glitches.
All entries had the same return address, RVA `0x3FE816` in the main module.

The instruction preceding that address is an indirect virtual call at
`0x3FE813`, through `[vtable+0x68]`. The caller supplies an object from
`[rbx+0x1450]` in RCX and a stack input envelope in RDX. The observed battle
entry instead consumes the envelope in RCX. Therefore an intervening dispatch
adapter/tail-call path must be resolved before treating the virtual method
and battle entry as interchangeable callable functions.

The encompassing routine starts at `0x3FE520`; the captured unwind range was
only a chained fragment. It already contains a bounded iteration structure:

- Iteration count is computed at `0x3FE61D..0x3FE65B` using fields at
  owner+0x478, child+0x3A4, and owner+0x148C, with an alternate count-one path.
- Each iteration begins at `0x3FE6B0`. It builds an input envelope, calls
  `0x3FCD10` for each input slot, and assigns auxiliary data at owner+0x14C8.
- After the virtual update, it calls `0x3999C0`, `0x3FCE80`, and `0x3FCA60`.
- A separate branch at `0x3FE832..0x3FE842` clears owner+0x1462 and repeats
  from `0x3FE7F1`, without rebuilding the input envelope.
- The outer iteration decrements its counter and returns to `0x3FE6B0` at
  `0x3FE886..0x3FE891`.

These are control-flow observations, not identified rollback semantics. Do not
set the repeat flag or iteration count: input preparation and post-update work
have not been characterized for resimulation. In particular, directly invoking
the inner battle update would omit work performed by this surrounding loop.

Disassembly evidence is in `build-network/catch-up-caller.txt` and
`build-network/catch-up-surrounding.txt`. Next research should resolve the
virtual dispatch adapter and inspect `0x3FCD10` and the post-update routines,
including their chained unwind ranges, before adding a one-extra-update probe.
No live writes or extra simulation updates were performed in this follow-up.

## Dispatch adapter and helper audit

`tools/audit_catch_up.py` now follows chained unwind metadata to collect whole
functions rather than stopping at their first fragment. The pinned binary
produced 5 fragments/217 instructions for 0x3FE520, 5/88 for 0x3FCD10,
3/396 for 0x3FCE80, 8/190 for 0x3FCA60, and 5/54 for 0x3999C0.
Machine-readable evidence is `build-network/catch-up-audit.json`.

The missing dispatch adapter is at `0x3D2A20`: `mov rcx,rdx` followed by a
tail jump to `0x2DBC60`. Its absolute pointer is stored at RVA `0x327B678`,
consistent with slot +0x68 of a table at `0x327B610`. This explains the
captured return address and the input-envelope argument conversion. The tail
adapter has no unwind entry; its bytes and pointer reference support the
interpretation, rather than an assumed C++ signature.

Input preparation at 0x3FCD10 obtains frame-indexed input through 0x3F0720,
saves the prior input pair, updates the previous-held value, and computes
`(previous ^ current) & current` for new presses. It then applies suppression
flags and masks. This agrees with the earlier observed held/new-press pairs.
Replaying input acquisition itself would also change that history; catch-up
should use owned, recorded envelopes instead of polling live inputs again.

Post-update 0x3999C0 iterates a callback collection, invokes virtual callbacks,
and can tail-call a cleanup routine. 0x3FCE80 consumes a collection of state
codes at owner+0x1470 and changes owner+0x1480 with many conditional calls.
0x3FCA60 is gated on owner+0x528 and state code 2, then accesses additional
per-slot structures. Exact domain meanings are not established. These are
not proven pure simulation operations, so replaying the complete caller loop
would risk repeating callbacks and transitions.

Implementation direction: a bounded offline extra-update probe should call
the inner battle trampoline with an owned input envelope on the existing game
thread, avoiding recursive entry through the recorder. It must measure the
counter before/after and compare captured state against natural-update
references. First test one extra update; no online enablement follows from
this static audit alone. Side effects inside the battle function remain an
open question. The probe is not implemented or armed by this audit.

## One-update probe implementation

`NativeImmediateReplay.hpp` now implements a separately armed offline idle
experiment. It uses the existing guarded adjacent restore to capture A and B,
restores A, calls the original trampoline once with an owned A input envelope,
requires the outer counter to advance exactly once, and immediately invokes
the existing replay comparison against B. The original current update then
continues. It does not repeatedly call the outer callback/state-transition loop.

The caller RVA, neutral input fields, owner thread, snapshot guards, and counter
must pass. Invocation exceptions or comparison failures latch a failed recorder
session. There is no retry or online trigger. Metadata reports actual invocation,
counter values, exception code, restoration/comparison results, and failure stage.
This is a bounded experiment, not a complete snapshot or proven catch-up adapter.

After installation and a fresh launch in unpaused offline Training, use
`scripts/Arm-SingleUpdate.ps1 -TrainingConfirmed`. Leave controls untouched.
Restart the game afterward, including after a failed comparison. The launcher
requires a matching installed build, no pending arm, and no previous native or
Steam-input experiment from this process. No live result exists yet.

### One-frame live result and three-frame extension

`native-adjacent-idle-17372-16283609.txt` passed: one synchronous invocation,
counter 878 to 879, restoration and reference comparison verified, 600
continuous samples, and no reported faults or exceptions. The user confirmed
untouched controls and normal responsiveness without glitches.

`NativeThreeImmediateReplay.hpp` extends the experiment without changing the
one-frame mode. It captures four natural idle boundaries, prepares all three
adjacent restore guards before writes, restores backwards through the three
boundaries, and then invokes three updates synchronously. Every update uses
its own recorded input envelope and one-shot invocation guard; every resulting
frame must pass the existing comparison before the next update is attempted.
Failure stops the experiment and requires a restart. The normal current update
still runs after the probe returns. Use `Arm-ThreeUpdates.ps1 -TrainingConfirmed`
only after installing the new build and launching fresh offline Training.
Three-frame live validation is pending; online rollback remains disabled.

### Three-frame first attempt

`native-adjacent-idle-56956-16509750.txt` captured four references and passed
all three preparation and restoration checks. The first synchronous update
advanced the counter 783 to 784 without exception, but its reference comparison
failed; the remaining two calls were not attempted. The user confirmed normal
responsiveness with no visible glitches, then closed SC6.

The follow-up build adds per-hop comparison diagnostics: measured RNG/player
identity and scalar byte differences, replay-save completion/fault, keys,
animation buffers, native payload, blend, transform and camera equality. When
a replay save exists it exports expected/actual payloads and record layouts
for offline byte attribution. These diagnostics do not relax any comparison
or change the three-call limit. The cause of the mismatch is not yet known.

### Native difference attribution

`native-adjacent-idle-57740-16911468` reproduced the first-replay mismatch.
RNG, player identities, scalar bytes, keys, rings, blends, transforms and cameras
all matched. Native expected and actual payload lengths were both 106766 bytes.
Of 13 raw differing bytes, 12 belonged to the previously handled player
self-pointer records (writer RVA 0x30AA54). The additional difference was in
record 349, writer RVA 0x30A9CE, source RVA 0x47ACB70, length 13584, offset 6828:
expected 0, actual 1. The capture's player pointer maps this to P2+0x1B3C
(main-module RVA 0x47AE61C).

Static tracing establishes that this is part of a 16-bit field, not padding.
The serializer writes the player+0x90 block of length 0x3510, which includes it.
Routine 0x304D50 selects fields from player+0x1B38 using a two-byte index.
Index 2 selects +0x1B3C. It adds a supplied delta, clamps that field to 0..240,
and dispatches a virtual notification when the resulting value changes.
Other direct accesses initialize or read this field; its player-facing name
has not been established.

One relevant caller is 0x3425A0: it reads a supplied record's type, mode, and
value at +4/+8/+12, and record type 4 calls 0x304D50 with index 2 at 0x3426F7.
It releases the supplied record via 0x2DD8F0 afterward. This is a candidate
source of the replay-only increment, not a confirmed runtime attribution.
Another call path at 0x3793D7 accepts a variable field index and value.

Evidence: `build-network/player-1b3c-references.txt` and
`build-network/player-field-update-callers.txt`. Stack-offset coincidences in
unrelated functions are not player-field accesses. No mismatch was ignored,
no compensation write was added, and no live experiment ran during this audit.
Next diagnostic should attribute writes/updates of field index 2 during the
bounded replay, including caller and delta, to distinguish an extra queued
operation from another state dependency. Preserve the exact native comparator.

### Bounded field-update trace

The three-frame mode now enables an additional pinned-prologue hook at
0x304D50. `FieldUpdateBridge.asm` preserves the volatile registers, floating
point environment, arguments, caller return address and original return path.
It invokes a read-only trace callback before tail-jumping to the trampoline.
No function-signature return type is assumed and the original call runs once.

`FieldUpdateTrace.hpp` captures at most 256 calls for field index 2 from the
probe's owner thread. It records caller, player, counter, requested signed
16-bit delta, prior 16-bit value, flags and phase. Phase 1 is reference capture
and natural playback, 2 restoration, 3 synchronous replay, 4 comparison.
This observes calls to one update routine, not every possible writer to the
field. Unobserved direct writes remain possible. It does not establish that
the requested delta was actually applied; the routine can reject or clamp it.

The trace stops on completion/failure/timeout and is serialized as
`*-field-updates.csv`. The hook remains pass-through afterward. A unit test
verifies original argument/return preservation, filtering, bounded capture,
stop behavior, LastError preservation and absence of field writes. Installation
does not arm the experiment; fresh offline Training confirmation is required.

### Increment trace result and assignment follow-up

Capture `native-adjacent-idle-54936-17711328` completed all three immediate
updates, but only the first two comparisons passed. The third differed at
the same P2+0x1B3C value (expected zero, actual one). The trace observed
identical index-2, delta-1 requests from return RVA 0x3793DC at counter 1098
in both the natural and replay phases; both saw a prior value of zero.
No trace entries were dropped. This does not support a duplicate update call.
The user confirmed responsiveness without glitches.

Static follow-up identified assignment routine 0x2D6D70, which writes the
signed nonnegative word from its second argument+0xC to player+0x1B3C.
Caller 0x2D73C0 invokes it for both players at 0x2D73FC and 0x2D740C.
This may explain a normal-path reset, but runtime attribution remains pending.
Other initialization/reset writers exist, so this is not an exhaustive trace.

The next diagnostic adds a read-only entry trace at 0x2D6D70 using the same
register-preserving bridge pattern. The CSV now uses `requested_value` and
`assignment`: assignment 0 means an additive request; 1 means a nonnegative
direct-assignment request. Negative settings values are skipped just as in
the native routine. Tests cover zero assignments, negative sentinel values,
invalid settings pointers, and absence of writes. No corrective replay changes
or comparison exceptions have been introduced.

### Longer read-only field observation
The reset/increment trace repeat (native-adjacent-idle-55492-18817609) passed all three immediate comparisons and 600 frames; the user confirmed normal untouched gameplay. However, it recorded zero field events, so it does not resolve the earlier intermittent P2+0x1B3C mismatch.
Added OFFLINE_FIELD_TRACE_ONLY and scripts/Arm-FieldTrace.ps1. This mode records 600 normal boundaries plus bounded increment/assignment entry events, without save/load or extra updates. Boundary values are exported to *-field-boundaries.csv. Event traces report overflow and are pre-entry observations, not proof that each requested write occurred. Build-Native and its local checks passed; installed for the next fresh offline Training capture.

### 600-frame field trace result
native-adjacent-idle-50652-19241406 completed 600 continuous frames, fault=0, no restore or extra updates. It captured 15 additive requests for P2 index 2, requested_value=1 and before=0, spaced exactly 40 counters apart (1177 through 1737), caller RVA 0x3793DC. No assignment-entry events or dropped events. Every recorded boundary retained P1=240 and P2=0. This establishes recurring requests during normal playback while boundary state remains zero; it does not distinguish rejection/clamping inside the increment routine from another uninstrumented write before the next boundary. User visual confirmation pending. Next diagnostic should observe the increment routine's post-call value before attributing this to an external reset.

### Post-call observation prepared
Added a read-only hook at pinned RVA 0x3793DC (the jump immediately after the observed increment call). It preserves volatile registers/flags and observes the player via preserved RDI; assignment=2 denotes a post-call observation, with the CSV before column containing the value at that observation. Unlike increment-entry records, these observations are emitted for every visit to this call site and must be correlated with the preceding index-2 entry record, not assumed to independently prove an index-2 operation. A dedicated mid-function harness checks stack alignment, player argument, continuation, and return value preservation. Recorder tests passed. Installation requires closing the running game.

### Post-call capture result
native-adjacent-idle-49756-19638640: 600 continuous frames, fault=0, 28 events, none dropped; no restoration or extra updates. Fourteen index-2 increment-entry events (P2 before=0, delta=1) each immediately paired with a post-call observation showing value=1, counters 2298..2818 every 40 frames. All 600 boundary observations show P2=0 (P1=240). This demonstrates that the increment takes effect and the field returns to zero before the next recorded boundary. No assignment-entry events were captured by the currently instrumented reset routine; the responsible later write remains unidentified. Next investigation should locate that write or narrow its position relative to the battle-update return, rather than suppressing the increment or relaxing comparison. Visual confirmation pending.

### Battle-return checkpoint
User confirmed the post-call capture stayed responsive with controls untouched. Added a read-only checkpoint at RVA 0x3FE816, immediately after the normal battle-update call returns, before caller-side callbacks. The pinned seven-byte LEA is verified before hooking. It uses the same register-preserving mid-function bridge as the locally tested post-increment checkpoint. On the owning thread, it emits assignment=3 only when the latest trace event is a post-increment observation (assignment=2). Compare its observed value against that event and the next boundary to distinguish an internal reset from caller-side work. This diagnostic performs no state restoration or extra updates.

### Battle-return capture result
native-adjacent-idle-57924-19970875 completed 600 continuous frames without faults, with 45 events and no drops. All 15 increment sequences show P2 0 before increment, 1 after increment, and still 1 immediately after the battle update returns (outer counter advanced once). All recorded next-entry boundaries remain zero. Thus the return-to-zero occurs after the battle update returns and before the next recorded entry, outside the synchronous routine currently replayed. The responsible write is still unidentified; do not force the field to zero as a general fix. Next narrow caller-side callbacks / intervening outer-loop work, with reusable read-only capture support to reduce restarts. User visual confirmation pending.

### Inter-update checkpoints and reusable observation
User confirmed the battle-return capture was visually clean. Added pinned checkpoints at 0x3FE822 (after 0x3999C0), 0x3FE82A (after 0x3FCE80), and 0x3FE832 (after 0x3FCA60). CSV assignment codes 4, 5, 6 respectively follow code 3, once per observed increment sequence. These observations narrow the reset location without invoking any extra game calls. Each hook validates the overwritten instruction bytes and uses the existing mid-function register-preserving bridge pattern.
Read-only field captures no longer consume full_used. Arm-FieldTrace permits repeating completed 600-frame fault-free field-only reports in the same process; other or incomplete native-adjacent reports and Steam-input reports still require restart. Existing full restore once-only and restore_failed guards remain. This does not enable DLL hot replacement.

### Inter-update capture result
native-adjacent-idle-36372-20236406: 600 continuous frames, fault=0, 90 events, none dropped. All 15 sequences show P2=1 at battle return (assignment=3), then P2=0 at 0x3FE822 (assignment=4), remaining zero at both later checkpoints and all boundaries. The reset is localized to the interval containing the call to 0x3999C0 with owner+0xA30, including its callees; the exact writer is not yet identified. This supports investigating that callback dispatch rather than invoking the entire outer loop or hardcoding zero. No restoration or extra updates were performed. Visual confirmation pending.

### Callback target inventory
User confirmed the inter-update capture was visually clean. Static inspection of 0x3999C0 shows reverse iteration of 0x40-byte entries (count +0x50, optional heap +0x40), active field +0x30, inline-or-pointer object +0x20, virtual target +0x68. Added bounded read-only inventory at the first increment's battle-return checkpoint using the outer owner in RBX. Records up to 64 entries, objects, vtables, targets, and first four object words; invalid memory/counts report invalid instead of invoking anything. Inventory is a single observation and does not prove which callback writes the field. Unit checks cover inline object resolution, count bounds and invalid pointer rejection.

### Callback inventory capture result
native-adjacent-idle-20824-20641156 completed 600 continuous frames without faults, 90 field events and no drops. Inventory valid with two active callback objects, both using wrapper RVA 0x3EF130. Stored targets are RVA 0x3C7080 (index 1, dispatched first) and RVA 0x8954A4 (index 0). Static wrapper resolves a weak object reference via 0xF823F0, applies member adjustment and calls the stored target. 0x8954A4 is a virtual thunk to slot +0x5F8. Targets are identified, not yet proven responsible for the reset; first target requires chained-unwind disassembly and the second requires resolved runtime object/vtable inspection. Saved initial disassembly in build-network/callback-targets.txt. No save/load or extra updates occurred. Visual confirmation pending.

### Resolve callback receiver and virtual target
User confirmed inventory capture normal/untouched. Expanded static disassembly of all chained fragments for 0x3C7080: reference-managed collection processing with multiple indirect calls; no direct field write established. The other target is the virtual thunk 0x8954A4. Added read-only weak receiver resolution matching 0xF823F0: index/serial validity, object-table count at RVA 0x42A115C, table pointer at 0x42A1150, 24-byte item, serial +0x10 and pending/unreachable flags +8. Applies signed member adjustment then reads virtual slot +0x5F8 for that exact thunk. No native methods invoked. Tests cover virtual resolution and stale serial rejection. Saved chained disassembly in build-network/reset-callback-analysis.txt. This resolves candidates, not proof of the responsible writer.

### Resolved callback capture
native-adjacent-idle-56120-21208828 completed 600 continuous frames, zero faults, 90 events, no drops, valid two-callback inventory. Weak receivers resolved; virtual thunk 0x8954A4 resolves to 0x428270. Its static body iterates two entries and, depending on a mode byte, calls a resolved interface slot +0x110 with EDX=0 or EDX=240. This is a strong candidate for the observed boundary values (P1=240, P2=0), but the interface writer and runtime branch remain to be verified. No correction implemented. Full disassembly saved in build-network/resolved-callback.txt. Visual confirmation pending.

### Setter call-site capture prepared
User confirmed resolved-target capture normal/untouched. Added read-only checkpoints at pinned call sites 0x428334 (zero branch) and 0x4283B9 (240 branch) in 0x428270. Records unique object/value/site combinations, virtual setter address (+0x110), counter and eight object words, bounded to 16 entries with explicit overflow/fault reporting. Original indirect calls remain in their MinHook trampolines and execute normally; no additional native calls or state writes. Tests cover target/argument capture, deduplication, LastError preservation, invalid object handling and reset across captures. The exact player-field writer remains unverified until target disassembly/runtime evidence is available.

### Setter identified
native-adjacent-idle-55232-21935546: 600 continuous frames, fault=0, setter_fault=0, 90 field events. Setter inventory reached 16 distinct temporary objects (overflow=1); inventory is not exhaustive. All retained setter entries use value=0 at 0x428334 and target RVA 0x3CD170. This thunk adds 8 to RCX and tail-jumps to 0x2D3B10. Disassembly proves 0x2D3B10 reads player index at adjusted receiver+8 (original object+16), resolves player via pointer array RVA 0x470DE90, checks player+0x43E2C==0, caps EDX at 0x7FFF, then writes DX to player+0x1B3C at RVA 0x2D3B32. The low DWORD of retained word2 is 1, consistent with P2. Thus observed zero setter path directly writes the suspect field, gated by an existing flag. The correction must preserve these semantics and applicable Training mode rather than unconditionally zeroing the field or replaying the full dispatcher. Saved disassembly in build-network/resolved-setter.txt. Visual confirmation pending; no restoration or extra updates performed.

### Guarded offline Training correction
User confirmed setter capture normal/untouched. Added TrainingFieldReset: observes the real setter call per player on each normal reference interval, recording player pointer, counter, requested 0/240 and player+0x43E2C guard. Before applying a recorded setter effect, validates counter, pointer-table identity, allowed value and unchanged guard for all samples; requires a P2 sample. Writes the 16-bit field only when the recorded/current native guard is zero. This reproduces the verified setter semantics for this offline probe without replaying the callback dispatcher. It is not a production/network correction or proof of complete outer-loop coverage.
Three-frame immediate probe now waits near the previously observed 40-update increment interval so its window can include the mismatch; output reports waited_for_increment and training_resets_applied. References retain their exact native/scalar/animation/camera comparisons. Test evidence must include an actual increment in the captured/replayed window; a comparison-only pass without that event is insufficient. Added unit checks for zero/240 application, stale-counter rejection, changed-guard rejection and nonzero guard no-write behavior. Installation pending game closure.

### First guarded correction live attempt
native-adjacent-idle-47028-23070312 stopped at sample 68: four references captured, three pairs prepared, hop2 native restore invoked but not verified (save_fault=1). No synchronous updates or training corrections ran; all comparisons unstarted. The increment window was captured (phase1 counters 738 and 778); this attempt cannot assess the new correction. Probe stage restore, hop2; restored=0 counts completed restores, not absence of native loading. Session requires restart because native loading began. Need diagnose hop2 restore verification fault without relaxing guards. Visual result pending.

### Restore failure diagnostics expanded
User confirmed the first correction attempt remained responsive without glitches. SC6 is now closed. Added delayed_failure_line at explicit delayed_step fault sites and per-hop reporting for native consumption, selected keys, postflight, scalar preservation, outer correction, delayed_loaded, blend/wind/sway/constraint restoration, save validity/stability, ring and camera verification, and exception code. These are diagnostic-only changes; no restore checks were weakened and the guarded Training correction remains untested live because the prior attempt stopped earlier.

### Guarded correction live PASS
native-adjacent-idle-60408-23296234: 600 continuous frames, zero faults; four references captured, three restores verified, three immediate updates and three comparisons passed. Recorded Training setter effects applied three times. Counters 1297->1298->1299->1300. Crucially, the index-2 +1 event at counter1298 occurred in both natural phase1 and immediate replay phase3, so this pass exercised the previously mismatching transition. All native comparisons passed under the established self-pointer allowance (12 raw pointer bytes only), RNG/scalars/rings/blends/transforms/cameras matched. Setter inventory overflow remains a diagnostic-only limitation (16 temporary object samples); latest per-player observations used by correction are independent of that inventory and are updated each call. Previous hop2 restore failure did not recur and its cause remains unresolved; this pass does not prove it fixed. Visual confirmation pending. Network rollback remains disabled.

### Six-frame catch-up stress probe
User confirmed the corrected three-frame replay visually clean. Prepared a distinct six-frame mode (OFFLINE_SIX_SYNCHRONOUS_REPLAY, Arm-SixUpdates.ps1), seven natural snapshots, six guarded adjacent restores and six immediate updates with a comparison after each. Three-frame mode remains available. It retains recorded Training setter effects and increment-window selection. Reports replay_depth and QPC timing around the replay loop; timing includes diagnostic setter validation and expensive per-frame state saving/comparisons, so it is not production rollback cost or a network-delay recommendation. Network rollback remains disabled. Prior intermittent restore failure remains unresolved and detailed failure reporting is retained.
