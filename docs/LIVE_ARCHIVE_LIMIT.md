# Frame 8658: diagnostic archive limit stopped the live clock

Ben 24228: 2115 + 2264 + 2752 + 69 = 7200 live frames. Friend 26068 reached the same boundary. Neither scheduler/adapter/storage reported a correction failure; clock bridge and dispatch reported external_stop, and reservation.failed was set by stop(). NativeCombatSession's publication callback called clock.stop when OwnedInputPublication completed its 7200-record archive. The next post-battle ready check therefore failed. This corrects the initial interpretation that reservation sequencing itself failed.

Ghidra: fresh SC6PostBattleCommitAudit (3FE520, 3FCE80, 3FCA60, 3999C0) confirms the outer loop consumes native clock deltas and runs post-battle work in that sequence. The 7200 limit is ours, not a native round condition. No native sequencing or readiness condition is relaxed.

Continuous publication now retains the bounded archive prefix without marking live recording complete at capacity. Every later frame still validates identity, ownership and exact ordering; archive_omitted counts records beyond the prefix. Round retirement/rebinding permits this full but healthy buffer. The raw prediction/confirmation histories are separate and continue, as do all existing verification, events, timing, effect and round diagnostics. No unbounded storage is introduced. Legacy finite observation modes retain their completion behavior.

The worker no longer ends continuous transport merely because the diagnostic archive checksum exchange is complete. This prevents a second recording-limit stop ten seconds later. Actual session stop/failure and native round handoff checks remain.

Regression: 7300 frames, immutable 7200-record prefix, active status, exact omission count, transfer to next round after saturation, and out-of-order rejection after saturation. Existing adapter/replay tests passed. Full build and live validation tracked separately.
