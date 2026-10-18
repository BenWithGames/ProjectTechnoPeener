# Bounded capture storage

2026-10-17: PID82180 verified 39 cycles, then E: reached zero free bytes. Cleanup removed 939 redundant steam-inputs binary files (5.04 GiB). Manifest: Downloads/SC6-Capture-Cleanup-20261017.csv. Small reports/CSVs, recent sessions82180/58432/76196, reference70852 and unrelated archives were retained. For82180, cycle1 and37..39 binaries remain; earlier successful cycle summaries remain. Truncated files cannot be recovered by cleanup.

Before arming, Limit-CaptureStorage.ps1 enforces a 4 GiB capture-directory budget by deleting only older steam-inputs binary evidence. It protects the newest three sessions and exact stems in retention-pins.txt. Reports, input CSVs, unrelated files and pinned sessions are not deleted. If protected files exceed the budget, startup refuses before arming. At least 2 GiB free is required.

During full-match replay, the report worker retains the first successful cycle plus the latest three verified cycles. It removes only seven named binary artifacts for an older cycle with a verified=1 summary. Every numeric summary and failed/unverified window remains. Filesystem work stays off the game thread.

Below 256 MiB free, cycle writes pause with queued owned evidence retained. New windows wait at safe boundaries; an in-progress restore/replay always finishes. Round cleanup and actual restoration-failure stops are unchanged. Historical I/O error counts remain visible, but recovered writes no longer force restart. Match completion waits for evidence flushing. suite_storage_blocked reports storage backpressure.

The 4 GiB budget is checked at startup; a running session adds bounded recent-cycle evidence (typically about200 MiB) plus small summaries. ZIPs elsewhere and arbitrary game files are untouched. Pinned references may require manual archival.

Tests cover latest/pinned preservation, old-binary deletion, protected over-budget and low-space refusal, verified-only cycle pruning, and write-failure pause/recovery. Existing verification and round checks remain enabled.
