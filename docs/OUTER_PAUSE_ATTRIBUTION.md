# Outer update pause attribution (2274)

The paired failure logs establish occasional long wall updates and outside gaps,
not their exact native subcall. Local frame2479 took39.7448ms with5.7553ms restore;
frame6020 took25.0379ms without correction. Peer frame2990 started385.6942ms after
the previous start, neither adjacent update corrected. Do not label this gap
network wait, CPU work, OS preemption, or round startup without more evidence.

Read-only Ghidra audit: work/ghidra-sc6-focus/outer-pause-2274.
3FBF30 dispatches3FE520 in phase2 and has further native tail work (562320).
3FE520 can run repeated callbacks at outer+1450 vtable+68, followed by3999C0,
3FCE80 and3FCA60 before returning to the outer tail. These operations must not
be skipped to save time: existing continuation/ownership guards still apply.

No new machine-code hook or gameplay wait change. Existing OuterEnter,
BattleAfter and OuterReturned callbacks now partition QPC wall time into entry
recovery, time to each battle return, post-battle callback, and remaining tail.
Each segment includes all work between boundaries (including scheduling stalls),
not a claim of exclusive CPU duration. Multiple native loop iterations accumulate.
If no battle boundary is observed the tail includes the remaining outer call;
per-segment call counts identify this case. Inclusive restore/capture/replay spans
remain separate and must not be summed with the partition.

Found and fixed permanent telemetry saturation:16384 slots previously filled once
per process, then dropped all later rows. Slots now recycle only after the single
report worker copies a ready row. Bounded multi-producer CAS reservation, acquire/
release publication, no overwrite of unread data; actual queue-full drops remain
counted. Worker formats its local row copy off the game thread. Final successful
report drains the pending partial batch before closing. Schema4 appends columns;
existing columns/counters remain. No allocation or file I/O in callbacks.

Tests: exact segment sums, multiple battle callbacks, three capacity wraps,
unread-full drop and subsequent recovery, two concurrent producers/report worker,
invalid clocks/reentry and existing admission identity cases. Adaptive delay,
three-frame prediction limit, exact snapshot verification and safety unchanged.
This is attribution/coverage work; no measured live speedup or resolved pause is
claimed until the updated timings are captured in a match.
