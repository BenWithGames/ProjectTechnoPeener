# Startup and round-rearming timing diagnostics

Startup timing schema 2 retains existing aggregate fields and adds session_bind,
first_capture, initial_ring_capture (the next three capture attempts per session),
settlement_wait, route_retire and rearm_provider. Attempts include unsuccessful
returns. The first capture is measured separately for each newly constructed
round session. Steady-state capture does not collect this extra startup sample.

startup_trace_N contains kind, round, thread, begin QPC, end QPC. Use
startup_timing_frequency to convert ticks and align these intervals with the
existing outer-update CSV. These are inclusive elapsed durations, not CPU time;
never add overlapping parent and child measurements. hold_wall is peer-agreement
waiting; settlement_wait can also include sleeps. Construction, preparation and
capture costs are distinct from those waits.

The trace is process-cumulative, append-only and bounded to 256 selected events.
After capacity is reached aggregates continue and startup_trace_omitted counts
unrecorded events. Hook scopes perform no allocation, locks, formatting or file
output. Entries publish with release/acquire ordering. Existing report draining
writes the fields. Round values can repeat across matches; use QPC ordering.

Fresh read-only Ghidra audit: startup-timing-audit, RVAs 3F5D20, 3FBF30,
3FE520 and 3E2000. Clock dispatch conditionally calls advancement, while the
outer combat loop uses clock differences and can perform multiple steps. Thus
clock calls alone must not be interpreted as combat frames or startup cost;
correlate actual capture/activation intervals with outer-update observations.
No native control flow, waits, input delivery, ownership, verification or round
safety rules changed in this instrumentation update. It does not itself promise
less stutter. First-lobby testing needs no warmup.

## Live match-end reporting

The live runner now recognizes the audited HgBattleModeEnd identity (vtable
3E87428, handler 2D2BC0, state accessor 301590) on the owner thread. GameSet
and between-round playback are not terminal. Completion additionally requires
retired routes/history, committed native outer continuation, nonzero updates,
and no restart requirement. Resource cleanup runs on that same owner thread,
even if the results screen no longer produces combat frame callbacks.

Only successful cleanup publishes continuous_match_completed. The transport
then exports captures and writes status=continuous_match_complete; the launcher
collects the ZIP and exits zero. This is local completion, not a claim that the
other PC passed or that every possible rollback case was exercised. Peer stop
packets cannot overwrite an already completed local result. All failure paths
and restart requirements remain. Restart both games before another test.

## Startup pause reduction

Agreement sends now happen immediately when the offer/ack/held packet changes.
Unchanged incomplete agreements retry every 10 ms; completed agreements retain
250 ms retries. No authentication, identity, bilateral hold or timeout checks
are removed. This removes polling delay, not network transit time.

Successor sessions may take the previous restore scratch allocation only after
native continuation commit and history retirement, on the owning thread, in
the same session, without restart/failure. Existing prepare_for_reuse verifies
and clears scratch before transfer. Old diagnostic copies remain intact; old
storage cannot capture or restore once its scratch has moved. Unavailable or
non-reusable scratch falls back to normal allocation. Snapshot pools, native
resources, input histories and round identities are still fresh. This targets
workspace construction during rearming; initial workspace allocation remains.

Fresh read-only Ghidra audit startup-pacing-audit reconfirms native outer loop
and conditional clock advancement. These loops remain untouched: scratch is
transferred only at the existing committed retirement boundary. The measured
first-session allocation cost remains a further optimization opportunity.
