# Secondary animation group restoration — 71844 / 8620

Both reports reject native post-load verification in record 357, length 0x11F0,
writer RVA 30AA96, with unchanged source addresses. There are 62 differing bytes,
first offset 0x1058 (FF -> 00), last 0x109B. The earlier group-3 fix covers
0x1118..0x115B, so it does not cover this range. Neither supplied report bundle
contains the full binary payload; the exact fifteen original float words cannot
be reconstructed from the summary alone.

Fresh read-only Ghidra audit: workspace work/ghidra-sc6-focus/secondary-groups-71844.
Pinned executable SHA256 F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553.
Audited writer 30A980, loader 30AE80, reset 349290 and update 3495B0.
Loader calls reset at 30B07B, then writes back its saved controls at
player+2020/+2030 and player+2064/+2074 while leaving the group masks and
weights cleared. Reset clears secondary groups 3 and 1 through
stores 349360/349371/349387 and mask AND at 34938F. Update starts at player+97054,
advances the inner group by 0x60 and control by 0x20, then moves the outer
group by -0xC0 and control by +0x44 (3497AB..3497CE).

This yields the following independently controlled groups:

| Group | Record mask offset | Live mask | Slot | Control weight |
|---|---|---|---|---|
| 3 | 0x1118 | player+970B8 | player+2020 | player+2030 |
| 1 | 0x1058 | player+96FF8 | player+2064 | player+2074 |

Extend the existing guarded repair to group 1. Require pinned serializer provenance,
full active mask 0x7FFF, valid animation slot, finite positive threshold-qualified
control, fifteen saved weights equal to that control, matching live control,
and an all-zero or already-exact loaded group. Validate both groups on both
players before any repair write; restore and read back all 68 saved bytes.
No pointers or comparison exemptions are added. Exact post-load payload and
replay endpoint verification still follow the repair.

Tests cover all 16 combinations across players/groups, a 62-byte reset fixture,
already-exact state, provenance failure, malformed mask, wrong weights, invalid
slot, changed live control and unexpected loaded data, including no partial
repair on a second-group failure.

Raw animation.active_group evidence retains the previous diagnostic prefix and
appends schema 2/group-1 saved, loaded and verified bytes. Text restoration reports
also include group, side, reason, repair count, masks and first weight words.
The next live run must confirm that the actual failing state satisfies these
guards; the logs alone do not prove all saved weight words.

Snapshot capacity reuse, fresh local input sampling, ownership retirement,
all previous diagnostics, native round/playback gates and exact verification
remain enabled. No warmup required.
