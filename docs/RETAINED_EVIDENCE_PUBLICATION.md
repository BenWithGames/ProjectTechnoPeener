# Retained replay terminal publication ordering

The prior stop path exposed failed/restart before capture_failure_evidence() and release(). The Steam worker's immediate restart branch could then finalize a report without binary evidence or trace snapshots. Reports 36028 and 26956 show this symptom: failure diagnostics populated but retirement/evidence sections absent.

The new terminal_ready latch publishes only after evidence capture and the cleanup attempt. The restart latch remains immediate for game safety. The Steam worker waits for terminal readiness before reporting a retained replay restart/failure. Successful replay likewise publishes completion after verified/stage are finalized, avoiding an earlier retired-before-verified observation.

Diagnostic capture exceptions publish evidence state 2 and still attempt cleanup. Cleanup failures remain failures, with restart required, but now have a completed terminal publication. Reports expose terminal readiness and evidence state explicitly.

Regression fixtures inspect the production stop path from inside evidence capture and cleanup, asserting that terminal readiness remains false while the restart latch is set. They verify readiness only after both phases complete, plus cleanup failure and evidence capture exception paths. Existing native-window tests pass. No gameplay restoration policy or RNG handling is changed.
