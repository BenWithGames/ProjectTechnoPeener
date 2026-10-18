# Supported wind layout audit and comparison policy

Pinned executable SHA256: F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553.

## Scope and evidence

This extends the prior 0x3E88CE8 audit to every entry in EffectAllocationSites.hpp.
Focused Ghidra decompilation was run read-only, without whole-program analysis.
Function extents include chained PE unwind fragments, rather than only the first
fragment. Research names are annotations, not original game symbols.

All types have five virtual entries: spatial query, initialization, advance,
initialized flag (+68) and alive flag (+6C). The two 0x130 types share their advance.

| RVA / size | Initialization and updates | Spatial query |
| --- | --- | --- |
| 3E88CE8 / 1E0 | 334960, 333E80, 332D60, 333130, 333550 | 333C20 |
| 3E88D18 / 180 | allocation 2FC720, setup 331E90, initial 3322C0, advance 332400 | 332B60 |
| 3E88C88 / 130 | allocation 334516, setup 3311F0, advance 331980 | 331DD0 |
| 3E88CB8 / 130 | allocation 3345B4, setup 3315A0, advance 331980 | 331DF0 |

The small types' initializer entry is the shared no-op at 2D2BC0. Constructors
write selected base fields, not the whole allocation. Manager 333FD0 dispatches
initialization/update/query, reads list and owner links, and removes dead objects.
Its aggregate feeds character wind force at 29310..2931C: these objects are not
ignored wholesale as cosmetic effects. Noise helpers and indexed loops were
checked as well as direct member references. The loops cover active 70..CF noise
state depending on type; none justify masking their contents.

New exports: workspace work/ghidra-sc6-focus/all-wind-audit and script
SC6AllWindAudit.java. Machine-code cross-checks: repository work/all-wind-audit.
Prior emitter exports: workspace work/ghidra-sc6-focus/wind-object-verified.

## Exact comparison exclusions

All intervals are half-open. Exact type AND allocation size must match. Existing
list-link relocation at 10..20 remains subject to shape validation.

| Type | Audited unused ranges |
| --- | --- |
| All four supported layouts | 08..10, 22..28, 34..40 |
| 3E88CE8 / 1E0 additionally | F4..F8, 11C..120, 144..148, 14C..150 |
| 3E88D18 / 180 additionally | E4..F0, 110..120, 12C..130 |
| Each 130 layout additionally | E0..120, 12C..130 |

The common gaps lie between vtable/list, two-byte base tag/owner pointer, and
lifetime/output vector. The 180 layout uses E0 for lifetime, F0..110 for position
and velocity, 120..12C for generated values, and 130..180 for matrix/history.
No object reads/writes were found in its intervening excluded ranges in the
audited paths. The 130 layouts use noise/configuration through DC, and generated
values at 120..12C; the intervening E0..120 and trailing 12C..130 are unused in
these paths. We conservatively keep other bytes strict, even some that may also
be unused. Unknown layouts are never admitted.

Cross-layout traps matter: F4 is active position in type 180 but a gap in 1E0;
144 is active matrix in 180 but a gap in 1E0; 110 is active in 1E0 but a gap in 180.
No native state is zeroed or rewritten to manufacture equality. Capture,
restoration and raw evidence remain complete and unchanged. Ownership, emitter
countdown, external references, RNG checks, round/fight guards remain unchanged.

## Recorded failures and regression coverage

Both 61160 and 47324 completed 16 cycles, then failed on 3E88D18 / 180.
The first mismatches were 24 and 08 respectively. Every observed difference is
inside the type-180 ranges above. The production comparator now accepts both
recorded pairs, plus earlier 61168/46908 pairs. Deliberately changing an active
field at 148 still rejects every archived pair.

EffectUpdateObserverTest mutates each of 1472 payload bytes across the four
layouts independently, checks combined gap mutations, wrong sizes and unknown
identities, and rejects an active output field after the gaps differ. Existing
malformed-list, dependency refusal, lifetime cleanup and round-reset checks stay.

This establishes the supported comparison policy for the audited paths. It is
not a proof of every indirect binary consumer, all future object types, all RNG
behavior, or a complete live match. Active state differences must still fail and
be investigated; no blanket pointer or float mask was introduced.
