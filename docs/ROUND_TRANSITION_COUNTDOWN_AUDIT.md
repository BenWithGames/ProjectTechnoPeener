# Round-transition countdown audit — 2026-10-22

Historical investigation record. The subsequent implementation is documented
in [ROUND_TRANSITION_RESERVATION.md](ROUND_TRANSITION_RESERVATION.md); this file
records the evidence collected before that implementation.

## Measured pause

The latest completed local match examined was
`match-dc7bdd378aa9a9c3e94e86be14895701-70140-33400546.zip`.
Its four startup hold intervals were 283.3144, 233.1898, 199.9420,
and 216.5351 ms. Successor preparation itself was 0.1167–0.1424 ms.
The report contains cumulative traces: these numbers select the latest match,
not the earlier match's allocation and startup work.

Round intent and retained dependencies are already prepared in Start.
CombatStartExchange still requires actual local and remote holds before
proposal/ack/commit and timed release. Moving allocation earlier alone does
not remove this wait. Retiring old snapshots four wall-clock ticks after a
round result is not a substitute for settling their outstanding inputs.

## Native evidence

Pinned executable SHA-256:
`F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553`.

Focused Ghidra output is in workspace
`work/ghidra-sc6-focus/transition-countdown-audit/`.

* RVA 2D9CD0 applies a pending battle mode, runs its update, increments the
  mode counter at +8, and calls its transition check. It repeats when that
  check publishes another pending mode.
* Start update RVA 384040 must continue running. It locks inputs initially
  and enables associated state at counter 2.
* Start transition check RVA 3840E0 compares elapsed +8 against duration +C.
  At completion it publishes Normal through global RVA 4843ED8. Normal can
  consequently execute in the same native battle call as the last Start update.
* Start entry RVA 3835B0 has duration assignments of 156, 120 and 3. A fixed
  two-second assumption would be incorrect.
* Battle update RVA 2DBC60 advances the battle counter at RVA 470D0C4 after
  the mode update. Countdown and battle counter are not an atomic pair when
  read externally.

## Read-only live capture

`work/transition-countdown-live.json`, PID 77056, contains 6,584 changed
observations over 180 seconds. It did not write game memory or invoke replay.

| Start battle frame | Duration | Last observed Start | First observed Normal |
| --- | --- | --- | --- |
| 87 | 120 | frame 206, elapsed 119 | frame 207 |
| 1872 | 120 | frame 1991, elapsed 119 | frame 1992 |
| 5830 | 120 | frame 5949, elapsed 119 | frame 5950 |

`battle_frame + duration - elapsed` had two adjacent values within each
countdown: 206/207, 1991/1992, and 5949/5950. Re-reading the same fields before
and after does not eliminate a stable interval inside the native update.
These observations therefore support the countdown interpretation but do
not independently authorize a future frame from an external sampler.

## Integration requirements

Early agreement must reserve a future owner-thread boundary while allowing
Start to advance. It must not announce the existing physical-held flag while
the clock is actually advancing, or consume a timed agreement whose release
expired before the actual boundary.

The reservation needs exact round/event/generation, clock identity, projected
battle and clock frame, owned-resource validity, and immutable peer/delay
agreement. Recheck at the actual first permitted Fight boundary. A changed
countdown, skipped intro, pending mode, resource invalidation, missed target,
or unavailable peer must preserve a bounded safe path. Existing snapshot,
playback/result, settlement, exact verification and ownership checks remain
required. The present held-boundary exchange remains in place until this
reservation and its cancellation/retry protocol are implemented and tested.

No pause-elimination or new-build claim follows from this audit.
