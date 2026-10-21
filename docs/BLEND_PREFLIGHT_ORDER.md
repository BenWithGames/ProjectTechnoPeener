# Blend readiness before native restoration

Latest evidence: friend report steam-inputs-3280, frames 264 to 265, correction depth 3. Preflight mask 249 lacks blend bit 4 (input-equality bit 2 is intentionally optional for general combat). Newer hops had already invoked restoration when the oldest pair rejected. The report does not contain the exact blend subcondition or archived blend bytes, so it cannot establish which field changed.

## Ghidra audit

Read-only focused decompilation of the pinned SC6 binary:
- RVA 2F8FA0 resets seven 176-byte blend blocks, assigns their table, resets selection at +16 and +20, clears active weights through 2F7570, and selects the initial animation through 2FA080.
- RVA 2F7570 changes the 35-bit mask and writes weights when active; clearing a weight does not necessarily overwrite its old float.
- RVA 2FA080 selects an animation index from the resource; its invalid-table/index branch clears the selection pair. This does not prove arbitrary pointer or padding changes safe to restore.
- Audit output: work/ghidra-sc6-focus/blend-readiness-audit, including native disassembly and pseudocode.

## Change

The general history restorer now evaluates the identical archived blend compatibility predicate for every hop before any native load. Its existing post-load validation, exact endpoint verification, ownership checks, timer validation and round checks remain unchanged. An incompatible oldest pair can no longer be discovered only after loading the newer pairs.

Continuous reports now publish the existing blend rejection reason, side, offset, changed-byte count, saved/next values and owner identities. These fields were previously collected but not exported by this runner.

This fixes check ordering and evidence loss, not a proven permissiveness error in the blend rule. No blend restriction is relaxed. A rejected correction still stops the experimental session; this is not automatic recovery or permission to resume with uncorrected inputs. The next live report is needed to determine the particular blend transition in the failed run.

Regression coverage: both player sides reject a changed stable blend field before any native load, valid archived pairs pass, and diagnostic values survive continuous report formatting.
