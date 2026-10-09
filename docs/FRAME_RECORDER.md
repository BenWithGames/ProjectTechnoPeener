# Native frame-entry recorder

The recorder observes entry to candidate update RVA 0x2DBC60. It records before the original routine executes, rather than polling wall-clock time. This is a diagnostic, not a complete state snapshot or rollback implementation. Input fields are observed values, not yet verified as the inputs consumed by that specific update.

## Build and test

Run scripts/Build-Native.ps1 with the existing pinned UE4SS dependencies. It runs scripts/Test-Recorder.ps1, builds the hook bridge and recorder, and stages package/Mods/SC6Rollback/dlls/main.dll. The test uses a disposable executable and synthetic memory, never SC6. It checks 600 bounded pre-update samples, 610 original calls including passive operation after completion, mixed integer/floating register and stack arguments, return values, invalid pointers, thread changes, and timeout. These tests do not establish SC6 stability or complete ABI preservation for untested calling conventions.

Vendored MinHook v1.3.4, commit c3fcafdc10146beb5919319d0683e44e3c30d537; see third_party/minhook/LICENSE.txt. The bridge preserves Win64 volatile GPRs, flags, XMM0–5, MXCSR and x87 control word. It relies on the standard Win64 ABI for nonvolatile registers; AVX upper lanes/x87 data-stack state are not a supported custom calling convention.

## Activation

1. Close SC6 before replacing the installed DLL; retain the previous DLL as a backup.
2. Launch from Steam and enter offline Training. The recorder checks the executable SHA-256 and does not install its observation hook at startup.
3. Run scripts/Arm-Recorder.ps1 -TrainingConfirmed. This checks the installed build and writes a PID-bound, single-use arm request.
4. Stay in offline Training until completion. Maximum 600 entries or 20 seconds. Do not use this research build online; automatic mode detection is not implemented.
5. Inspect Mods/SC6Rollback/captures beside the installed DLL directory. Each run produces frames-PID-ticks.csv and a matching .txt report; status.txt records activation/errors.

The first valid request installs the observation detour after checking the original prologue. The callback reads only game-state data and never changes input, simulation state, or networking. Installing the detour does modify executable instructions. The original routine executes through the trampoline. After capture the hook remains passive; the DLL is pinned until process exit so a hot mod uninstall cannot leave a dangling detour. Restart SC6 to remove the hook, and restore the backup DLL for the previous scaffold.

## Data and limits

Each row contains QPC timestamp, thread, original first argument, player pointers, outer counter, player timers, inputs, move IDs, player speed requests/countdowns/tags, animation progress, and shared speed controls. Pointer values are research references, not portable snapshot content. Reads happen on the observed game thread at function entry but do not guarantee atomicity against other game threads. A counter check rejects inconsistent reads; invalid state stops the run. File output is deferred to UE4SS on_update after sampling stops, outside the update-entry callback. Output may be delayed if UE4SS is not updating.

The fixed buffer has no allocation or disk writes in the sampling path. A contended callback does not wait; it increments skipped_entries. Inspect that count, fault, outer_discontinuities, stop, csv_written, and sample count before accepting a capture. A paused game may end by timeout with fewer than 600 samples. The frame-entry sequence is not automatically a verified global simulation-frame sequence.

First SC6 validation: idle Training, arm once, confirm movement still works afterward, inspect exactly 600 entries with no skips/faults/discontinuities. Live validation is pending. Do not progress to restore/replay until state coverage, other threads, RNG, and side effects are understood.

## Native A+G entry capture validated

After restarting Codex restored shell access, frames-5952-14807765.csv and its report were copied into project captures and checked directly. All 600 rows have consecutive outer counters, one thread (56832), and span 9.9693423 seconds. Report: no skips/faults/discontinuities; user confirmed all throws landed and SC6 stayed responsive.

Four simultaneous progress-2.0 holds occurred on move pair 487/10041, each across five consecutive entry samples: 102–106, 242–246, 397–401, 549–553. Each group contains shared/side1 scales 0 and 0.5, selector -1, and side1 countdown samples 4 and 8. These are sampled boundary states, not a proven decrement sequence or exact visual hold duration. This verifies the native recorder captures the throw-associated holds while outer updates continue. See captures/native-ag-analysis.json.
