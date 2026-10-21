# Input latency comparison

Both PCs install this build. Start in the first private lobby, without a warmup.
Use **Steam-TestSuite.cmd** for prediction enabled, or **Steam-TestSuite-PredictionDisabled.cmd** for the baseline. Both players must choose the same mode. Restart both games between runs. Play normally through the match and collect both reports with Collect-RollbackReport.cmd. Repeat with the same characters, stage and connection, preferably enabled/disabled then disabled/enabled.

This baseline is the instrumented mod with prediction disabled, not pristine SC6. It preserves native clock waits, requires a tagged confirmed remote native input, and does not create predicted frames. Later mod confirmations must agree with the native input; conflicts still fail. Both modes retain snapshot, verification, resource ownership and round-safety machinery. Different protocol signatures reject mixed modes and older builds. Rearming copies the chosen mode to each new round.

## Early round-one startup

Both comparison modes now remove the old 10-second attachment and 5-second clock-observation waits. Two consecutive fight-ready clock/combat pairs establish the mapping, and both peers agree on a boundary 60 frames after the first fight-ready pair (about one second at 60 FPS). Resource readiness, continuity, identity, missed-target rejection and fight-readiness checks remain. This is not a promise of activation on the very first frame; a peer or resource delay can prevent startup. Native round-start mode keeps advancing normally until those fight-ready pairs exist. Historical non-continuous suites retain their 120-pair/600-frame rehearsal settings.

## Measurements

Ghidra evidence: native input writer RVA 3F2B60 copies the local raw word from clock+3B8/3BC into a 512-entry tagged ring at +3C0, indexed by local_cursor (+4410). Native service RVA 3FDB80 performs input service; the stateful wait predicate is RVA 3E9EE0, and advancement is RVA 3E2000. Existing decompilation/disassembly is under work/ghidra-sc6-focus/prediction-input-publication and native-delivery-audit.

The observer reads four candidate local-ring entries before and after service. A newly valid identity/frame-tagged entry supplies a publication interval bounded by the two QPC timestamps. Entries already present before observation are not assigned a fabricated sampling time. At successful native input-formation completion, the same frame and raw word are matched. Replay uses a separate adapter path and does not contribute samples. This endpoint precedes the combat update; rendering, controller polling, OS queues and display latency are not measured.

Reports contain per-round `input_latency_*` fields, including:

- Lower/upper publication-to-consumption mean/max in microseconds, plus the native-service publication bracket width.
- Native service duration and observed wait-episode wall time. Repeated polls count as one episode, ending when the wait clears or advancement occurs. The final open episode is censored, not counted as zero.
- Native-wait calls, withheld calls, prediction-bypassed calls, reason-2 calls and other reasons. Call counts are not unique frames or saved milliseconds.
- Matched, missing, duplicate, invalid and overwritten sample counts. Zero matched samples means not measured. Old round histories are separate and cannot match new rounds.

The bounded 512-entry observer allocates no memory, writes no files and acquires no locks in the game loop. Atomic aggregate reporting runs through the existing deferred report path. Timing does not authorize advancement or weaken any guard. The first timestamp is **not a physical controller sampling timestamp**: these are native publication-to-consumption bounds. If publication coverage is low, investigate coverage before drawing conclusions.

Run `Compare-InputLatency.ps1 -EnabledReport <enabled ZIP or continuous.txt> -DisabledReport <disabled ZIP or continuous.txt>` for the **same PC**. It combines current and retired rounds with sample weighting and reports the disabled-minus-enabled mean interval. Missing/invalid evidence is shown; absent or invalid measurements cannot establish improvement. Compare Ben's runs with Ben's, and the partner's runs with the partner's. No cross-PC QPC subtraction is performed.

## Fresh-local enabled mode
The FreshLocalInput build assigns post-service local raw input to the next mod simulation frame instead of consuming the delayed native ring word. Both peers must use the new enabled build. Reports mark input_latency_fresh_local=1; disabled mode retains native buffering and marks 0. Publication timing brackets the service which sampled the committed word; sent inputs remain immutable through stalls. See FRESH_LOCAL_INPUT.md. The earlier blend-pointer restoration failure is not fixed by this change.
