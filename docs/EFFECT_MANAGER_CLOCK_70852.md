# Effect-manager cached time-step restoration — report 70852

User observed the freeze during Groh's attack throw in round 3. 26 complete cycles passed; cycle27 hop1 (4598 ->4599) failed before that hop's native load. Earlier mutation makes restart mandatory. Shared-state reason4 record2036 kind3 size8 offset5: saved 0000803f0000803f; next/current/live 0000803f00c0cc3e. This is manager+08=1 and manager+0C changing 1 ->0.39990234375, not a dangling pointer or unknown wind-object layout.

Fresh Ghidra decompilation with full PE unwind/chained ranges:
- work/ghidra-sc6-focus/effect-clock-audit (six functions)
- work/ghidra-sc6-focus/effect-manager-audit (six functions)
- work/effect-clock-refs.txt (focused native instruction-reference scan, not whole-program coverage).

2DA1B0 assigns global470E038 to static manager4100B60. 2DB3A0/2DCAB0 reset manager+08 to1. 333FD0 copies global48462F0 to manager+0C before dispatching effect updates. 331180 and331980 consume manager+0C as a time multiplier for noise/effect progression;332400 also reads the manager. 385D70 and2FB2C0 write/reset battle timing. This supports a mutable cached time-step, not an immutable setting. It does not establish which exact throw instruction authored the slowdown.

The actual native.saved ranges cover48462F0 in record663, block48462D0 length88. They do not cover4100B6C. Source time scale is therefore already in the native save stream; its manager cache needs the separate restoration extension. Do not force the cache to the current global: preserve the exact saved value at its original frame boundary.

Fix: only separated side2/kind3/size8 at module+4100B68 qualifies, with parent side2/kind5/size8 at470E038, saved and live pointer exactly module+4100B60. Only bytes4..7 (manager+0C) are copied back. Neighbor+08 must remain equal. Existing source/next/current/live checks, all-record preflight before writes, complete post-write verification, exact replay endpoints, retained ownership and round guards remain unchanged. Full 16-byte manager records and arbitrary kind3 addresses gain no restoration permission. No new hook and no comparison masking.

Regression covers saved1/0/negative-zero/0.39990234375/2 values, validate-only purity, exact restoration, neighboring-field rejection, live drift rejection and wrong owner rejection without partial writes. Raw shared-state diagnostics and cycle snapshots retain all bytes. Full build and live-test status recorded in CATCH_UP_RESEARCH.

Full build and regression suite passed. Installed and hash-verified 7969A741E5F1A2D4ACCEB454D54A050F54B27557DCBE50828C9CBA1DC97A2B86; matching partner ZIP and Ben launcher prepared. Live confirmation pending.
