# Rolling prediction backend integration — 2026-10-18

Status: backend changes implemented and under validation. This does not activate latency-reducing run-ahead in the installed FullMatchReplay profile.

## Changes

NativeRollingHistory now uses the same NativeEffectSession bindings as NativeRollbackWindow for general combat capture. Captures retain effects, emitters and external references, verify the captured image, and reject owner changes. Retirement drains effect references through NativeSnapshotPool before releasing descriptor/generated-buffer ownership. Teardown finishes the effect session only after all bindings have drained. The production NativeCombatSession preparation path now refuses a source without effect retention.

NativeHistoryRestore validates paired effect ownership before any loading. Each reverse hop restores the retained effect image and verifies it after the native load, with selector dependency validation and the existing guarded inactive-animation compatibility rule. Unpaired/unowned effects are regression-tested to reject before native loading.

The birth journal now supports removing a prefix no retained snapshot needs, and truncating an obsolete prediction suffix after rewind. Rolling history rebases all retained birth cursors, requiring sole ownership of all outstanding effect bindings. Edits use the allocation hook's nonblocking direct-memory gate. A corrected branch records new allocations; it does not copy seeds from an obsolete predicted allocation sequence. The existing original-input begin/finish sequence verification remains unchanged. New counters report discarded events and branches.

Network confirmations are now drained after successful clock service even when native waiting is active. This does not consume a battle reservation or bypass native wait. Identical authenticated retransmissions are accepted as duplicates while the corresponding confirmed frame remains retained. Old conflicting input, expired/aliased history, wrong session/round and queue failures retain their rejection behavior.

## Ghidra evidence

Read-only exports in workspace work/ghidra-sc6-focus/prediction-confirmation: 3FEBA0 advances all-confirmed cursor4414 only while native slot6B0 accepts all-mask43FC with required-valid argument1. 3FE8F0 similarly advances local cursor4410 using local mask39C. 3F6770 receives remote inputs against that confirmed frontier. These reinforce the decision to keep prediction storage separate and never mark predicted entries as authoritative native confirmations.

## Validation limits and remaining integration

Pure/fake-engine tests exercise 7,200 frames of bounded birth history, correction convergence, late/reordered and duplicate inputs, both slots, neutral input, owner/round rejection, and failure latching. They are not a live SC6 prediction pass. New corrected branches still need live determinism evidence for newly allocated effects; a naturally read allocation-entry value must not be represented as independently verified across peers.

NativeClockAccess still respects native waiting. NativeCombatArming still stops on round identity change; it does not safely rearm the next round. Thus native run-ahead admission/publication and quiescent round handoff remain incomplete. Do not enable or market this build as full-match latency-reducing prediction. FullMatchReplay continues using the existing verified recorded-input replay path, with all diagnostics and round guards.
