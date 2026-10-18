# Rollback contract audit — 2026-10-17

User priority: validate rollback-facing native contracts with Ghidra, starting with the latest failure. Preserve verification, raw diagnostics, ownership cleanup and round-safety checks. A decompilation alone is not validation; pure C++ protocol/scheduler logic also needs source review and tests, rather than an invented native counterpart.

## Scope and honest coverage

The source inventory covers 190 files. `work/rollback-audit/inventory.json` (workspace root) records hashes, locations and native-code literal candidates. The first Ghidra batch produced 99 native bodies; a second selector/RNG batch produced 10 bodies, one overlapping the first batch. Both runs used the pinned game image, read-only project mode, no whole-program analysis, and PE chained-unwind ranges where available.

This is NOT a completed validation of every function. Literal scanning includes false positives (field offsets and masks that happen to lie in executable ranges), misses leaf functions without exception records and dynamically selected virtual functions, and does not enumerate C++ function semantics. These limitations remain explicit in the machine-readable inventory. Do not turn `completed=true` in Ghidra output into an audit pass.

Executable SHA256: `F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553`.

## First finding: reports 70368 / 39124

Both PCs completed three cycles. The fourth cycle's first two original-input replay hops have identical captured helper call sequences. At source frame 1113 the natural sequence has 69 direct shared-RNG calls and replay has 72. Both PCs have the same first divergence:

| Position | Natural caller / result | Replay caller / result |
| --- | --- | --- |
| 0 | 38F21B / 15811038 | 38F21B / 15811038 |
| 1 | 33371F / 3140156686 | 38F21B / 3140156686 |
| 2 | 3337A5 / 4218564545 | 33371F / 4218564545 |

Call addresses above are return RVAs. The primary extra call instruction is 38F216 in selector 38F180. Replay also has two extra secondary-stream calls returning at 3338BD in effect update 333550. The first extra selector call shifts the primary sequence consumed by later effects. Ghidra confirms that 333550 conditionally calls the secondary generator after calculations driven by primary random values; this supports, but does not alone prove, causation of the secondary differences.

Entry generator states match. Primary endpoint cursor is expected 8 / actual 9; secondary endpoint differs. Camera raw endpoint data matches on both PCs. Native payload is exact on the partner; Ben's raw padding byte differs but the native comparator already accepts this exact guarded padding case. Comparison mask 895 includes native-pass bit 512 and camera-pass bit 256, and lacks transform-pass bit 128. Do not treat that padding as the stopping failure.

Reproduction: `work/audit_rng_70368_39124.py`; results and evidence hashes: `work/rollback-audit/latest-rng-attribution.json`. Full ring history has overwritten older events, so the generic decoder labels attribution candidate-only. The relevant frame is retained after history beginning at frame 1002, with zero reported drops and before/after replay markers. No claim is made about uninstrumented inline writers or other native threads.

## Selector contracts checked against Ghidra

Fresh pseudocode: `work/ghidra-sc6-focus/selector-contract-audit/` and `rollback-contract-audit/`.

| Native function | Verified behavior | Consequence for rollback |
| --- | --- | --- |
| 34F130 | 25 state words at 485EB30 plus cursor at 485EB94; regenerates at cursor 25, then increments | Existing 104-byte first-generator capture and cursor bounds match this layout |
| 34F3C0 | Mutates six words at 470E2B0..470E2C4, four internal iterations per helper call | Existing 24-byte second-generator capture matches; one helper event is not one internal iteration |
| 34F550 | Separate scalar RNG helper | Scalar equality cannot substitute for either shared-generator check |
| 38F180 | Selection -2 draws modulo variant count, retries a previous choice up to 100 attempts, then stores the new choice at object+25C | Previous-choice state affects call count even with an identical RNG seed |
| 376B20 / 38BD60 | Observed selector calls pass player+95788 | The previous-choice field for these callers is player+959E4 |
| 38EFA0 | Initializes selector; owns allocation at +260; table pointers +248/+250 refer into it; resets previous choice to -1 after setup | Table lifetime and mutable contents must be audited; copying stale pointer bytes is not restoration |
| 38F110 | Frees allocation +260 through allocator/free and clears the pointer | A saved allocation address alone does not keep table storage alive |
| 38F180 | Selection -1 increments a short counter in the mutable table; queues selected actions | Previous choice is not the only potentially mutable dependency |
| 38F2F0 | Walks 24 entries of 24 bytes, decrements pending counters, dispatches actions, marks consumed entries -1 | Queue is 0x240 (576) bytes, not 240 decimal bytes; replay can duplicate/omit side effects if queue state is not handled |
| 30A980 / 30AE80 | Player native serializer/loader omit the selector's +95788 region; next broad block starts +95FA0 | Native save/load is not a complete simulation snapshot |
| 32AC00 / 32ACE0 | Reference record writes/reads DWORD0 and QWORD8; padding 4..7 is unwritten/unconsumed | Existing narrow padding comparison rule is supported, and already passed this run |

The actual saved native source ranges on both PCs have no overlap with either player's previous-choice field, queue, or three table-pointer fields. The source also has no explicit selector capture/restore. This is a demonstrated coverage gap, not merely an unexplained changed byte.

The helper trace does not record selector invocation identity, arguments, selected variant count or previous-choice value. Therefore an extra retry caused by forward-state previous choice is a strong explanation, not yet a proven per-invocation branch history. Two separate invocations cannot be excluded by a helper return RVA alone. No arbitrary RNG substitution or broad memory copy is justified.

Further downstream review: 38F2F0 dispatches to 3110B0, 311190 or 38F3B0 depending on entry flags. 3110B0 forwards an event to the global object's virtual +38 handler. 38F3B0 has branches writing player+198A and +972xx fields and a tail path into 2FA080 that can advance scalar RNG. Consequently this selector/queue cannot safely be classified as harmless visual-only state or excluded from determinism checks. The retained direct shared-RNG trace contains one thread and exactly one natural selector draw versus two replay selector draws for frame1113; it still does not identify individual selector invocations.

## Other native contracts rechecked in the first pass

| Area | Evidence / checked contract | Remaining limitation |
| --- | --- | --- |
| Single update | 2DBC60 runs player/animation/camera/effect work and increments 470D0C4; `SingleUpdateCall` requires exactly one increment and owner thread | This does not prove closure over every transitive mutable dependency |
| Save/read protocol | 3841E0 / 384540 build reference maps and invoke stream write +28 / read +30; `FullRestoreStream` has separate saved bytes and reader accounting | Native loader intentionally preserves/resets some values; extra snapshots still required |
| Player loader | 30AE80 preserves +244/+248/+324 across its broad block load and resets/updates other derived state | Review every special post-load compatibility rule against its exact native writer before broadening |
| Camera output | 2DBC60 calls 31EA50 and consumes camera matrices; endpoint bytes matched in this failure | Historical camera failures are not declared solved by one matching run |
| RNG hooks | 34F130/34F3C0 arguments are global-state based; wrappers preserve volatile registers, flags, XMM/control state and call originals once | Existing ABI tests do not prove every instrumented site or upper AVX state; keep separate hook audits |
| Round readiness | Current mode handler is 384EE0; existing playback/result guards remain necessary because historical playback restores Normal state | Revisit native playback/results and match-end transition contracts separately; no guard changes in this audit |

## Remaining function-by-function work

1. Resolve selector ownership/mutable state and invocation evidence above before another RNG fix.
2. Validate each native save/load extension: camera types and timers, animation blend/lookup/matrix state, RNG consumers and object-specific effect fields.
3. Validate retained-object identity, dependencies, allocator/free paths, scoped clears and retirement on every exit path.
4. Validate native update/input-ring/clock boundaries and mode/round/match playback transitions.
5. Audit hook/trampoline ABI and diagnostic overhead independently from restoration; validate source-only scheduler/network/queue functions using tests and concurrency review.
6. Map indirect native calls and transitive dependencies, which the initial literal inventory cannot establish.

No runtime code, installed DLL, comparison rule, or round-safety guard changed in this audit. No new live test or installation is needed for these read-only findings.

## Selector restoration implementation — 2026-10-17

Ghidra follow-up `work/ghidra-sc6-focus/selector-queue-audit` confirms queue reset 38EF60 initializes 24 slots when enabled; 311190 dispatches through the global event object's virtual +38 handler; 2FA080 advances the scalar RNG inline on some branches. These are simulation dependencies, not safely dismissible visual padding.

Implemented separate POD capture for both players: previous choice, all 24 pending entries, the allocation-backed selector table and its mutable short counters. Restoration writes only previous choice, queue contents and mutable group counters. Allocation, table/header pointers, enable flags and immutable table content are identities, never restored as stale addresses. Both sides must validate before any write. Initializer/disposer entry hooks serialize lifetime changes against snapshot reads/writes and invalidate saved generations even if an allocator reuses an address. Table capacity is a 256 KiB diagnostic budget, not a native format maximum.

Restoration runs after native loading and completion, before the replay update. Source restoration and replay endpoint selector state are separately verified. Raw source/expected/restored/replayed tables and queues, plus rejection diagnostics, remain in evidence. Full-match signature changes prevent mixed-build tests. Existing RNG equality, exact endpoint checks and round safety are retained.

Local tests cover the extra-draw mechanism, previous-choice/queue/counter restoration, immutable dependency rejection before writes, invalid pointer/layout/queue-owner rejection, lifetime-generation rejection and lifecycle synchronization. These establish local contracts; the last live failure's exact selector invocation is still not captured by historical logs. A live replay pass is required to confirm the fix resolves that divergence.

### Continued audit: downstream queue effects

Verified queue-reset and both dispatch branches in fresh Ghidra output. The queue's stored player pointer is checked against the two captured player identities. The global dispatcher has an indirect virtual target; whole-program/transitive closure is not yet proven. Next audit work is mapping that target and its save/load coverage, then validating native update boundaries and remaining restoration extensions against the inventory. This is an ongoing audit, not a declaration that every rollback function is validated.

Validation complete: full native build and regression suite, selector tests, evidence decoder tests and suite-summary tests passed. Installed hash 6021F58BD46EB21D0E78D4CE5A513195E00668722BDCEC059DE20678D8C683FB. Live verification pending. Package metadata: build-native/selector-package.json. The new snapshot adds bounded table storage and validation work; timing diagnostics remain active to measure its actual cost.

Live 70852: user reports freeze in round3. 26 completed verified cycles; cycle26 selector_restored=1 selector_endpoint_verified=1. Cycle27 second hop4598->4599 rejects combat preflight mask237 (wind bit16 absent; input bit2 intentionally waived), before that hop's native load. Shared-state diagnostic reason4, record2036 kind3 size8 offset5: saved 0000803f0000803f, next/current/live 0000803f00c0cc3e, address0x7ff79eb40b68; non-restorable shared field changed. Not selector rejection (phase4 reason0). Earlier hop already restored, so restart required. Collected Downloads/SC6Rollback-Report-steam-inputs-70852-56c9f34f.zip. Partner report pending; do not claim bilateral pass or completed match. Next audit the shared field's writers/consumers before changing its restoration policy.

## Effect manager time-step audit (70852)
See docs/EFFECT_MANAGER_CLOCK_70852.md. Fresh Ghidra and saved-stream coverage demonstrate missing cached time-step restoration at static manager+0C; source battle time scale is already serialized. Narrow identity-checked scalar restoration implemented, no pointer writes or comparison masking. Existing guards retained. Live confirmation pending.

Live76196 EffectClock: user reports freeze. Three cycles completed; cycle4 third hop1030->1031: native restore verified1, replaystage5, comparisonmask1023, selector restored/endpoint verified1, sharedpreflightpassed. Failure is retained effect image comparison (windfailure4, object0, offset312/0x138); emittercheck0, externalcheck0. Final sharedcache saved/next/current/live all1.0, so this endpoint does not exercise earlier1->0.4 transition and cannot prove that case fixed. Collected Downloads/SC6Rollback-Report-steam-inputs-76196-e4ed2943.zip. Partnerreportpending. Restartrequired; no newcodechange. Next inspect effect object's rawexpected/actual payload and native consumer at0x138.
Partner36908-a9929406 confirms identical cycle4 hop2 frame1030->1031 failure at retained windobject0 offset0x138. Threecyclescomplete; nativecomparison1023, restoreverified1 andselectorendpoint1 both. BinarytypeRVA3E88CE8 size480. Active words0x138/13C/140 differ: Ben expected(-587.924438,-587.924438,-0.001700899), actual(0.040040046,0.040040046,24.9749966). Partnerexpected(0.040040046,0.040040046,24.9749966), actual(19.219286,5,0.2).0x134 agrees0.040040046 both. Rawpointer/paddingdifferencesalsoexist; notallrawbytesarefailures. FullcomparisonJSON work/report-36908/comparison.json. Same activefieldfailure onboth, not proof ofrootcause. Next trace stage-wind update dependencies and input/state at0x138 withGhidra; don't mask activevalues.
