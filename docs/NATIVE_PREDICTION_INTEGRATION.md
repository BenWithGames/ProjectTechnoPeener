# Native prediction integration audit

Status: implementation in progress; latency-reducing prediction is NOT enabled.

The current FullMatchReplay suite still restores and verifies recorded inputs. Its successful runs do not establish that the older NativeCombatSession can safely run ahead of the native network clock.

## Ghidra evidence

Pinned executable SHA256: F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553.
Read-only decompilation exports are under workspace work/ghidra-sc6-focus/prediction-wait-gate, prediction-wait-dependencies, prediction-input-publication, and prediction-delay-mode.

- 3F5D20 calls service, the stateful wait check, then advancement.
- 3E9EE0 consumes wait reason 4425 and changes wait counter 3A8. Returning false unconditionally would discard protocol behavior.
- 3FDFA0 distinguishes missing input, local lead, phase transitions and stalled progress. These are not interchangeable admission reasons.
- 3F6600 publishes input from a 512-entry tagged ring, using cursor 3B0. In online mode 3E9610 is false, so no offline delay subtraction applies. Missing identity/frame tags produce zero input. Publication itself does not validate the entry's valid byte.
- 3F2B60 stores local inputs at local cursor 4410, not necessarily simulation cursor 3A4.
- 3F6770 receives remote input and rejects frames older than all-confirmed cursor 4414. Prediction must never advance this cursor or forge confirmed ring entries.
- 3D73F0 dispatches the published pair through input listeners. The exact listener-to-battle-envelope conversion remains to be resolved; two native button words cannot simply replace the ten-word combat envelope.

## Implemented instrumentation

NativePredictionFrontier reads tagged entries for the current native simulation frame, distinguishes neutral confirmed inputs from missing entries, and classifies local availability, remote availability, phase exclusions and a three-frame bound. It is explicitly a pre-service observation, not the final wait decision and not authorization to advance. Reports include bypass_enabled=0 and the remaining activation blocker. The logging worker uses bounded sequence checks to avoid emitting a torn last-sample tuple; the owner thread never waits for the logger.

Tests cover both local masks, zero-valued legitimate input, identity mismatch, 512-entry aliasing, missing local data, phase/wait exclusions and depth bounds. Existing suite verification, safety, ownership and round checks remain in place.

## Required before activation

1. Resolve and verify native fresh-input publication through listener conversion, including held/edge semantics and frame mapping. Keep predictions separate from authoritative native ring entries.
2. Integrate current NativeEffectSession lifetime/birth handling into rolling correction history. NativeRollingHistory currently retains descriptor/generated-buffer references, but does not provide the newer effect lifecycle handling used by NativeRollbackWindow.
3. Connect bounded admission and late authoritative corrections without bypassing native phase/disconnect handling or accepting stale identities.
4. Reinitialize prediction history only at verified fight-ready round boundaries; retain restart behavior after unsafe partial restoration.

Do not present the observer as an implemented latency reduction, and do not enable the dormant NativeCombatSession merely because FullMatchReplay passed.

## Envelope construction follow-up

Additional Ghidra export prediction-envelope resolves 3FCD10 and 3FE520. The outer update takes work from native cursor 3A4 minus consumed cursor 148C. Per step, 3FCD10 requests cursor - remainingSteps - 1, saves the previous envelope, reads raw ring input, and computes pressed = (previous_raw XOR current_raw) AND current_raw. It applies edge/held suppression, owner disabled mask 12F8 and optional 0xFFFFC3F0 restriction. Native callbacks can clear suppression booleans. Auxiliary envelope words are supplied separately at owner14C8. This narrows the unresolved issue to safe publication/ownership and preserving the auxiliary/callback semantics, rather than guessing an edge format.

NativeInputWords.hpp models this pure resolved conversion; it is not wired to mutate live inputs. Its test exhausts each native button bit's old/new transitions under both suppression flags, restriction and disabled mask, plus held and release cases. Existing raw-history cursor/confirmation rules still apply.
