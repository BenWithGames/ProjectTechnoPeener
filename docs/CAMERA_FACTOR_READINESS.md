# General-combat camera factor readiness

## Evidence and scope

The partner report `steam-inputs-42960` stopped during second-round restoration preparation: rejection 16, preflight mask 125 (127 with the input exception), missing the camera bit. Loading had not begun. That report did not export the camera readiness subcheck, so the exact failed branch cannot be established from it.

Ghidra headless analysis of the pinned SC6 executable (SHA256 F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553) independently identifies a missing legal transition in our policy. Evidence is retained in `work/ghidra-sc6-focus/live-camera-factor-audit` and `live-camera-readiness-audit` in the workspace.

Function RVA 31A890 updates both camera factors at +250/+254 on the same branch. SUBSS at 31AADC/31AAE4 subtracts float bits 3D9CAC08 (approximately 0.0765), then clamps to zero. ADDSS at 31AAFC/31AB04 adds float bits 3DCCCCCD (0.1), then clamps to one. Global mode 4846364 equal to one subtracts one after clamping and clamps again, producing zero for both factors. Stores are at 31AB52/31AB5A.

## Change

General-combat readiness now accepts the exact paired rising, falling, or forced-zero transitions. Both factors must follow the same branch and remain in [0,1]. Mixed branches, non-finite values, negative values and incorrect subtraction results remain rejected. Strict rising-only fixture policy remains unchanged. Restoration writes the exact saved factors; endpoint verification, live identity checks, loaded-reset validation, ownership and round guards remain enabled.

The continuous restore report now includes camera readiness reason, record, section, offset, side, saved value and next value. These expose the existing probe diagnostic rather than adding a new gameplay sampling hook.

## Validation

FullStateProbe regression tests compare falling results against independent SSE SUBSS/MAXSS calculations, cover clamp boundaries and forced zero, reject invalid and mixed transitions, exercise actual mock restoration, and reject stale live state. NativeRollbackWindow tests check propagation of camera readiness details. Full build validation is recorded in `build-native/camera-factor-full-build.log`.

This fixes a binary-supported policy gap. A live run is still required to establish whether it resolves the latest second-round failure. Workspace allocation optimization is separate; existing lightweight timings and all prior diagnostics are retained.
