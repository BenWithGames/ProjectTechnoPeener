# Outer pacing audit — 2026-10-19

Status: diagnostic correction, not a proven slow-motion fix.

Fresh read-only Ghidra audits: work/ghidra-sc6-focus/slow-outer-audit and slow-clock-audit. Machine code confirms 3FBF30 dispatches 3FE520 at outer phase +1461 == 2. The combat delta loop requires clock +478 and clear +12F3; its ordinary path consumes clock identity/frame +3A0/+3A4 against outer +1488/+148C. Alternate path +1463 calls 3F7D70 and may set +1465. Do not force delta or bypass these gates. The decompiler follows the outer tail jump into unrelated tail code: claims here use the bounded disassembly, not inferred tail pseudocode.

Ben 24228 and friend 26068 each contain about 1,500 consecutive simulation intervals separated by unmarked outer calls. Those intervals average about 34.6–34.8ms; the old summary discarded them by resetting adjacency. They add about 27 seconds over the nominal 7,200-frame schedule. Zero reported native wait counters does not explain their cause.

Schema 2 records unmarked outer calls after the first marked callback, retains adjacency, and samples the native clock and outer state before/after each callback. Cumulative owner-thread dispatcher counts distinguish absent dispatch, withheld reservations, prediction ineligibility, unadmitted steps and admitted advancement. Counts may include calls between outer callbacks; compare consecutive snapshots. Clock counters describe the hooked dispatch, not an inferred reason for every native branch. Invalid reads are explicit; none are used to control gameplay.

Records remain bounded at 16,384, allocation-free on the game thread, drained by the existing worker. Raw capture/restore/replay verification, ownership and round checks remain unchanged. Full-process timing includes transitions, so classify phase/identity before attributing a gap to combat. A live run is needed to identify the responsible gate before implementing a pacing change.
