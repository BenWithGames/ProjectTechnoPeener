# Camera decision-counter load sentinel

Reports 58316 (Ben) and 36560 (partner) completed five cycles, then failed
restoration in the sixth window, source frame 1335 / next 1336. Wind and RNG
checks reported no mismatches. Ben: packed target offset 120 (owner+2A8),
26 -> 27, loaded FFFFFFFF. Partner: packed offset 116 (owner+2A4), 1 -> 2,
loaded FFFFFFFF. Both failed the decision_counter_loaded guard.

Focused read-only Ghidra decompilation of the complete chained function
31AC60..31ADA4 confirms that these are integer temporal decision counters.
It reads player flags +1732/+1733, increments the relevant counter or explicitly
stores -1, and applies signed comparisons against six. The native reset stores
are at 31ACF0 (+2A8) and 31AD1D (+2A4; corroborate instruction export).
The previous camera audit already identified this sentinel in normal updates.
The two saved reports establish it as an observed post-load value as well.
This does not prove the full dynamic call chain that produced the post-load
sentinel; no claim is made that a dedicated loader contains that store.

The compatibility guard now allows loaded -1 for an otherwise accepted source
pair, alongside the existing saved/next alternatives. No source-pair acceptance
is broadened: skipped increments, reset transitions and overflow remain rejected.
The existing opt-in, exact camera vtable/owner identity, captured-vs-live checks,
all-field preflight, saved-value writes and exact post-write/endpoint checks stay.
Only the two counters receive this additional loaded-state alternative.

Regression coverage exercises both offsets with 1->2 and 26->27; validation-only
leaves -1 untouched; real restoration writes the saved value; a stale live value
rejects before writes. Unrelated loaded values, skipped increments and overflow
are rejected. AllWind comparison changes remain included.

Ghidra artifact: workspace work/ghidra-sc6-focus/camera-counter-reset/
0031ac60_CameraDecisionCounters.c; script SC6CameraCounterReset.java.
Live match verification remains required.
