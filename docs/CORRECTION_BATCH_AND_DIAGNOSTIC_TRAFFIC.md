# Correction admission and memory traffic

Final owner-thread confirmation drain runs in NativeCombatSession's boundary-reader callback, after both native filters complete and before begin_boundary/capture/reconciliation. It is bounded by the existing packet queue and never sleeps. RawPredictionCoordinator refreshes only the still-unexecuted revision/head token, then the normal scheduler reconciles all admitted truth. Arrivals during replay still wait until a safe boundary; they are never applied halfway through a replay or falsely acknowledged. Tests cover both slots, two late packets, one correction, conflict rejection, and rejecting refresh after execution. Existing adaptive delay and prediction limits remain unchanged.

Adjacent preparation previously copied the same two ring payloads twice into FullStateProbe slots0/1 and2/3. Audit of all ring consumers shows the adjacent load/verification path reads slots0/2; slots1/3 are serializer scratch captured before use by the separate delayed fixture path. Remove only those duplicate scratch copies (614400 payload bytes per preparation), explicitly invalidate scratch payloads, retain exact source/endpoint captures, rotation/ownership checks, and final comparisons. Tests poison scratch bytes, verify they stay untouched/invalid, and exercise the real mock save/load/endpoint validation. Reader trace preparation initializes source/writer and zero destination/reader in one pass instead of copying then rewriting metadata. No native serialization order or lifetime changes; no pointer aliasing into retired history.

Diagnostic formatting, binary report writes, compression and upload remain off the simulation thread (no automatic upload). RollbackJournal now separates producer and consumer cursors by64 bytes and drains up to64 immutable events per batch, releasing queue capacity before stream formatting. No event sampling/removal; overflow counts remain explicit. Regression covers wraparound, overflow, partial batches and concurrent producer/consumer integrity. This targets collection/queue overhead; it does not claim disk I/O was previously on the combat thread.

Diagnostics add continuous_pre_execute_receipt_checks and continuous_pre_execute_receipts. Existing full-update/correction timings remain the performance test. Copy byte reduction is deterministic; milliseconds and jitter reduction need live paired reports. Previous GameSet and ZIP lock fixes remain included.

## Startup callback-order regression (2026-10-23)
Paired match b286b0de17beefcdb4693be0dea22773 failed on both PCs at head 0:
coordinator_failed=1, driver_failure=reader (2), pre_execute_checks=0,
filter_sealed=0, native_updates=0. The boundary reader invokes filter completion,
which calls before_step and prepares the token. Calling before_execute before
that reader rejected the first update.

NativeBattleBoundaryReader::read_for_execution now owns the ordering: validate
and read/seal the boundary, then refresh confirmations, then return to the driver
for begin_boundary/capture/reconcile/execution. NativeCombatSession uses that same
method. No guard, delay, copying optimization, or diagnostics are removed.

LiveRawIntegrationTest exercises real filter preparation, the global completion
route, production reader and coordinator through the first two updates for both
slots and delays 0/1/2. Invalid edges and wrong callers still reject before
simulation. Reversing the shared method back to the old order makes the startup
assertion fail (callback-order-mutation.log). Correct order passes. Native memory
access is mocked; live-game validation is still needed.
