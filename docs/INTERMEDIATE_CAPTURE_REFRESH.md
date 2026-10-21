# Intermediate capture refresh

Optimize the four intermediate captures at checkpoints 6/7/8/9 only. Initial
post-load and final post-save/replay captures retain the original full traversal.
Only general-combat restoration with separately owned effects and fixed contact
slots can use this path. There is no reuse across native updates or round changes.

The fast path rereads EVERY published record, retaining all intermediate evidence
and exact comparisons. It reuses only the preceding capture's graph enumeration.
Root list heads, group next/child links, node vtables/next links, variable link
counts, owned target vtables/next links and global manager identity must remain
unchanged. Fixed contact counts and targets are revalidated through the existing
constraint dependency validator. The original second-read stability check remains.
Unknown kinds, malformed bounds, changed topology or access exceptions fall back
to the original full capture, which retains its original rejection behavior.

Fresh read-only Ghidra audit in work/ghidra-sc6-focus/intermediate-capture-audit:
336F80, 33A3A0, 33A990, 33AB60, 334DC0, 34BA0B and 335710. These confirm the
embedded contact slots and independently owned target lists used by capture.
The intervening mod restoration stages perform checked scalar/matrix copies;
they do not invoke a native simulation update or allocate graph nodes. Even so,
the fast path checks all traversal inputs rather than assuming stable topology.

All ownership checks/cleanup, snapshot capacity reuse, fresh local input,
exact post-load/endpoint verification, diagnostics and round guards remain.
Text restoration diagnostics add intermediate_refreshes, intermediate_fallbacks
and intermediate_last_rejection. Existing restore_state_capture timings include
both paths. No diagnostic sections are removed.

Regression compares refreshed captures against independent full traversal for
all 0..4 contact counts, and exercises owner/list/child/vtable/count/manager
changes and missing contact dependencies. Local synthetic 1062-record optimized
benchmark: full capture 51.1 us versus refresh 16.4 us. This is not a live-match
or friend's CPU measurement; four of six captures are candidates, not all work.
