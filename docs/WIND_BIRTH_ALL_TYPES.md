# Allocation-entry audit across all supported wind types

Reviewed the existing Ghidra exports against the StageBirth journal policy.
This audits allocation-entry dependencies, not a guarantee of all future replay behavior.

| Type | Initialization/update evidence (RVA) | Allocation-entry restoration |
| --- | --- | --- |
| Stage | 334960, 333E80, 332D60, 333130, 333550 | Preserve original four-byte accumulator at +138: update reads it before initialization. |
| ShockWave | 2FC5B0, 331E90, 3322C0, 332400 | No additional heap seed identified. Setup initializes noise phases, position, velocity, lifetime and coefficients; initial update seeds previous position. +138 is a calculated basis component, not the stage accumulator. |
| Parallel | 334430, 3311F0, 331980 | No additional heap seed identified. Constructor initializes smoothing state; setup initializes all six noise phases and their coefficients before advance. |
| RingOut | 334430, 3315A0, 331980 | Same initialization coverage with its own setup coefficients and query behavior. No additional heap seed identified. |

Exports: work/ghidra-sc6-focus/all-wind-audit and wind-origins, relative to workspace root. Small phase offsets are 70/80/90/A0/B0/C0. ShockWave phases are 70/80/90/A0/B0. Both setups initialize the associated coefficients; they still depend on restored RNG and external inputs.

The installed journal already validates birth site, size, type, order and count for all four types. Existing snapshot restoration and active-field endpoint checks remain in place. Do not copy the stage +138 policy to other layouts, or copy complete raw allocation memory (which includes constructor-owned pointers).

Added regression coverage iterating all four registered allocation sites, ensuring non-stage births are sequence-checked without payload writes. No runtime change or replacement DLL is required by this audit. Live coverage of each type is still needed; absence of a birth in a test does not prove its replay path.
