# Sway owner control restoration (2026-10-22)

## Failure

Local report match-b737ec0af17121d2f2da0bfac4b336d2 completed 41 corrections
(82 replayed frames), then failed the D78 control guard at owner offset 0x38.
The prior guard incorrectly treated changing control values as immutable.

## Pinned binary evidence

Executable SHA-256 F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553.
Read-only Ghidra: sway-control-audit, sway-control-writer, and earlier
sway-d78-callers/dependencies, under work/ghidra-sc6-focus.
Capstone cross-checks: sway-control-owner-uses.txt and sway-control-field-uses.txt.

- Owner is player+0x29120 (initializers 0x2D4419, 0x2DAAB6, 0x303F70 and
  native player load 0x30AE80).
- 0x34A49C / 0x33AB60 stores references to owner+0x2C and owner+0x38 in
  D78 nodes. 0x33AC80 reads the mode and 0x33AF60 uses the blend for matrix
  interpolation and its floor branch.
- Writer 0x308310 changes player+0x2914C (owner+0x2C) to 0, 1 or 2.
  It updates history at player+0x1B84 and copies the result to player+0x29158
  (owner+0x38). Depending on character/state branches, it selects 1 or fades
  the previous value toward zero. It is mutable per-update state.
- Assembly writes at 0x3083D6/0x3083DE and 0x3084E0/0x3084E8 confirm the
  history/output relationship. Normal callers include 0x2DCC35 and 0x341CDF's
  continuation. This is not a pointer or a fixed configuration constant.
- Native player load 0x30AE80 restores player+0x90 for 0x3510 bytes, covering
  history at 0x1B84 and the input state flags. It does not restore these owner
  outputs as part of that block. We do not rerun 0x308310 during restoration:
  it advances history and has other side effects.

## Restoration

The owner snapshot already contains both outputs. Root restoration now writes
only their saved four-byte values alongside its existing wind vector, before
sway replay. Mode must be one of the audited 0/1/2 values and blend must be finite.
Post-load current values must match either the saved or next value exactly;
unexplained third values reject. Live memory must still match the captured current
state. All unknown neighboring bytes remain exact. All roots validate before any
write, and complete root byte equality is checked after writes.

Sway validation permits these captured changes and recognizes the already-restored
owner state at its later stage. Reference/dependency checks, finite checks, exact
endpoint verification and round safety remain. Diagnostics append full control
saved/next/current/live bits and the control offset without renumbering old fields.

Regression coverage includes both sides, all nine mode transitions and five blend
values, actual root -> sway -> constraint restoration order, preflight without
writes, exact final records, third-value rejection, stale reads and unchanged
neighbor guards. Full release tests are required before installation.
Live match validation remains pending.
