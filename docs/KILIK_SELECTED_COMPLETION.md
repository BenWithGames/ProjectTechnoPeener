# Selected-animation completion at Kilik Soul Charge end

Matched report 506e367311fc7961f09fe5622288f49f: local frames 7490 -> 7491 rejected before native load (blend reason 6, side 1, block 5 table +8). Table changed from selected allocation to null; owner, wrapper, resource, full 35-bit mask and zero index were unchanged. Peer stopped in response. Reports did not include the adjacent timer words, so the exact timer in that captured event cannot be established from the text report.

Fresh Ghidra headless audit: work/ghidra-sc6-focus/kilik-completion-audit, SC6BlendEffectFollowupAudit.java, existing pinned SoulcaliburVI.exe project. RVA 37C230 installs the selected table, zero index/timer and full mask. RVA 37C2F0 compares the float timer at player+2B27C against the selected sample duration; on completion (controller+20 == 0) it clears the table at +2B270 and index/timer at +2B278, then enters cleanup 37C130. Cleanup changes blend weights but does not clear the full mask. Thus requiring a zero saved timer across completion is an incomplete assumption.

CombatBlend now permits selected -> null with a finite nonnegative saved timer and exactly zero next timer. Null -> selected activation retains the zero-timer restriction. Both indices remain zero, both masks must be full, and the caller must supply the unchanged controller/player dependency proof. Live resource membership, native reset checks and exact restoration/endpoint verification remain mandatory. No arbitrary pointer or allocation change is permitted. Round and match cleanup are unchanged.

Regression exercises both players, both directions, zero and progressed timers, invalid scalars, wrong table, missing proof, mask/index corruption and native-load mismatch. Successful restoration checks the complete saved block bytes. Added blend_scalar_saved/next diagnostic words for subsequent real-world failures.

This addresses a binary-proven valid completion path consistent with the report; a new Kilik Soul Charge completion test is required to confirm the reported event is resolved.
