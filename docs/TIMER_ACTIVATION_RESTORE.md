# Timer activation restoration (frames 726–727)

Reports 69464 (Ben) and 36980 (partner) rejected the same P2 slot-0 pair before any native load: selector +3C 207 -> 204, cursor +40 0 -> 4, state +48 1 -> 2. All other bytes of that slot and all five other slots were unchanged. Both archived category-zero bounds were 524. The descriptor pointer was header+40.

Pinned native evidence:
- 38B740 initializes category offsets: +18=0, +1C=header[20] (category-zero count), following offsets are cumulative counts. +08=header+40, +10=header+header[18].
- 349840 checks slot resource state, resource count, requested selector bounds and character identity; writes rate +44, cursor +40=3, category +38=0, selector +3C=requested, state +48=2. It can deactivate other slots.
- 3499D0 selects active slots matching character identity, uses category and selector to index the descriptor, and advances the cursor by rate. This explains the observed cursor 4 with rate 1.

The compatibility policy now accepts category-zero inactive-to-active transitions with unchanged resource/descriptor/table identities, unchanged category/count/rate/character/callback fields, zero saved cursor, finite positive rate, a finite new cursor >=3, and both selectors inside the archived category-zero count. Null callback is required. Endpoint phase 2 remains strict against a new activation.

Restoration copies the saved +3C selector as well as existing +40 cursor and +48 state, after validating all six slots and checking current live bytes. Complete slot equality is verified afterward. No pointers or callbacks are restored. Other category changes, rate changes and active-to-active reselections remain rejected.

FullStateProbeTest covers the reported transition and exact slot restoration, endpoint rejection, resource identity changes with no writes, callback rejection, selector bounds, negative/nonfinite/too-small cursors, and existing completion behavior. The initial test allocation was moved to the heap to avoid the Windows test executable's stack limit; targeted tests then passed.

Inline RNG, animation-copy, native state and all prior suite diagnostics remain enabled. This fix does not claim to resolve the earlier replay RNG mismatch; it allows this separately validated activation window to reach restoration/replay. Profile-12 signature is bumped to require the same new build on both PCs.
