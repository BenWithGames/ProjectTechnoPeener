# Correction lookup optimization — 2026-10-19

## Evidence

Friend report steam-inputs-18952: mean inclusive restore 15.4503 ms, replay 0.263972 ms/frame; corrected native outer update 19.0853 ms (9/10 over 16.67 ms). Ben steam-inputs-40452: mean restore 11.1813 ms; corrected outer 16.7485 ms. Inclusive timing buckets overlap and must not be added together. These reports predate the fixed constraint-slot build; they identify restoration as a bottleneck, not the exact savings of this change.

Fresh focused Ghidra audit: correction-lookup-audit, functions 336F80, 33A3A0, 33A990, 33AB60, 334DC0, 34BA0B, 335710. Owner lists and group nodes are native mutable linked structures. Targets are side-owned; contact slots refer to independently owned nodes. This supports temporary address indexing, not caching across updates or removing validation. Ghidra measures no performance.

## Change

Replace per-node scans of all preceding snapshot records with a capture-local bounded address index. Replace each active contact's full-record dependency search with a validation-local index of eligible target records, keyed by address and side. Keep the first matching target, as before. Continue checking its owner and every child record. Index collisions use bounded linear probing; missing/full indexes reject. No allocation, persistent cache, or native callback/order change.

All byte captures, final live-memory rereads, exact endpoint verification, raw diagnostics, ownership cleanup, and round guards remain. No prediction depth, delay, scheduling, or pacing changes.

## Validation

Constraint tests retain all 25 contact-count transitions and no-partial-write checks. Added deliberate hash collisions, full-table lookup/insertion bounds, duplicate keys, side separation, newly introduced node cycles, child corruption, and fresh-capture recovery.

Optimized MSVC synthetic benchmark: two players, 80 sway nodes each, four active contacts/node, 16 owned targets/player, 1,640 records. Byte-for-byte full-record comparison against the pre-optimization header passes. Five 200-capture runs measured baseline 339–353 microseconds versus 35–37 microseconds. This is about 90% less time in this synthetic capture path, not total restoration or end-to-end gameplay. Benchmark source and baseline are retained under workspace work/BenchmarkTransformCapture.cpp and work/TransformSnapshotBaseline.hpp.

## Live check

Start from the first lobby without warmup, Ben P1 and friend P2. Use Steam-TestSuite.cmd. Compare existing capture, load-fixup, restore, correction-correlated outer-update and subsequent interval timings. Confirm multiple correction depths and round transitions. This build includes the preceding fixed constraint-slot freeze fix, which still needs live validation. Do not call slow motion resolved until the match timings demonstrate it.

Repeat with production /O2 /Ob1 /fp:precise: baseline 382–413 microseconds, candidate 36–39 microseconds. Raw results: work/benchmark-transform-results.txt. Full capture contents remain identical.
