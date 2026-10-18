# Wind creation origins

Read-only investigation of the pinned executable (SHA256 F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553). No game code, hooks, comparison rules, diagnostics or round guards changed.

## Native identities and creation paths

These cover all four concrete types currently supported by EffectAllocationSites.hpp. Names below are native MSVC RTTI names, not names inferred from appearance. Base Iw_WindBase at vtable 0x3E88D48 is used during destruction and is not a fifth supported concrete allocation.

| Native type | Vtable / allocation | Origin |
|---|---|---|
| Iw_WindParallel | 0x3E88C88 / 0x130 | Baseline wind setup; created together with RingOut by 0x334430. Query 0x331DD0 returns its current vector without a location-dependent calculation. |
| Iw_WindRingOut | 0x3E88CB8 / 0x130 | Same baseline setup. Query 0x331DF0 scales its vector by a region value obtained from X/Z through 0x3300C0. It is not allocated only upon a ring-out event. |
| Iw_WindForStage | 0x3E88CE8 / 0x1E0 | Repeated births from registered stage-emitter configurations. This is the type in failure 76196/36908. |
| Iw_WindShockWave | 0x3E88D18 / 0x180 | Character move/event processing: activation of a frame window in character data invokes 0x2FC5B0, which creates the shockwave. |

## Parallel and ring-out setup

Reset paths 0x2DB3A0 and 0x2DCAB0 call 0x334750. That routine selects initial wind parameters, enables global 0x4846450, and queues manager callbacks to clear/recreate the baseline wind objects. Callback 0x334430 waits until the manager list is empty, then allocates Parallel at return site 0x334516 and RingOut at 0x3345B4. Constructors use setup routines 0x3311F0 and 0x3315A0, with randomized parameters. Both advance through 0x331980.

RingOut query passes the sampled X/Z to 0x3300C0. That helper checks active world/stage flags, queries region data via 0x3915A0, and returns a region record's low four bits. The result scales the wind vector. Native naming and region dependence are confirmed; this does not prove the vector determines ring-out gameplay or its exact visual purpose.

## Stage emitter path — current failure

1. Bridge 0x3B3110 forwards a configuration pointer to 0x2DAEB0.
2. 0x2DAEB0 copies configuration fields through +0x4C into an owned entry in pending list 0x470E9E0.
3. Reset/setup calls 0x2D9F30. It clears the old runtime emitter list, copies pending configurations, initializes each with 0x334860, and installs them in list 0x470F1C0. Pending entries are then released.
4. 0x334860 derives runtime position, orientation and parameters from the configuration, resets the emission timer +0x5C, and establishes an initial catch-up count of 0xB4. This is emitter state, separate from each spawned object's +0x138 accumulator.
5. Game update 0x2DBC60 iterates runtime emitters and invokes 0x334960. When the emitter timer expires, it allocates Iw_WindForStage, invokes 0x333E80 to configure the object, and links it into the wind manager.
6. 0x333FD0 initializes and advances the new object, then queries its spatial contribution.

The latest mismatch is therefore a stage-emitter child, not the character shockwave type. The source configuration and its native class establish this without guessing from the move visible when the game stopped. Exact named stage asset/emitter identity is not present in the saved allocation evidence. The bridge appears in a native function-pointer table at 0x327B668; its upstream asset/engine caller is not identified by the direct-call scan. Do not label the emitter as snow, smoke, grass or a specific stage prop without further evidence.

The +0x138 birth-state problem documented in EFFECT_ACCUMULATOR_76196_36908.md remains relevant. Identifying this origin does not fix its uninitialized accumulator or justify omitting it.

## Character shockwave path

0x300620 reads the character's current event descriptor at +0x44058 and its timing state at +0x44068. It compares frame values with descriptor start/end shorts at +0x36/+0x38 and calls 0x2FC5B0 on an inactive-to-active transition. A related path at 0x300B60 reads a secondary descriptor from the character resource at +0x455C0, using timing fields around +0x44DC8; it invokes the same creation routine when its active flag changes.

0x2FC5B0 checks the wind enable flag, derives strength/type parameters from descriptor flags and shorts, uses character orientation/state (including +0x94 and +0x1354), allocates the 0x180 object, and configures it through 0x331E90. This is a data-driven character event, not a hardcoded Grøh-only or Yoshimitsu-only path. The trace does not map individual move IDs or prove every attack generates a shockwave.

## Common destination and limits

All four enter manager 0x470E038. Update 0x333FD0 runs initialize/advance virtual methods, removes dead objects, queries the list at sample positions, sums/clamps its contributions and writes per-character vector fields +0x29310 through +0x2931C. That establishes a common character-facing output, but not every downstream cloth, hair, animation or gameplay consumer. Avoid claiming these are purely visual or that they move the fighters themselves.

Native class identities had also been recorded in older RESTORE_PROBE.md and CATCH_UP_RESEARCH.md notes. This investigation revalidates them and connects creation paths; earlier conversational uncertainty about whether the latest object was stage or character wind was unnecessarily broad.

## Evidence and coverage

Fresh read-only focused Ghidra exports are in workspace `work/ghidra-sc6-focus/wind-origins/`, using scripts SC6WindOriginsAudit.java and SC6WindOriginsFollowup.java. PE chained-unwind fragments were included. RTTI identity/self-RVA validation is saved in rtti.json. Additional setup/query exports remain in all-wind-audit and effect-manager-audit.

Full-reference scan walks PE unwind-covered functions and validates candidate relative references with Capstone; results are full-references.json. It is not complete coverage of leaf functions or indirect calls. A separate raw relative-call candidate scan found the 0x3B3110 leaf bridge, whose instructions were independently decoded. No additional concrete vtable constructions appeared in the covered references. This is coverage of the four supported types, not a proof that no other wind subsystem exists in the entire game.

No live test, rebuild or installation was required for this trace. All existing safety and verification behavior is unchanged.
