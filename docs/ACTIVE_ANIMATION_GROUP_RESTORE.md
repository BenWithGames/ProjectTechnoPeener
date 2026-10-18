# Active animation group restoration — 73052 / 2292

Both machines completed 36 verified cycles. Cycle 37, source 6076 to 6077 (hop 2), failed exact post-load comparison in native record 357, length 0x11F0. Mask +0x1118 was 0x7FFF before load and zero afterward; the fifteen floats at +0x1120 through +0x115B were all 0x3E99999A before load and zero afterward. Sixty-two bytes differed. Camera/wind post-load checks passed.

Fresh Ghidra exports: work/ghidra-sc6-focus/active-group-73052 (workspace root), functions 349290, 3495B0 and 2F5E60, pinned executable SHA256 F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553.

349290 deliberately resets secondary animation group masks and weights. 3495B0 rebuilds group 3 from the selected slot at player+2020 and control weight at player+2030, setting all fifteen weights and the low fifteen mask bits when the control reaches the native threshold. Our post-load save/verification occurs before that update, exposing the reset.

ActiveAnimationGroup restores exactly the 68 saved bytes at player+970B8 before the post-load writer. It checks source record identity, writer/reader RVAs, the matching player prefix, active mask, slot, finite positive control, all fifteen weights, matching live control, and the all-zero loader reset (or already-exact state). Both players are validated before either is written. Readback is exact. Unsupported layouts reject; no comparison exception is added for active groups. The existing inactive compatibility rule remains unchanged.

Deferred evidence section animation.active_group retains reason, side, record, restored count and both players' saved/loaded/verified bytes. Reasons: 1 stream bounds, 2 provenance, 3 mask/prefix identity, 4 control domain, 5 derived weights, 6 live control mismatch, 7 unexpected loaded bytes, 8 readback, 9 access exception.

Full-match replay scheduling, ownership cleanup, diagnostics, round/fight gating and exact endpoint verification remain enabled. Local regression success is not live-match verification. No latency-bypass or prediction behavior is introduced.
