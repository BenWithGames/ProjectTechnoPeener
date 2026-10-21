# Restoration performance audit (2026-10-19)

## Measured target

Friend report 11316: across retired rounds plus the final round, restoration averages 18.692 ms; corrected outer updates average 22.637 ms. Replay averages 0.327 ms per replayed frame. These inclusive timing categories must not be summed. Ben's corresponding corrected-update average is 13.927 ms. Optimize against the slower machine and tail latency, not only the fast host's mean.

## Native evidence

Read-only Ghidra outputs are in work/ghidra-sc6-focus/ring-cost-audit and direct-restore-audit.

- Vtable slot +0x28 for both ring types returns current(+0x28) plus element_index*64, bounded to 0x300 or 0x20 elements.
- RVA30B7D0/30B6E0 explicitly construct three aligned physical slots at stride 0xC000/0x800, initialize matrix payloads, and reset index/current/previous. This matches our header validation and distinguishes ownership metadata from payload bytes.
- Player load RVA30AE80 saves live fields +0x248,+0x244,+0x324 before reading the archived block and writes those live values back afterward. It also loads through the currently selected ring slot. Native load is therefore not a self-contained byte-copy operation.
- Root load RVA384540 constructs a reference-resolution table using current global player identities before deserializing. Retained dependencies and native routing checks remain necessary.

## Implemented small optimization

RingSnapshot::validate_owners validates the same player, object, vtable, slot layout, index, current/previous and stability checks without copying 307,200 bytes. Header-only scratch remains invalid as a payload snapshot. Restore still copies all archived bytes, captures the full result and performs the exact post-write comparison. Aliased scratch is rejected before mutation.

There are two ring restores per adjacent load. A depth-three correction avoids six unnecessary pre-write copies: 1,843,200 bytes. The optimized local synthetic fixture measured 2.2497 us for full capture plus ownership comparison versus 0.0337 us for header validation. This implies only roughly 13 us for six checks on this host, not a millisecond-scale cure and not a prediction of the friend's savings.

## Larger solutions and proof needed

1. Direct target restoration: eliminate intermediate native loads. Not enabled. Current readiness contracts and fixups are adjacent-state contracts, and the native loader preserves live values. Must explicitly reconstruct target dependencies and validate non-adjacent load behavior before replacing this sequence.
2. Reduce full workspace/default copies: profile resetting invalid/unpublished payload capacity separately, then prove every consumer obeys validity/count boundaries before omitting writes. No reset behavior changed in this patch.
3. Earlier confirmation drain: 8.5-8.9 ms mean receive-to-drain in these reports. Moving processing to a safe pre-scheduler boundary can reduce prediction horizon pressure; it cannot remove wire latency. Must preserve single-owner scheduling and avoid native operations in the receive worker.

Regression tests cover unchanged header-only payload, invalid snapshot semantics, alias rejection, invalid indices across all four groups, full restore equality, and all existing native restoration guards. The known changing blend-table pointer is deliberately not relaxed by this performance patch.
