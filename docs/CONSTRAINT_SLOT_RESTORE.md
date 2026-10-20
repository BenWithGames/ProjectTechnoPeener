# Collision constraint activation and fixed-slot restoration

## Failure addressed

The 40452 report stopped before loading at transform record 1283: 2045 saved records versus 2061 newer records. The first difference was a sway node header versus an additional constraint record. The friend (18952) had no local restoration failure and stopped after receiving the peer-stop message. This is separate from correction-time frame-budget overruns measured by OuterTiming.

## Binary evidence

Pinned executable SHA256 F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553. Fresh Ghidra exports: work/ghidra-sc6-focus/constraint-topology-v3, produced by SC6ConstraintTopologyAudit.java in read-only mode. Research names are annotations, not original symbols.

- Group solver RVA 336F80, particularly 337CAA..337Dxx, adds a collision contact when a query succeeds, the object is not already present, and fewer than four contacts are active. It increments the existing sway node's +DA count and writes the target pointer at +120 + slot*60 +50. The solver scratch containing the corresponding list/count is stack-local, initialized again for each call (3382C0).
- Constructor 33A3A0 initializes four embedded slots at +120 with stride 60. It sets +DA from descriptor +4D. Binding 33A990 translates initial descriptor indices into object pointers. Therefore +DA is not an allocation count or an immutable topology field.
- Native owner update 34BE26..34BEC7 walks owner+08 and owner+18, following node+28, before updating sway groups at owner+10 and owner+20. These lists provide independent identity for targets even when no current constraint references them.
- KSwayHitNode vtable RVA 3E88E38 update 335710 builds +40 matrix from +10 position and +C0 orientation, optionally multiplies by the selected bone matrix, then produces the inverse at +80. Existing captured inputs/outputs remain captured.

The logs establish the old flattened-layout rejection; the binary proves a normal activation path that invalidates the old constant-count assumption. A new live run is still needed to verify this fix against that match.

## Implementation

Production FullStateProbe uses fixed_constraint_slots=true. Legacy snapshot captures remain available for existing research fixtures. Mixing layouts fails same_topology.

Each exact supported sway type (3E88DB8/3E88DF8) records all four embedded slots as kind25, 58 bytes each. The trailing eight bytes outside these fields are not written. Inactive pointer values are retained as opaque bytes and never dereferenced. Active pointers must resolve to an independently captured same-player KSwayHitNode in the owner's native lists. Node headers and next links are kinds26/27; target matrix/input records retain kinds18..21.

The restore validates all records before writing, restores slot contents first, then publishes the saved +DA count. Sway-vector restoration leaves that count for the constraint-restoration phase. D8 link count/links, native owner-list identity, target type, bounds, live bytes, source shapes and round checks remain strict. Actual node addition/removal and unknown target types are still rejected. This does not recreate or blindly retain arbitrary objects.

Exact endpoint verification and all existing raw diagnostics, timing, ownership cleanup and round-safety checks remain enabled. There is no new sleep or hold.

## Validation

Test-ConstraintSlots covers all 25 count transitions from 0 through 4, exact round-trip slot/count restoration, ignored inactive invalid pointers, wrong/deleted target ownership, unknown target type, cyclic list, invalid counts, stale late-slot mutation with no partial write, and incompatible snapshot layouts. Test-FullRestore exercises the existing full preflight/load/post-load orchestration with the production layout. The test is included in Build-Native.

Live verification: both install the new ZIP/build, launch fresh games, use the first private lobby without a warmup, P1 Ben / P2 friend, and run Steam-TestSuite.cmd. Play normally. The whole-outer timings remain available for the separate slow-motion diagnosis.
