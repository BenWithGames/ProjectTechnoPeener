# Block 5 inactive-to-selected activation

Failure: paired match 2274f26f3032eb009d551fd36f787234, local PID 66924,
round 5, native frames 12799 -> 12800. Combat preflight rejected before native
load: block5 +8 null -> selected, mask 0 -> 0x7FFFFFFFF, scalar 0 -> 1.0.
Stable wrapper/resource/owner. Friend PID 28436 reached the prediction limit
following the local stop; its report contains no restoration rejection.

Ghidra read-only audit on the pinned executable:
- 37C230 installs the controller-selected table, resets index/scalar, then calls
  2F7570(1.0). The constant at 3E8A31C is 0x3F800000.
- 2F7570 sets all 35 mask bits and weights for 1.0.
- 37C2F0 calls activation when the controller is not initialized, then may
  advance the scalar by 1.0 in the same update (subject to native playback).
- 2F8FA0 native load rebuilds the wrapper default table and clears mask/timer.
- 37C570 selects the table from resource section 2. Existing bounded membership,
  live dependency checks and controller restoration remain required.
Evidence: work/ghidra-sc6-focus/block5-activation-2274 (six decompilations and refs).

The new selected_activation_pair accepts block5 only, saved null table/index/
scalar/mask, next verified selected table/index0/full mask and scalar exactly
0 or 1.0. Existing active_selected_pair delegates to it; this consistently covers
archived preflight, live dependency verification, post-load reset validation and
exact source restoration. No controller dependency is copied or guessed. The
existing restore copies saved pointer/index/scalar/mask/weights and compares all
saved block bytes. Endpoint verification, round guards and adaptive delay are
unchanged. Prior full-mask completion rules are unchanged.

Regression: both sides; timer0/1; exact restored bytes; wrong table, wrong mask,
wrong index/scalar, unexpected loaded state, missing selected proof, wrong block,
and noncombat mode reject before writes. FullStateProbe dependency test covers
archived and live membership for this new pair. Removing the new allowance makes
the targeted FullRestore test fail; restored code passes. Live match not yet run.
