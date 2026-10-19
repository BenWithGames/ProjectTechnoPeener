# Prediction input delivery and settlement

Status (2026-10-18): live native input preparation, authoritative raw transport,
correction replay and guarded prediction admission are connected. The full local
build and integration regressions pass; two-player live validation is pending.
Existing restoration verification, diagnostics and round guards remain enabled.
The historical entries below describe earlier, deliberately inactive components.

## Implemented

`PredictionInputDelivery.hpp` owns raw input history separately from SC6's
ring and recording. It predicts only remote raw buttons, derives held/pressed
words using the audited 3FCD10 rules, and retains the other six envelope words.
The caller must supply resolved suppression/restriction decisions from the
native boundary; filtered input cannot reconstruct the raw buttons.

Late truth recomputes the retained suffix, including press edges on subsequent
already-confirmed frames. The confirmed publisher cannot cross an input gap
or send a prediction to the recorder. A failed publisher is terminal because
its callback may already have produced a side effect. Publication is a callback
contract; no native callback is bound in production yet.

Handoff requires confirmation, publication, and acknowledgment that the latest
input revision was applied through the complete simulation head. A stale
revision token cannot acknowledge a later correction. Native settlement and
resource ownership still need independent validation; this token is not a
native-state proof.

`BoundedRollbackSession` now supports an explicit drain request. It stops
admitting new frames, continues accepting old-round confirmations, and can
reconcile the final correction without advancing another frame. Equal-input
arrivals also require reconciliation before declaring the handoff ready.

Regression coverage in the existing NativeInputWords and BoundedRollback
tests covers later press-edge changes, 7,200 frames per player-slot assignment,
filter policies, buffer reuse, reordered arrivals, recorder gaps, stale revision
and round identities, depth limits, publisher failure, and final corrections
without extra simulation advancement. Both focused suites passed.

## Ghidra and binary evidence

Pinned executable SHA256:
`F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553`.

Existing exports `prediction-envelope/003fcd10_Origin_3fcd10.c` and
`prediction-wait-dependencies/003f6600_Origin_3f6600.c` establish raw edge
formation and the recorder's unfiltered publication path. The earlier runtime
inspection found that recorder active. This is why guessed native ring entries
cannot be isolated merely by clearing their valid byte.

Fresh read-only Ghidra exports are in workspace
`work/ghidra-sc6-focus/prediction-outer-commit/`; instruction evidence is
`work/prediction-delivery-commit-assembly.txt`.

* 3FE520 calls 3999C0(owner+A30) immediately after the battle update and before
  3FCE80 consumes the state-change queue.
* At 399A2C, 3999C0 calls each active delegate's vtable+68 function. Its cleanup
  tail jumps to 399DF0. These are not merely plain scalar queue writes.
* 3FE960 separately updates the six auxiliary input words from selected local
  input/controller objects; the owned stream preserves these supplied values
  rather than synthesizing them from the opponent's buttons.
* 3FDEC0 updates outer wait-related counters even when no battle step occurs.

Holding only 3FCE80 is therefore not sufficient evidence of safe round
handoff. The native post-battle delegates must be classified before deferring
or committing their side effects during speculative play. Their invocation is
verified; their concrete runtime targets and all side effects are not yet
verified by the initial capture. The first follow-up scan was limited to 2 GiB
and missed the relocated clock. An expanded bounded scan then found one clock
and collected 159 samples (157 stable clock/list samples), with two callbacks
through holder vtable 37AF460 and invocation wrapper 3EF130 in every sample.

Follow-up evidence `work/native-outer-callback-targets-67876.json` (60 samples)
and `work/native-outer-virtual-targets-67876.json` (40 samples) resolves:

* Bound function 8954A4 is a virtual-call thunk. Weak-object serial/index/flags
  validated using the read-only equivalent of F823F0; object vtable 3296278
  resolves slot 5F8 to 428270. That function conditionally invokes fighter
  interface slot 110 with F0 or zero based on per-player settings 1 or 6.
  The semantic name of that setter and whether these branches are active in
  every supported match mode remain unverified.
* Bound function 3C7080, object vtable 32698A8, obtains fighter interfaces and
  calls 3BC0F0/3BC050 to populate cached per-fighter data through getters. Its
  downstream consumers and whether deferring updates is safe remain unaudited.

Fresh exports `prediction-callback`, `prediction-callback-targets`,
`prediction-weak-target` and `prediction-post-battle` contain those functions.
These concrete targets narrow the remaining audit; they do not authorize
blanket suppression or repeated invocation of the callback list.

## Still required

### Follow-up: concrete post-battle interfaces (2026-10-18)

Ghidra exports `callback-fighter-interface`, `callback-interface-targets`, and
`callback-state-access` resolve the live battle interface (vtable 327B610):
slot E0 calls 3C28F0 and creates the fighter interface at vtable 3268728;
slot 100 calls 3BDDD0 and creates the global-state interface at 3269070.
The read-only capture `work/native-callback-interface-67876.json` observed
these targets in all 40 samples.

The conditional setter is now resolved: fighter slot 110 is 3CD170, a
this-adjusting thunk to 2D3B10. It selects the player through 470DE90,
checks player+43E2C, and writes player+1B3C (16 bits) only when the guard is
zero. The caller supplies 240 for settings mode 1 and zero for mode 6.
This corroborates the much earlier TrainingFieldReset investigation in
CATCH_UP_RESEARCH.md; that implementation remains an offline diagnostic,
not an authorization to apply recorded Training settings to online play.

The follow-up read-only capture `work/native-callback-setter-modes-67876.json`
found settings [0,0] in all 40 samples, with stable array headers. Outer mode
was 10 in these samples, not active fighting mode 2. This establishes no
setter branch for that observation only, not a universal online-mode rule.

The other callback's fighter getters read player health-related fields,
meter, move data and auxiliary state. However, 3BC0F0 also updates several
reference-managed cache collections, and 3C7080 allocates/releases temporary
collections. Neither is equivalent to a harmless scalar read. Blindly
repeating these callbacks during inner replay or dropping them during
prediction remains unsupported. The native dispatcher executes them before
3FCE80, so a guard installed only at the next round's clock poll is too late.

Implementation remains incomplete: a native pre-commit route must settle
old-round corrections before callback/queue side effects, then release the
old routes and resources without racing their transport consumers and obtain
fresh bilateral agreement before rearming. NativeCombatArming still treats
an active round identity change as restart-required. No new DLL was installed
as part of this callback audit, and live prediction must not be advertised as
enabled by the owned-input helper alone.

Connect the owned raw-input stream at the audited native consumer boundary,
route confirmed publication without changing native confirmation cursors,
and integrate settlement before native post-battle side effects. Then retire
old ownership and negotiate/rearm the new round without resetting objects
under the transport worker. The current original-input replay suite remains
distinct from this unfinished prediction integration.

## Validation

### Retired-round transport (2026-10-18)

SchedulerConfirmationQueue now retains authoritative remote held/pressed words
for its bounded 7,200-record round. Owner-thread retirement closes admission
with a nonblocking producer gate, drains any already admitted packets, and
requires the scheduler's handoff-ready state plus complete truth through its
exact head. It never clears the SPSC ring underneath the producer. A retired
queue is immutable and must retain process lifetime.

A fresh queue may link to an already retired queue for the same peer, session
and local slot. Up to 16 round archives are supported; old round/event pairs
cannot be reused. Exact delayed authoritative inputs return SchedulerPublish::retired
and are marked handled by the existing transport helper. Conflicting remote
words, unknown rounds, wrong nonces/slots and unrecorded frames remain rejected.
No comparisons treat all numerically older rounds as safe. Diagnostics expose
retirement and the number of recognized retired packets.

NativeCombatSession can retain a previous session's archive when preparing a
new session, and can retire its confirmation queue only at a settled, current,
owner-thread, quiescent boundary. This does NOT yet publish a new native route
or reset/rebind the observation stream. Existing full-match profiles are not
silently switched to prediction by these changes.

Tests cover queue retirement racing 10,000 producer attempts, exact old input,
all identity fields, changed input, unknown frame, round/frame reset, reused
round identity, and an unretired predecessor. Focused combat-adapter and native
window tests passed. The existing SteamHandshake loop already treats only
full as backpressure and rejected as terminal; the retired result is consumed
exactly once by submit_scheduler_input.

Fresh Ghidra exports are in `work/ghidra-sc6-focus/post-battle-retirement-audit`
and `post-battle-consumers`. The 3FCA60 tail reads the cache returned by
3C29B0 (+200), checks keys 38/40, and calls 55A7B0. Binary UTF-16 strings resolve
55A7B0's console variables as r.Photography.Allow / r.Photography.LuxorAllow;
545EA0 reads r.Photography.Allow. Do not label this helper as combat advancement.
The separate 3FCE80 state queue, outer 1462 repeat-update flag and 14F0/F70
countdown callback still require owned continuation handling. No live hook or
installation is claimed by the retired-packet implementation.

OwnedInputPublication now supports a single transfer from a retired producer
to a new round, preserving shared count, old records and their round metadata.
The old producer cannot publish again; another producer cannot claim the same
transfer. NativeCombatSession uses this path when a previous session is supplied.
The capture's overall existing capacity still applies; this is not an unbounded
recording buffer.

CombatStartExchange also accepts an immutable completed predecessor (bounded
to 16 agreements). Exact old peer offers/acknowledgements are counted separately
without touching the current agreement; wrong old echoes are rejected without
poisoning the new round. The predecessor must have published a completed
agreement. That proves transport completion only, not native round retirement;
the owner must separately enforce the settlement and ownership guards.

The full native build passed in `build-native/round-retirement-build.log` before
the subsequent publication-transfer/start-message changes. Updated focused
tests passed in `build-native/round-retirement-native-test.log` and the combat
adapter test. A production compile/relink of both affected translation units
is tracked in `build-native/round-retirement-refresh.log`.

Still not implemented: the native outer continuation hook, committed-state
route replacement, fresh start-hold publication and round rearming. The
existing NativeCombatArming remains one-shot and stops on round identity change.
Do not install or label this as completed live prediction/round handoff.

### Pre-commit settlement integration (2026-10-18)

`PostBattleSettlement` now connects confirmation draining, a fresh adapter
endpoint, and `BoundedRollbackSession::settle_handoff`. NativeCombatSession
exposes `settle_before_post_battle` with live round/counter, owner-thread and
quiescent clock-reservation checks. Waiting never admits another frame.
Runtime status includes poll/wait counts, result (0 unattempted, 1 waiting,
2 ready, 3 restart-required), failure reason and endpoint frame. Existing
restore diagnostics and failure cleanup are unchanged.

This is an integration entry point, NOT a published native hook. The fresh
Ghidra export `work/ghidra-sc6-focus/post-battle-commit-audit` includes all
chained unwind ranges for 3FE520, 3FCE80, 3FCA60 and 3999C0. All four
decompiled successfully. Besides delegates -> queue -> post-state processing,
3FE520 can repeat the battle update via byte 1462, then decrements 14F0 and
dispatches another delegate list at F70, and handles byte 1464 through
428D30. Returning early from 3999C0 does not suspend this continuation.
The dispatcher is shared with many unrelated callers; a global suppression
hook would be incorrect.

Regression coverage in BoundedRollbackSessionTest checks reordered final
truth, fresh endpoint before restore, no additional advancement, repeated
clean polling, wrong identity/frame/thread evidence, non-quiescent boundary,
endpoint and drain failures, exceptions, and recursive entry. The native
window test compiles the integration and exercises the existing native
contracts. Neither constitutes a live round-handoff pass.

Remaining work: own the whole outer continuation, commit callbacks only at
the corrected boundary, and safely replace the one-shot route/exchange and
publication state for a fresh round. In particular, SteamHandshake currently
treats a scheduler sink's rejected old-round packet as fatal; a round switch
must distinguish authenticated retired-round traffic from invalid traffic.
Do not reset the shared observation mapping beneath its worker. No hook,
installation, or partner package has been enabled for this unfinished path.

Full Build-Native completed with exit 0 in
`build-native/prediction-delivery-settlement-build.log`. Test-TestSuite passed
in `build-native/prediction-delivery-test-suite.log`. The subsequent report-only
change was compiled and relinked with exit 0 in
`build-native/prediction-report-rebuild.log`; the staged DLL was checked for
the updated activation-blocker string. It no longer reports `none` solely
because general combat restoration exists. No installation or partner ZIP was
made; the installed RollingPredictionBackend remains unchanged.

## Continuation control protocol (2026-10-18)

`PostBattleContinuation.hpp` now expresses the ordering contract: settle final
corrections, retire the old input producer/archive, commit the entire native
continuation once, wait for a fresh fight identity, and complete bilateral
rearming. It rejects incorrect owner/token, stale peer/session/event,
generation regression, reentry, callback failure and exceptions. Retirement
and rearming can report waiting without repeating already committed work.
Counters distinguish each wait from a completed rearm.

This is NOT a native continuation implementation. No game hook uses this
controller yet, and no automatic round-rearm claim is warranted. The native
adapter must preserve the stack-local input references, remaining step count,
repeat loop, and tail callbacks of 3FE520. Neither skipping 3999C0 nor merely
returning from the inner update retains that state. Production route ownership
and the next-round resource preparation still need connecting to the adapter.

Fresh read-only Ghidra export:
`work/ghidra-sc6-focus/post-battle-continuation-audit` (workspace root).
All four targeted functions decompiled successfully with their chained unwind
ranges. The whole-function update contains stack-local input references and
both repeat and remaining-step loops, confirming the continuation requirement.

`PostBattleContinuationTest.hpp` is included in the existing bounded rollback
test. Test-BoundedRollback passed with /W4 /WX, covering ordered progress,
waiting at each safe stage, exactly-once commit, stale identities, wrong owner,
wrong token, exceptions, and recursive entry. These are control-protocol tests,
not a native replay/handoff pass. No installation or package was produced.

## Outer input lifetime audit (2026-10-18)

Fresh Ghidra export `work/ghidra-sc6-focus/outer-input-lifetime` covers 3FE520,
2DD100, 3FCD10, 1D38300, 3FDEC0 and 3FE960. Machine-code disassembly is retained
alongside it. At callsite 3FE81D: RBX owns the outer object; RSI is the number
of remaining outer steps including the current step; EBP is remaining minus
one; R12B/R13B retain edge/held suppression from BEFORE counters 1468/146C
were decremented. The three-pointer input header is at native RSP+38.
3FCD10 uses clock frame minus reverse index minus one to select input, so
resuming with newly computed loop counters would change the selected frame.

2DD100 only zeroes that three-pointer header. 3FE520 then sets it to the two
8-byte player input records and the owner's 24-byte auxiliary block at 14C8.
It is not sufficient to retain the header's address after returning from the
native function. `NativeOuterContinuationFrame` owns a deep copy and rebases
the three pointers, preserving loop counters and cached suppression flags.
It checks two-player layout, all pointer relationships, input representation,
and unchanged clock/layout observations around the read. It does not install
a hook, suspend the native stack, or authorize a resumption.

The bounded rollback test now also covers original-buffer invalidation after
capture, all sixteen read-failure points, changed clock observation, pointer
and player-count mismatch, incorrect loop counters, and invalid envelope.
These tests passed under /W4 /WX. Native continuation execution, production
route retirement/replacement and automatic fresh-round preparation are still
unconnected. The installed build remains unchanged; no live rearming test is
ready. Existing diagnostics and round-safety behavior are unchanged.

## Production route retirement connection (2026-10-18)

NativeCombatSession now accepts retirement only after its confirmation archive
is retired, the scheduler is settled and quiescent, and PostBattleContinuation
reports an entire committed continuation for the exact old identity. It then
releases retained history once and marks the bundle retired. Failure leaves
ownership blocking. terminate revokes clean-retirement permission. A retired
bundle remains allocated for delayed input and diagnostics; publishing a new
bundle uses compare/exchange against that exact retired predecessor, never an
unconditional overwrite. Preparation with a predecessor rejects an unretired
session. Clock and battle hooks distinguish cleanly retired ownership from
failed ownership, and the old arming poll does not mistake that completed
round for a new round failure. The report includes continuous_route_round_retired.

This does not yet execute or suspend 3FE520. No production caller can provide
the native continuation completion proof yet. Nor is new-round startup/hold
publication connected: a new hold must take precedence over clean retired
ownership before the new bundle activates, and stale hold/status/exchange/poll
pointers must be replaced together on the owner thread. Do not install or
claim automatic rearming based on these route changes alone.

Fresh read-only Ghidra output is in
`work/ghidra-sc6-focus/outer-continuation-execution`; all six targets completed.
Bounded rollback tests passed, including exact committed identity and failure
rejection. Native-window tests passed before the final retirement guard changes;
the full Build-Native subsequently completed with exit 0, validating those
changes, the full local test suite and both production TUs. Log:
`build-native/continuation-route-build.log`. The DLL is staged only, not installed.

Existing `BattleAfterBridge.asm` already intercepts 3FE816 and preserves flags,
volatile GPRs, XMM0-5, MXCSR and x87 control state before returning to its
trampoline. `SC6TraceBattleAfter` currently only records evidence. Extend that
existing interception if implementing native control; do not install a second
overlapping hook. Merely changing the C++ observer's return value cannot skip
or retain the outer function. A resume mechanism must also preserve native
loop state, frame layout and unwind behavior, and gate further selected-clock
advancement while a continuation is pending. The input-copy and controller
tests do not establish that ABI contract.

## Retained-stack execution candidate and successor arming (2026-10-18)

`OuterStackContinuation` executes a supplied routine on a retained Windows
fiber on the same owner thread. Unlike the earlier control model, its test
actually suspends and resumes a call stack with loop locals and destructors.
It preserves LastError across suspension, uses floating-point context switching,
rejects a changed invocation argument and cross-thread resume, and refuses to
release a suspended or failed stack with live native frames. Failed parked
instances require process lifetime. It does not convert the game's thread or
install a hook. Debug and /O2 /Ob1 /fp:precise /MD tests passed; both variants
are now in Build-Native.

This remains a CANDIDATE, not an enabled SC6 execution backend. Windows fibers
preserve the thread's TLS but switch FLS. See
https://learn.microsoft.com/en-us/windows/win32/procthread/fibers . Direct import
inventory found TLS functions and CRT imports, not direct fiber/FLS imports;
that does not exclude FLS in CRT or dynamically resolved dependencies. Fresh
Ghidra export is `work/ghidra-sc6-focus/outer-stack-continuation`; its
thread-imports.json is only a direct-import inventory, not a compatibility
proof. Rendering/engine work between suspensions and native endpoint stability
also need validation before publishing a continuation route. No production
caller or hook uses this executor yet.

NativeCombatArming now supports process-lifetime successor generations. An
owner-thread rearm provider supplies freshly validated configuration/resources;
the retired arming poll waits for native fight readiness, checks identity,
fresh event/ticket, same peer/slot/build, then creates the successor. It replaces
the exact predecessor hold, status, exchange and startup poll and constructs
the session with the previous input archive. Successors retain their predecessors'
owning chain. The old start hold retires so it cannot block between-round stock
updates; a newly published hold takes priority over the retired battle bundle.
Native-clock tests cover retired versus fresh holds and no extra advancement;
native-window tests cover fresh successor identities and stale-ticket/event,
slot, generation and clock rejection.

The live startup dispatcher does not yet register a fresh-resource rearm
provider, and the native outer hook does not yet call settlement/continuation
commit. Those are still required integration work. Previous runtime status
draining must also preserve each generation's diagnostics when replacing the
worker's current status pointer. Do not label this a completed automatic
rearming test or install it as one.

Validation update: `Build-Native.ps1` completed with exit 0; log
`build-native/stack-continuation-rearm-build.log`. After that build, the standalone
executor was tightened to initialize each fresh invocation from the host floating
point environment and to reserve 5,000,000 bytes, matching the pinned SC6 PE stack
reserve (4,096-byte initial commit). Both debug and optimized executor tests passed
again. The standalone executor is not included by either production translation
unit, so those final edits do not change the staged DLL. No installation or partner
package was performed. Diagnostic generation draining remains required before
publishing successor status in a live runner.

## Diagnostic history and fresh rearm observation (2026-10-18)

The logging worker now drains every retained runtime generation, oldest first,
within the existing 16-generation transport bound. Current summary keys remain
unchanged; retired summary keys carry the round event prefix. Late retired-packet
counts remain visible on later drains. Cross-session, cyclic, or oversized chains
are reported as invalid rather than followed indefinitely. The chain is immutable
before status publication and has process lifetime; only the logging worker drains.

PostBattleContinuation now re-observes fight readiness on each subsequent pending
arm poll. Playback waits without arming; a changed identity fails closed instead
of using the cached candidate. Commit still runs exactly once.

Tests: Test-NativeRollbackWindow and Test-BoundedRollback passed. Both production
translation units compiled and the DLL linked (exit 0), log
`build-native/continuation-diagnostic-drain-build.log`. This was an incremental
production build, not another full suite run.

Fresh read-only Ghidra export: `work/ghidra-sc6-focus/continuation-integration-audit`.
All six targets decompiled. 3FE520 invokes 3999C0, 3FCE80 and 3FCA60 inside its
repeat loop, followed by remaining-step processing. Its address is referenced
through data/vtables; the export does not establish a safe parent scheduling
boundary when a fiber yields. An after-battle observer return cannot stand in for
retaining that complete execution context.

Not installed. Native executor/hook integration and fresh-resource rearming remain
unfinished. In particular, reusing CombatResourcePreparation's ready flag is not
fresh acquisition: ReplayOwnedResources has a single callback owner and cancelled
resources must not be reused. A successor needs validated ownership and a bilateral
boundary, not locally invented frame/ticket values. These gaps remain blockers;
no activation labels or safety checks were relaxed.

## Outer caller correction (2026-10-18)

The earlier phrase "data/vtables" was too broad. The references at 0x3EB2398,
0x3EB23D8 and 0x48B382C are chained UNWIND_INFO/RUNTIME_FUNCTION metadata (32-bit
RVAs), not executable callers or vtable entries. A targeted scan found the direct
CALL at 0x3FBF61 to 0x3FE520. Ghidra decompilation and instruction disassembly of
its owner, 0x3FBF30, are saved in `work/ghidra-sc6-focus/outer-caller`.

After the call returns at 0x3FBF66, the caller queries 0x3F2840, may invoke
0x3F6FD0 or 0x3EEC20 and change owner+0x1461, updates owner+0x1440, and finally
restores its cached XMM1 argument and tail-jumps to 0x562320. Ghidra follows that
tail call in its pseudocode; use caller-disassembly.txt to delimit the audited
314-byte function. No claim about all engine work in the tail target is implied.

Consequently, returning from a fiber-wrapped 0x3FE520 while suspended would let
the caller's post-update work run on an unfinished battle. The proposed fiber
wiring is NOT approved by this evidence. Any successful suspension must retain
or block the caller too; a bounded same-thread settlement wait before returning
would avoid this premature return, but its network progress, failure handling,
and exact resume boundary still need integration. Do not publish the existing
standalone fiber executor as a native continuation backend.

No production code, executable hook, installed DLL or partner ZIP changed in
this investigation. Fresh-resource successor acquisition remains unfinished;
existing ready flags do not constitute renewed ownership. All existing round
checks and verification remain enabled. Ghidra ran read-only with no automatic
whole-program analysis. The new reference scan is a research artifact, not a
proof of all indirect callers.

## Blocking settlement and successor resource acquisition (2026-10-18)

PostBattleSettlement::wait now keeps the caller on the same call stack while
polling settlement, without a fiber switch or engine pump. NativeCombatSession
exposes wait_before_post_battle with a 10-second deadline and 1-ms pending pauses;
ready settlement returns immediately. Timeout, exception, recursion, and clock
regression are terminal. A restart-required result is NOT permission for the
native adapter to resume callbacks. That terminal hook behavior is still missing,
so the method is not connected to the native observer or enabled in a live profile.

ReplayOwnedResources::start_successor acquires distinct strong descriptor and
generated-buffer references from the still-owned prior source. It checks current
native descriptor globals and buffer/control identities before and after acquiring,
requires the old effect session to be retired on its owner thread, then replaces
only the exact prior callback owner. It leaves the old source alive for diagnostics.
The successor lazily creates a fresh effect session rather than reusing the retired
registry. A changed dependency set is refused: this API does not reconstruct or
invent owners for replacement resources. The live rearm provider still needs to
choose the agreed boundary and invoke this path after proven history retirement.

Ghidra evidence: strong-owner-release/029b9940_OwnedObjectDispose.c and
0216d8c0_OwnedControlDispose.c, resolved from the pinned control vtable 0x36E9D40.
The first dispatches destruction through the owned object at control+0x10; the
second frees the control when its deleting flag is set. This supports preserving
strong ownership during successor acquisition; it does not prove raw addresses
remain valid after releasing the old lease. Read-only decompilation completed for
both targets after correcting a leaf-function range in the analysis script.

Test-BoundedRollback passed (blocking success, timeout, exception, recursion and
existing convergence/round guards). Test-NativeRollbackWindow passed (successor
lease survives old release, foreign-thread and duplicate acquisition rejection,
effect retirement checks, and existing transaction failures). No installed DLL
or partner package changed. Native hook/failure containment and automatic provider
registration remain incomplete; this is not a completed live round-rearm test.

## 2026-10-18: startup failure containment and provider registration

NativeArmingDispatch now accepts an immutable rearm-provider registration before
queue publication and forwards it to NativeCombatArming before preparation.
Successors already inherit that provider. Registration refuses missing context,
duplicate configuration, cancellation and changes after queue publication. This
is the registration path, not a production provider that negotiates a fresh round.

Preparation exceptions now latch activation failure on the arming owner. Startup
failure cancels the native hold before terminating the session; the failed hold
continues to match its clock object but cannot service or advance it. This avoids
falling through to the original clock after partial publication. Termination
contains C++ cleanup exceptions and reports history retirement as unsuccessful.
Foreign clock objects are rejected before any retired-session rearm work.

Successor descriptor acquisition now marks its references as owned. Without that
flag, setup and resource-clear callbacks could ignore references acquired by the
successor. The Ghidra outer-caller export was rechecked: returning from 0x3FE520
still allows caller 0x3FBF30 to change outer mode/counters and enter its tail update.
These startup fixes do not make a failed post-battle settlement safe to return.

Validation: Test-NativeClockAccess and Test-NativeRollbackWindow passed, including
cancelled holds retaining ownership without service/advance, foreign-thread
cancellation refusal, immutable provider registration and rejected preparation
never invoking the provider. Production build log:
build-native/rearm-registration-containment-build.log.

Still not connected: post-battle failure containment on the actual native stack,
complete continuation-commit observation, and a production rearm provider that
negotiates the next boundary and obtains fresh resource ownership. Do not install
or describe these prerequisites as a completed live-prediction build.

## 2026-10-18: native post-battle containment entry

The existing 0x3FE816 bridge now supplies the native input header at original
RSP+0x38 (bridge RSP+0x118) without changing the native return address or loops.
FrameRecorder invokes a process-lifetime PostBattleHookGate when published for
its owner thread. Only explicit callback permission resumes the trampoline.
Callback failure, exception or recursive entry latches the first failure and
keeps the original native call stack parked; the network/report worker can still
write diagnostics. This is terminal containment requiring restart, not recovery
or successful round handoff. A stop request cannot release this unsafe stack.
No gate is published by the production session yet.

Ghidra was rerun read-only for 0x3FE520, 0x3FCE80, 0x3FCA60 and 0x3999C0;
all four decompilations completed. Evidence is under
work/ghidra-sc6-focus/postbattle-containment-audit in the workspace. The existing
0x3FBF30 caller evidence remains applicable: delegate return does not establish
completion of the outer continuation. Do not assert committed from this hook.

Optimized Test-Recorder passed, including the new assembly harness verifying the
native input-header address, preserved RAX and restored MXCSR. Test-BoundedRollback
passed callback success/failure, terminal first-failure behavior, exception,
recursive entry, and failure containment never reaching the continuation.
The report worker emits gate publication, calls, permissions, contained failures
and first failure code. Existing verification and round safety remain unchanged.

Unfinished: connect a session's settlement callback to the gate, observe full
native continuation completion, and supply the production rearm provider with
fresh bilateral admission and fresh resource ownership. No install or partner
ZIP is warranted yet. Production build log: postbattle-hook-containment-build.log.

## 2026-10-18: settlement publication and native-return rearming

The continuous-start request now installs both hash-pinned native hook sites:
0x3FE816 (settlement before delegates) and 0x3FBF30 (full outer update). Ghidra
re-exported the outer caller and its tail thunk 0x562320 under
work/ghidra-sc6-focus/outer-completion-audit. The assembly wrapper preserves
volatile arguments, FP controls, return registers and flags, calls the original
trampoline including the tail update, and only then issues a completion receipt.
It does not guess a C++ floating-point signature or skip native loops.

NativeCombatSession activation requires both installed hooks and publishes its
settlement gate after its owned route bundle. At a non-fighting boundary, the
gate copies and validates the input envelope, claims the active native call,
settles pending corrections on the same stack and retires confirmations. Failure
remains contained. Receipt verification checks the exact outer/token/owner,
current route ownership, quiescence, settled handoff and retired confirmations
before releasing history and marking the route retired. An intermediate clock
poll cannot treat a pending native return as a completed round handoff.

The production provider is now registered by NativeArmingDispatch. It checks
fresh peer nonces/slot, native clock, round event/generation, fight readiness and
battle frame, obtains successor resource leases, and creates a fresh ticket.
The successor must still complete bilateral hold/offer/ack agreement before
activation. It does not assume that observing a local boundary proves peer
agreement. Changed/unverified dependencies are rejected, not fabricated.

CombatResourcePreparation switches to the new collector only after strong
references are acquired and callback ownership moves. The previous collector
then releases its leases; its diagnostic object remains. At most 16 diagnostic
generations are retained, and the report includes every retired generation.
Cleanup failure remains terminal. Report fields distinguish native completion,
rearm result/failure/frame, and actual active successor from merely wired support.
Rearm failures: 1 ownership/cancellation, 2 peer, 3 round/ticket, 4 clock read,
5 battle read, 6 readiness read, 8 resource renewal, 9 resource exception,
100+NativeCombatConfiguration::Result for admission failure.

Validation: complete Build-Native regression suite passed; optimized native
wrapper and post-battle bridge tests passed; receipt tests cover nested calls,
out-of-order/wrong-thread returns, duplicate completion and exceptions. Resource
renewal tests cover foreign owners, failed acquisition preserving old source,
15 successors with balanced references, retained diagnostics, and cleanup failure.
The final focused regression and incremental production compile also passed.
Logs: native-handoff-provider-build.log, native-handoff-provider-final-build.log,
production-rearm-window-test.log, native-outer-receipt-test.log under build-native.

Installed and hash-verified on Ben's PC. Partner package metadata is in
build-native/native-round-handoff-package.json. DLL SHA256:
266F8E45F74582B583CFAAF23A7CD285E038CF4B69D54C9AB40C0D863D0BA6B2.
Partner ZIP: Downloads/SC6Rollback-FullMatchReplay-NativeRoundHandoff-Suite-20261018-120126.zip.
Ben's Downloads/Steam-TestSuite-Ben.cmd points to the matching package with P1,
partner friend 1593325093 and consent preset; partner package presets P2 and Ben.

Important scope: the default package still selects FullMatchReplay (original
input replay). Native prediction input delivery and native-wait bypass are NOT
connected by this change. The new handoff path is locally validated, not yet
verified in a live prediction match. Do not claim latency reduction or a live
round-rearm PASS from the existing FullMatchReplay profile.

## 2026-10-18: native raw-input delivery audit (not activated)

Re-exported 3FCD10, 3F0720, 3E9EE0, 3FDB80 and 3E2000 with Ghidra.
Evidence: work/ghidra-sc6-focus/native-delivery-audit (pseudocode and
machine-code listing). Function-body metadata for some exports is only one
byte, so the raw lookup findings were checked against its full disassembly,
3F0720 through 3F079B, rather than treating decompiler completion as validation.

The raw lookup uses two 512-entry rings at clock+3C0, 16 bytes per entry.
It optionally subtracts clock+390 after calling virtual slot658, rejects a
negative effective frame, then checks identity and frame tags. Its zero return
is ambiguous: a tagged neutral input and an absent entry both return zero.
The lookup does not itself check the validity byte used by native admission.
Consequently lookup success alone must not be treated as confirmation.

Added NativeRawInputLookup as a read-injected model with explicit tagged,
missing, invalid and unstable outcomes; it does not write native memory or
bypass waiting. Tests cover both slots, three ring wraps, neutral input,
identity/frame mismatch, delay subtraction, invalid indexing and changing reads.
Added its regression to Build-Native. Raw lookup, input-word/delivery and
prediction-frontier focused tests passed. No production DLL changed.

Still incomplete: connect the raw-input consumer route and authoritative raw
input publication to the current envelope-based transport, then allow only
verified missing-remote-input waits to advance within the prediction budget.
The existing transport publishes filtered held/pressed envelopes, which cannot
be inverted to recover raw input. Existing FullMatchReplay remains installed;
no new ZIP or installation was produced by this audit. Live prediction remains
disabled. All existing diagnostics, verification and round guards are unchanged.

## 2026-10-18: typed raw confirmation route

Added RawPredictionRoute, joining authenticated-transport raw records to
PredictionInputDelivery on the simulation owner thread. Identity includes both
nonces, round, event, slot and absolute frame. A bounded SPSC queue carries
arrivals, including arrivals before the corresponding local frame. A 7200-frame
truth archive detects conflicting retransmissions after prediction-ring reuse.
Neutral raw input is authoritative when confirmed; it is not a missing-input
sentinel. The existing envelope wire format was deliberately not reinterpreted.

Late raw arrivals recompose the retained suffix, including pressed edges of
already-confirmed following frames. Apply receives that complete history; only
a successful callback with the same revision/head acknowledges reconciliation.
Apply/publish exceptions and partial failures are terminal. Round handoff also
requires contiguous confirmed publication, applied corrections, no queued
arrivals and no future truth beyond the local head. Owner-thread diagnostic
output includes arrivals, duplicates, confirmations, applications and publication.

Ghidra raw-lookup/input-formation/wait exports were refreshed. The production
scheduler still stores filtered CombatFrameInput and rejects changed confirmed
values; that cannot simply receive recomposed edge words as new confirmations.
The native adapter must consume the owned raw-derived history consistently for
both capture and replay, and the native input formation hook must expose its
resolved suppression/filter decisions. Do not enable wait bypass before that
consumer integration. RawPredictionRoute is tested infrastructure, not a
published native route and not a live-prediction PASS.

Validation: Test-RawPredictionRoute, Test-NativeInputWords and
Test-NativeRawInputLookup passed. New route test includes reordered arrivals,
late press affecting the next confirmed frame, identity rejection, conflicts,
neutral prediction budget, callback failure/exception, future input preventing
handoff, owner validation, and a real producer thread across 7200 frames.
Added the test to Build-Native. No production DLL or installed package changed.

## 2026-10-18: shared native-consumer and replay-adapter history

RawInputHistoryView now gives both consumers the same immutable identity-bound
view of PredictionInputDelivery. CombatUpdateAdapter can attach this view before
its first update; begin-boundary, input-aware capture, and step all use its
recomposed envelope. Missing/retired/wrong-owner history fails without falling
back to the old envelope. Existing snapshot verification and counters remain.

NativeRawInputConsumer scopes reads to the admitted clock, clock identity, owner
and 3FCDCE return address. It translates explicitly supplied clock/battle origins
and obtains raw words from that same view. Other callers use native lookup.
FrameRecorder installs a signature-checked function-entry hook at 3F0720 only
when continuous startup is requested. An owned failed read is contained; the
consumer pointer is NOT published yet. Ghidra exports were refreshed, and the
3F0720 and 3FCD10 machine-code listings verify the four-register ABI/callsite.

Regression exercises both consumers with the same history: initial missing
remote input, a following confirmed press, late correction of the preceding
raw input, restore, then replay with the following pressed edge cleared. It
also checks captured inputs, auxiliary preservation, invalid identity, retirement,
foreign owner rejection and missing-history failure. CombatUpdateAdapter,
NativeClockAccess, BoundedRollback, RawPredictionRoute and NativeInputWords tests
passed. Production incremental compilation passed before the final atomic-field
cleanup; shared-raw-consumer-build.log is refreshed for the final compile.

NOT READY FOR LIVE ACTIVATION: the producer still must capture resolved native
filter decisions and prepare the frame before native consumption; clock mapping
must account for the audited conditional delay subtraction at 3F0751. The live
session does not yet attach/publish this shared view/consumer. Native wait bypass
remains disabled. Do not describe this component integration as a running live
prediction test. No new installation or partner ZIP was produced.

## 2026-10-18: resolved-filter capture and pending frame preparation

Ghidra plus disassembly verified the 3FCDCE hook point: RSI is the outer object,
R15D the slot, R12D the reverse index, R14B edge suppression, BPL held suppression,
and DIL button restriction. These flags are already resolved by native callbacks;
none are inferred from filtered input. Vtable slot658 resolves to 3E9610, which
returns clock+4404 == 0. In the admitted online mode (1), 3F0720 therefore does
not subtract the delay field. Evidence is in native-delivery-audit, including
003e9610_DelayEnabled.c and its three-instruction machine-code listing.

Added a register/FP-preserving NativeFilterBridge at 3FCDCE. The continuous-start
hook installer signature-checks that site. Its callback can substitute only the
raw EAX return and otherwise preserves state. NativeFilterFramePreparation builds
a pending frame before the first slot consumes it, records each resolved filter
once, and supplies raw history values. The native previous-raw word is checked
against the sampled pointer/value before synchronizing it to corrected history,
so native pressed-edge formation uses the same previous raw word as replay.
NativeFilterAccess validates clock identity, online mode, phase, local validity,
frame mapping, owner, round identity and fight readiness; it rejects missing local
input rather than inventing it.

At the battle boundary, both native held/pressed pairs must exactly match the
prepared frame before it is sealed with native auxiliary words. The history view
refuses incomplete envelopes, and reconciliation cannot invoke its callback for
an incomplete frame. Raw confirmation draining is deferred between slot reads,
preventing one player from consuming a different revision than the other.

Tests passed: filter preparation (including real access template compilation),
optimized assembly bridge, raw route, input words, combat adapter, bounded rollback
and native clock access. The assembly harness verifies captured registers, volatile
register restoration, replacement RAX and MXCSR restoration. Production incremental
build passed: build-native/native-filter-production-build.log. Recorder validation:
build-native/native-filter-recorder-test.log. New preparation test is in Build-Native.

Remaining live-activation work: construct/bind/publish the preparation route with
the live session, connect its authoritative raw-input producer and reconciliation
acknowledgment to the transport, and explicitly admit eligible native wait cycles.
The preparation callback route remains unpublished and native wait bypass remains
disabled. These are tested components, not an enabled live prediction match.
No install or partner ZIP was produced; the previous installed FullMatchReplay
build remains in place. All existing restoration verification and round guards
remain enabled. The old entry-level raw consumer must not be published alongside
the post-lookup preparer: preparation now owns substitution at the resolved-filter
point after native raw lookup.

## 2026-10-18: live prediction integration

The production NativeCombatSession now owns and publishes a resolved-filter
preparer, shared raw history and raw transport route after bilateral activation.
The first native filter binds the exact outer object and seeds both histories
from the native previous-raw pair. Both slot filters must seal the current
native envelope before the scheduler consumes it. Combat replay reads that same
history; filtered transport records no longer enter the live correction queue.

Raw packet kind 11/version 1 uses the authenticated Steam peer/session envelope.
Its immutable local archive is retransmitted independently of capture progress,
including while the game thread is held for settlement. The scheduler compares
raw markers; late truth recomposes the entire retained suffix, including changed
pressed edges on subsequent confirmed frames. Revision acknowledgment happens
only after the matching scheduler update/replay, without draining new arrivals.
Retired routes retain exact truth archives for delayed old-round packets.

Fresh read-only Ghidra exports and instruction listings:
work/ghidra-sc6-focus/live-prediction-publication/ (workspace root).
3FDFA0 reason 2 denotes the confirmed-input horizon; reason 3 is pacing. Native
service and its stateful wait function still run. Only an owned, fight-ready
boundary with a valid local ring entry, eligible reason and scheduler reservation
can advance while native input is missing. Both the native horizon and scheduler
remain bounded to three frames. Transition/timeout waits are preserved.

Ghidra confirmed another consumer requiring isolation: 3F6600 records tagged raw
words without checking remote validity, calls 3D73F0 and advances cursor 3B0.
The live route replaces only this matching clock's publication, sends confirmed
pairs through the same native append routine, and preserves the audited 3B4 /
3D7330 flush behavior. It never writes guessed inputs or confirmation flags into
the native input ring. Preexisting first-frame publication requires exact valid
native tags and matching raw words. The hook is signature checked.

At round handoff, the real endpoint is captured without inventing a future raw
history frame. Pending corrections settle before callbacks commit. Confirmed
recording and input routes retire before the native clock identity can reset;
resource/history release still waits for the full outer-return receipt. Fresh
resource acquisition and bilateral admission gate the next round. Unsafe failure
retains containment and requires restart; no restoration check was relaxed.

Integration testing caught and fixed history rollover: a newer late correction
must not prevent retirement of an older confirmed, published, applied prefix.
The applied-head proof now governs that retirement, while revision equality is
still required for handoff. Two slot assignments each passed 7,200 frames with
nonzero initial raw seeds and delayed confirmations (two and three frames),
verified corrected endpoints, confirmed publication and retired-round packets.
Recorder regression covers ordering, flush-after-final-publication, and rejection
of prepublished input lacking valid tags. Native-clock regression verifies service
and wait still execute, eligible advancement happens once, and budget denial
still withholds advancement. Adapter regression covers the final settlement
endpoint without a nonexistent next input frame.

Validation: Build-Native.ps1 passed (live-prediction-full-build.log). Final changed
components were retested and production rebuilt (live-raw-integration-final-tests,
live-adapter-final-tests, live-clock-final-tests, live-frontier-final-tests and
live-integration-final-build logs in build-native). Existing assembly, restoration,
RNG, camera, effect ownership, round and diagnostic tests remain in the build.

New suite profile LivePrediction invokes the continuous runtime, not profile 16
reference replay. Its handshake signature differs from the older continuous
build. No warmup is required. The experiment is capped by the existing recording
capacity and safety stops. Update counts or transport PASS do not establish live
prediction success; both real-match reports are still required.

Installed 2026-10-18: SC6Rollback-LivePrediction-Suite-20261018-124732.zip.
DLL SHA256 D0DBD387F14774688C9A47AADDFA073FE25451C5111E0238B4CA740AE4AC16A9.
Installed E: game DLL matches packaged DLL. Partner ZIP config verified: P2,
Ben friend code 283215760, consent preset, C:\Program Files (x86)\Steam game path.
Ben Downloads launcher now uses LivePrediction, P1, partner code 1593325093.
Package/install metadata: build-native/live-prediction-package.json.
No game was launched and no live two-player result is claimed by this build.

2026-10-18 startup repair
The first LivePrediction reports (39624 / 35140) both recorded
suite_wind_requested=0 and continuous_start_state=4. The live path omitted
ReplayOwnedResources::enable_wind, leaving source.effects null. NativeCombatSession
requires that service before preparation; the prerequisite remains mandatory.
Live setup now enables effect retention and animation-copy diagnostics once,
without starting the old reference replay probe. Hook installation failure is
terminal, and a new protocol signature rejects mixed packages.

continuous_start_stage: 1 clock/deadline admission; 2 source acquisition;
3 arming allocation/rearm provider; 4 arming invocation; 11 initial contract;
12 route/configuration checks; 13 battle frontier; 14 exchange setup;
15 native session/hold preparation; 16 route publication; 100 prepared.
These stages supplement, not replace, all existing diagnostics.

Ghidra read-only audit: live-startup-admission. Dispatcher RVA 3F5D20 services,
checks waiting, then advances; battle RVA 2DBC60 increments 470D0C4 at the end.
No counter offset adjustment or relaxed round boundary was justified or applied.
The observed omission is fixed; only another live run can establish that startup
and subsequent prediction now complete on both PCs.


2026-10-18 early-start packet repair
Reports 41952/26252: P1 prepared; P2 rejected an 80-byte activation packet
before publishing its CombatStartExchange. The receiver previously selected
that packet format only when the local exchange already existed.
The receiver now always consumes 80-byte input-mode packets, validates Steam
peer, mutual handshake, fresh sender/recipient nonces, build and encoding, and
defers valid early packets. No early packet grants admission or changes native
state. Existing 250 ms retransmission supplies it after exchange publication.

Authenticated kind-12 stop packets are retried for ten seconds after terminal
failure. The live worker stops on peer silence after nine seconds once queued.
Terminal reports cancel local startup immediately, with existing owner-thread
cleanup. Before any native routes are published, the owner may retire the startup
hold and resume stock dispatch. Active or successor routes remain contained;
this does not permit fallback after prediction/restoration. Existing round,
resource, nonce and restoration verification remain mandatory.
Counters: continuous_early_start_packets, continuous_stop_packets_received,
continuous_stop_packets_sent; runtime continuous_startup_hold_released.
Failure statuses now return nonzero from the live launcher.
