# Nonblocking missed-update recovery

This replaces the live 8 ms receipt grace. No receipt event/timer is enabled and
no grace callback is attached in NativeCombatSession. Adaptive delay 0--2,
prediction limit 3, input publication, fast redundancy, snapshot optimizations,
exact endpoint verification and round/ownership guards are unchanged.

Evidence: match 0bde3c8ec11f1c3aa0872c6b3bb7648c has prediction-limit misses
followed by confirmations before the next combat consumption. Some gaps reach
55 ms despite resumed outer CPU work below 7 ms. These logs establish missed
opportunities, not the precise cause of every native scheduler gap.

Ghidra rerun read-only on the pinned SoulcaliburVI.exe with
SC6ClockCombatSequenceAudit.java and SC6PendingCombatAudit.java. Outputs:
work/ghidra-sc6-focus/missed-update-sequence-audit and missed-update-pause-audit.
Machine code at 3FBF30 calls 3FE520 only for outer+1461 == 2. The ordinary
3FE520 path consumes clock+3A0/+3A4 minus outer+1488/+148C. Alternate +1463
can select a different path and is excluded. +12F3 must be clear. 3E9EE0
mutates native pause state; the existing guarded native pause-release path is
retained rather than bypassing it or writing clock counters.

Implementation: a prediction-limit rejection leaves a single retry candidate.
The existing outer-entry wrapper checks the correct clock, owner, sole outer
entry, ordinary combat mode and fully consumed previous delta. It then reopens
only an unchanged, previously serviced-and-withheld clock state. Native service
and input sampling are NOT repeated. Confirmations are drained without sleeping;
the original fight readiness, prediction frontier and reservation checks run
again. Successful admission advances exactly once through the native advance
routine. The original outer function then consumes that delta normally. The
retry never calls combat itself. A failed candidate is consumed once; another
normal clock service supersedes it. Retired/stopped/baseline routes cannot use it.
No catch-up loop, extra input delay or larger prediction horizon is introduced.

If truth has not arrived, state changed, a prior delta is pending, or the round
is no longer eligible, recovery does not advance. This cannot recover packets
that arrive after the outer entry, nor guarantee an outer entry when the native
engine skips it entirely. It targets a safe available opportunity, not proof that
all persistent frame-phase imbalance is solved. Live paired validation is needed.

Diagnostics: existing fields retained. receipt_grace_budget_us/backstop are zero
when the facility is disabled. missed_update_recovery_* counters distinguish
attempts, advances, stale state and still-blocked attempts. Journal kind 12 is
recovery, detail 0 begin / 1 advanced / 2 blocked / 3 stale / 4 failure; aux0
is admission reason, aux1 settled frontier. Outer rejection reasons are 1 wrong
clock, 2 phase, 3 suspended, 4 alternate path, 5 pending/different delta,
6 unreadable boundary. Outer CPU timings include recovery work. Existing advance
events still represent actual simulation progress; recovery_advanced counts a
clock reservation, not proof its later combat update completed.

Regression coverage: late receipts, no receipt, changed native phase/cursor,
round handoff, stop, drain failure, wrong clock, duplicate retry, lost fight
eligibility, native pause release preserving other pause bits, outer mode/delta
rejections and balanced journal events. Native service executes once; no grace
wait executes. Full native build/test suite required before packaging.

Follow-up audit and limitation (same investigation): all six logged reason-5
misses in the local paired report reach an ordinary zero-delta outer entry only
5--8 microseconds later. The known late packets are milliseconds later. This
outer-entry recheck therefore does NOT, by itself, fix those historical gaps.
It is a bounded nonblocking recovery opportunity, with the live grace removed,
not a demonstrated repair of the larger scheduling/phase imbalance. It may be
less effective than grace on that arrival pattern; compare paired live reports.

Further Ghidra tracing followed the wait-owner singleton through 2E60030,
2E594F0 and constructor 2E4FDE0. Its +20 interface vtable is 3D279A8;
+88 resolves to 2E5A430 and toggles root+40 bit40. Consumer 2E6DFE0 resets
an accumulated stall/watchdog interval when this bit is set. This is NOT
sufficient evidence that the bit selects combat updates. No changes to this
flag, the engine's tick scheduler or timeout handling were made. The unrelated
pause flags and native side effects remain intact.

Full Build-Native.ps1 passed, including the new recovery and outer-boundary
regressions and existing outer ABI harness. No live validation yet. The friend
package is an explicitly experimental nonblocking-recovery build; do not report
the overall timing imbalance solved from the local tests.
