# Inactive animation block table restoration

## Evidence and limits

Pinned SC6 executable SHA256: F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553.

The friend report steam-inputs-15336 rejected archived frames 3526 -> 3527 before native loading: combat rejection 16, blend reason 6, P2 block 5 byte 8 (flattened offset 1028). The report retained only the first differing byte (0x20 -> 0), not both full pointers. It does NOT prove that the new pointer was null or identify the exact runtime clearing caller. Ben stopped after receiving the peer failure; this was not evidence that fresh local input caused the rejection.

Ghidra and machine-code evidence:
- RVA 3034E0 constructs the bank's seven 0xB0 blocks and calls 303230 with bank+0x200.
- 303230 writes null to each block+8 and clears its mask through 2F7570; there is no release/refcount operation on the link.
- 2F8FA0 (native load/reset path) writes the wrapper table to each block+8, resets selection and clears masks. Block 5 gets an additional mask clear. This rebuild is not a transfer of table ownership.
- 2F7570 clears mask bits for zero weight without necessarily clearing the stored float bytes.

Research exports: work/ghidra-sc6-focus/blend-block-clear-audit and blend-bank-copy-audit; existing blend-readiness-audit. BankUpdate at 2F90E8 is an interior unwind fragment and is not used as evidence for the clearing caller; the full 2F90D0 export remains available separately.

## Implemented contract

Only general-combat P2 block 5 may cross between null and the same verified wrapper table while both archived states have zero selection, zero auxiliary selector and zero active mask. Every other pointer/unknown byte remains checked. Both archives must capture the same table and resource identity alongside the bank. Preflight uses only archived identities, including all-hop preflight.

After native loading, the existing archived/live wrapper checks still run. The rebuilt link must equal that verified table and remain inactive. The saved link is restored along with saved weights and masks, then exact comparison of all restored blend bytes still runs. Foreign table changes, resource changes and active transitions remain rejected. No arbitrary pointer copying, live-input change, prediction-limit change, or round-safety relaxation.

This supports an audited native inactive-link transition; a live rerun is required to establish whether the reported byte transition belongs to this class. Full table, wrapper, resource, index and mask values are now appended to continuous restoration diagnostics so any different case is distinguishable.

Regression coverage exercises both directions, missing proof, foreign pointers, active masks/selectors, wrong side/block, stale loaded state and unchanged memory after rejection. The complete native build also checks the existing low-latency path.
