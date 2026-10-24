# Build and package workflow

Release: Build-Native.ps1 runs every variant in NativeTests.psd1 before compiling (currently 70, including signed updater validation). No result caching or implicit test skipping. Run Test-UpdaterLifecycle.ps1 with SC6 closed to exercise a separate helper waiting for a synthetic game process; this never launches or terminates the real game.

Development: Test-NativeSuite.ps1 -List lists names. Use -Name AnimationListGuard,SnapshotPreflight for an affected-test run. Each invocation writes build-native/test-runs/<timestamp>-<PID>.json with scope, requested/completed tests, failures and per-test seconds. Targeted runs are not full-suite validation. Sequential execution avoids shared output races.

Package-AutomaticPlay.ps1 creates the full first-install package. -UpdateOnly omits unchanged UE4SS.dll and dwmapi.dll and requires their installed SHA256 values to match RuntimeRequirements.json before writing. Missing or mismatched runtimes require the full package. No downloads. -IncludeResearchNotes optionally includes historical engineering notes; they remain in docs locally by default. Runtime diagnostics and report archiving remain intact.

Both manual formats include the signed updater. Publish-UpdateFiles.ps1 creates a different, strictly allowlisted automatic-update ZIP and signed manifest. See SIGNED_UPDATER.md. Initial updater installation requires a manual package; old builds cannot fetch the updater on their own.

Install.ps1 -VerifyOnly validates without writing and can run while the game is open. Actual installation still requires closing it.

Measured 2026-10-22: full ZIP 6,423,234 bytes; update ZIP 668,596 bytes (~90% smaller). Both contain DLL E0EEE93699C6CAB9C498742227801FDC6B576503F6FAC130904A5831225FF469. This changes packaging and development workflow, not gameplay performance.
