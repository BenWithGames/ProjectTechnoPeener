param([string]$OutputDirectory=(Join-Path $env:USERPROFILE 'Downloads'),
      [ValidateSet('PredictionCorrection','PredictionCorrection-Diagnostic','RepeatedCorrection','AttackCorrection','AttackCorrection-Diagnostic','AttackCorrection-IdleCamera','AttackCorrection-CameraTransition','AttackCorrection-TopologyDiagnostic','AttackCorrection-ReplayEndpoint','AttackCorrection-ScalarDiagnostic','AttackCorrection-BlendReadiness','AttackCorrection-NodeIdentity','RepeatedAttackCorrection','CombatCorrection','CombatCorrection-Readiness','CombatCorrection-RejectionCleanup','CombatCorrection-NeutralRetry','CombatCorrection-BlendWeights','CombatCorrection-ClockDuplicates','CombatCorrection-LoadedBlend','CombatCorrection-BlendPointer','CombatCorrection-NodePayload','CombatCorrection-InactiveBlend','CombatCorrection-ResetSelection','CombatCorrection-PayloadInventory','CombatCorrection-DivergenceRetry','CombatCorrection-HoldDiagnostic','CombatCorrection-ImmediateRelease','CombatCorrection-ReplayDifference','CombatCorrection-EvidenceTiming','CombatCorrection-EvidencePublication','CombatCorrection-EvidenceBlendTimers','CombatCorrection-EvidenceTimerRestore','CombatCorrection-EvidenceCameraCounters','CombatCorrection-EvidenceCleanStop','CombatCorrection-EvidenceWindLifecycle','CombatCorrection-EvidenceStageReferences','CombatCorrection-EvidenceCameraValidation','CombatCorrection-EvidenceFreshClock','CombatCorrection-EvidenceTimerCompatibility','CombatCorrection-EvidenceReferenceFirst','CombatCorrection-EvidenceCameraState','CombatCorrection-EvidenceCorrectedFlag','CombatCorrection-EvidenceRngCallers','CombatCorrection-EvidenceRngBridge','CombatCorrection-EvidenceSharedState','CombatCorrection-EvidenceMotionZero','CombatCorrection-EvidenceCameraModes','CombatCorrection-EvidenceTimerCompletion','CombatCorrection-EvidenceCameraOffset','CombatCorrection-EvidenceRoundRetry','TargetedRetention','TargetedRetention-Coverage','RetainedObjectReplay','RetainedObjectReplay-ScalarReset','RetainedObjectReplay-AnimationCopy','RetainedObjectReplay-InlineRng','RetainedObjectReplay-TimerActivation','RetainedObjectReplay-EvidenceOrder','RetainedObjectReplay-RepeatedCycles','RetainedObjectReplay-CycleRetirement','RetainedObjectReplay-WindCaptureDiagnostic','RecordThreeMatches','OwnedRecordingReplay','RepeatedOwnedReplay','MultiRoundReplay','MultiRoundReplay-AdmissionFix','MultiRoundReplay-ZeroCamera','MultiRoundReplay-FightReady','MultiRoundReplay-TimerLookup','MultiRoundReplay-ScopedClear','MultiRoundReplay-MatrixUpdate','MultiRoundReplay-InactiveWeights','MultiRoundReplay-MatchEnd','MultiRoundReplay-EndState','MultiRoundReplay-MotionZero','MultiRoundReplay-SecondaryPaths','MultiRoundReplay-RepeatedCombat','MultiRoundReplay-BufferReuse','MultiRoundReplay-FirstMatch','MultiRoundReplay-GateDiagnostic','MultiRoundReplay-RecordedCopy','MultiRoundReplay-CopyEvidence','MultiRoundReplay-EmitterCountdown','MultiRoundReplay-DeferredEvidence','FullMatchReplay','LivePrediction')][string]$BuildLabel='PredictionCorrection')
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$name='SC6Rollback-'+$BuildLabel+'-Suite-'+$stamp
$destination=Join-Path $project ('build-network\'+$name)
$payload=Join-Path $destination 'observer-mod'
New-Item -ItemType Directory -Path $payload -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Limit-CaptureStorage.ps1') -Destination $destination
Copy-Item -LiteralPath (Join-Path $project 'docs/CAPTURE_STORAGE_POLICY.md') -Destination $destination
foreach($name in @('Steam-TestSuite.cmd','Steam-TestSuite.ps1','Steam-Inputs.ps1','Summarize-TestSuite.ps1','Install-Observer.cmd','Install-Observer.ps1','Collect-RollbackReport.cmd','Collect-RollbackReport.ps1')){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $destination
}
$installer=Join-Path $destination 'Install-Observer.ps1'
$text=[IO.File]::ReadAllText($installer).Replace('Installed and verified observer.','Installed and verified PredictionCorrection suite.').Replace('No recording starts until Observe-Inputs.cmd is explicitly run.','Follow README.txt: warm up, return to the private lobby, then run Steam-TestSuite.cmd.')
$text=$text.Replace('param([string]$GameFolder)',"param([string]`$GameFolder='C:\Program Files (x86)\Steam\steamapps\common\SoulcaliburVI')")
[IO.File]::WriteAllText($installer,$text)
foreach($name in @('dwmapi.dll','UE4SS.dll','UE4SS-settings.ini','UE4SS-LICENSE.txt','MINHOOK-LICENSE.txt','main.dll')){
    $source=if($name -eq 'main.dll'){Join-Path $project 'package\Mods\SC6Rollback\dlls\main.dll'}else{Join-Path $project ('package\'+$name)}
    Copy-Item -LiteralPath $source -Destination (Join-Path $payload $name)
}
(@{profile=($(if($BuildLabel -like 'CombatCorrection*'){'CombatCorrection'}elseif($BuildLabel -eq 'RepeatedAttackCorrection'){'RepeatedAttackCorrection'}elseif($BuildLabel -like 'AttackCorrection*'){'AttackCorrection'}elseif($BuildLabel -eq 'RepeatedCorrection'){'RepeatedCorrection'}else{'PredictionCorrection'}));observeSeconds=60;friendCode='283215760';playerSlot=2;consent=$true} | ConvertTo-Json -Compress) | Set-Content -LiteralPath (Join-Path $destination 'TestSuite.json') -Encoding ascii
@'
SC6Rollback PredictionCorrection suite
One controlled three-frame wrong-movement prediction and network-confirmed correction.
Continuous combat rollback remains disabled. This test uses a consenting private match.

Both PCs:
1. Close SC6. Extract this ZIP and run Install-Observer.cmd.
2. Launch SC6, enter one private warmup match, and return to the lobby without scripts.
3. Partner: run Steam-TestSuite.cmd. Ben's friend code 283215760, P2 and consent
   are preset. Wait at the final Enter prompt. This package is configured for her PC.
   If Codex starts Ben's script, only the partner launches this CMD.
4. Start the scripts together, wait for both to connect, then enter the next private match.
5. Leave BOTH controls untouched until observing_suite_gameplay appears. There are
   two brief native clock holds: initial agreement and a bounded input-confirmation wait.
   The test captures four clean states, simulates three wrong remote movement inputs,
   rewinds that prediction, and checks three corrected frames against the clean reference.
6. After observing_suite_gameplay, play normally for 60 seconds. Leave SC6 open until
   the suite reports completion; stage results are exchanged automatically.
7. Run Collect-RollbackReport.cmd and paste the raw report path printed by the test.
   Send the resulting ZIP in Downloads to Ben. Nothing is uploaded automatically.
   Restart both games before another suite.

SUITE_COMPLETE requires eight selected stages to pass on BOTH PCs, including an
actual player-state change and three verified corrected updates. Transport alone
is insufficient. A no-change prediction is a failed correction test.
If prediction_correction_failed_restart_required appears, or gameplay freezes,
close both games and retain the reports. Do not retry in the same game process.
Safe pre-mutation rejection may continue observation with failed stages.
Text and JSON summaries are saved beside captures/steam-inputs-PID.txt.
Automatic compatibility/ping is included; this script does not enable ranked rollback.
'@ | Set-Content -LiteralPath (Join-Path $destination 'README.txt') -Encoding ascii
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
if($BuildLabel -eq 'RepeatedCorrection'){
    $readme=Join-Path $destination 'README.txt'
    $text=[IO.File]::ReadAllText($readme).Replace('One controlled three-frame wrong-movement prediction and network-confirmed correction.','Three controlled three-frame corrections in the same round, scheduled 180 frames apart.').Replace('SC6Rollback PredictionCorrection suite','SC6Rollback RepeatedCorrection suite').Replace('There are','For EACH cycle there are').Replace('actual player-state change and three verified corrected updates.','actual player-state change and three verified corrected updates in EACH of three cycles.')
    [IO.File]::WriteAllText($readme,$text)
}
if($BuildLabel -like 'AttackCorrection*'){
    $readme=Join-Path $destination 'README.txt'
    $text=[IO.File]::ReadAllText($readme).Replace('SC6Rollback PredictionCorrection suite','SC6Rollback AttackCorrection suite').Replace('One controlled three-frame wrong-movement prediction and network-confirmed correction.','One controlled A-attack prediction and three-frame network-confirmed correction.').Replace('simulates three wrong remote movement inputs','injects remote A for one frame and releases it for the next two frames').Replace('PredictionCorrection','AttackCorrection')
    [IO.File]::WriteAllText($readme,$text)
}
if($BuildLabel -eq 'RepeatedAttackCorrection'){
    $readme=Join-Path $destination 'README.txt'
    $text=[IO.File]::ReadAllText($readme).Replace('SC6Rollback PredictionCorrection suite','SC6Rollback RepeatedAttackCorrection suite').Replace('One controlled three-frame wrong-movement prediction and network-confirmed correction.','Three controlled A-attack corrections in the same round, scheduled 180 frames apart.').Replace('There are','For EACH cycle there are').Replace('simulates three wrong remote movement inputs','injects remote A for one frame and releases it for the next two frames').Replace('actual player-state change and three verified corrected updates.','actual player-state change and three verified corrected updates in EACH of three cycles.')
    [IO.File]::WriteAllText($readme,$text)
}
if($BuildLabel -like 'CombatCorrection*'){
    $readme=Join-Path $destination 'README.txt'
    $text=[IO.File]::ReadAllText($readme).Replace('SC6Rollback PredictionCorrection suite','SC6Rollback CombatCorrection suite').Replace('One controlled three-frame wrong-movement prediction and network-confirmed correction.','One bounded correction with real recorded combat inputs and neutral remote prediction. SC6 network waits are unchanged.').Replace('Leave BOTH controls untouched until observing_suite_gameplay appears.','BOTH PLAY NORMALLY from FIGHT, using movement and attacks; do not wait idle.').Replace('captures four clean states, simulates three wrong remote movement inputs,','captures four combat states, predicts neutral remote buttons,').Replace('After observing_suite_gameplay, play normally for 60 seconds.','Continue playing through the 60-second observation.').Replace('A no-change prediction is a failed correction test.','A fully verified and retired no-change prediction is inconclusive and retried automatically; it is not a correction PASS. Neutral windows are skipped before restoration; up to 12 attempts spaced 180 frames apart. Both peers confirm cleanup before each retry. The test stops after one verified correction or reports no suitable window observed.')
    [IO.File]::WriteAllText($readme,$text)
}
if($BuildLabel -like 'CombatCorrection-Evidence*'){
    $configPath=Join-Path $destination 'TestSuite.json'
    $plan=Get-Content $configPath -Raw | ConvertFrom-Json
    $plan.profile='CombatCorrectionBaseline'
    $plan | ConvertTo-Json -Compress | Set-Content $configPath -Encoding ascii
    $plan.profile='CombatCorrection'
    $plan | ConvertTo-Json -Compress | Set-Content (Join-Path $destination 'TestSuite-Immediate.json') -Encoding ascii
    @('@echo off','powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Steam-TestSuite.ps1" -Config "%~dp0TestSuite-Immediate.json"','pause') | Set-Content (Join-Path $destination 'Steam-TestSuite-Immediate.cmd') -Encoding ascii
    Copy-Item (Join-Path $project 'tools/analyze_replay_evidence.py') $destination
    Add-Content (Join-Path $destination 'README.txt') 'TIMING COMPARISON: Steam-TestSuite.cmd = 100ms baseline; Steam-TestSuite-Immediate.cmd = zero minimum. Both PCs must choose the SAME mode. Use baseline first, restart before changing modes, keep characters/stage/actions comparable. Both modes use identical restoration and retries. One different outcome is not proof of causation. Failure ZIP includes raw binary evidence; collect after scripts finish. Analyzer counts raw differences including padding, not semantic failures.'
}
if($BuildLabel -in @('CombatCorrection-EvidenceWindLifecycle','CombatCorrection-EvidenceStageReferences'),'CombatCorrection-EvidenceStageReferences'){
    Add-Content (Join-Path $destination 'README.txt') 'WIND LIFECYCLE: both PCs must install this revision. The owned snapshots now retain and restore supported wind objects and validate their external stage references. Play normally using the existing warmup/lobby workflow. Unknown or changed dependencies still stop restoration. Wind lifecycle is only proven when the summary records an actual list change, verified replay and completed retirement; transport PASS alone is insufficient. Collect-RollbackReport.cmd includes wind lifecycle evidence with the existing diagnostics.'
}
if($BuildLabel -in @('CombatCorrection-EvidenceReferenceFirst','CombatCorrection-EvidenceCameraState','CombatCorrection-EvidenceCorrectedFlag','CombatCorrection-EvidenceRngCallers','CombatCorrection-EvidenceRngBridge','CombatCorrection-EvidenceSharedState','CombatCorrection-EvidenceMotionZero','CombatCorrection-EvidenceCameraModes','CombatCorrection-EvidenceTimerCompletion','CombatCorrection-EvidenceCameraOffset','CombatCorrection-EvidenceRoundRetry')){
    $readme=Join-Path $destination 'README.txt'
    $text=[IO.File]::ReadAllText($readme).Replace('One bounded correction with real recorded combat inputs and neutral remote prediction.','Three bilateral corrections, each gated by original-input reference replay.').Replace('captures four combat states, predicts neutral remote buttons,','captures four combat states, verifies original-input replay, then predicts neutral remote buttons,').Replace('The test stops after one verified correction','The test stops after three corrections verified by BOTH PCs')
    [IO.File]::WriteAllText($readme,$text)
    Add-Content $readme 'SIX-STEP METHOD: (1) capture four reference states; (2) restore and replay the three original inputs; (3) retain strict safety guards and compare replay endpoints exactly, deferring only the validated inactive-weight rebuild; (4) predict and correct only after reference replay passes; (5) separately report clean rejection, verified endpoint/cleanup, or unsafe stop requiring restart; (6) require three bilateral corrections, then observe normal gameplay and report round/rematch coverage. Cross-round correction and general post-mutation recovery remain unavailable: observation alone never certifies them. No continuous rollback is enabled. Clean pre-mutation rejections retry; unknown mismatches after mutation stop.'
}
if($BuildLabel -in @('CombatCorrection-EvidenceRngCallers','CombatCorrection-EvidenceRngBridge','CombatCorrection-EvidenceSharedState','CombatCorrection-EvidenceMotionZero','CombatCorrection-EvidenceCameraModes','CombatCorrection-EvidenceTimerCompletion','CombatCorrection-EvidenceCameraOffset','CombatCorrection-EvidenceRoundRetry')){
    Add-Content (Join-Path $destination 'README.txt') 'RNG CALLER DIAGNOSTICS: failure evidence includes a rolling 8192-call trace for helper 34F550. The analyzer reports callers, frame, thread, seeds and phases (natural/unclassified, load, reference replay, prediction, correction), dropped and overwritten events. Inline RNG writes are not intercepted. Do not interpret missing helper calls as no RNG activity. Existing camera fixes and exact replay checks remain enabled.'
}
if($BuildLabel -in @('CombatCorrection-EvidenceSharedState','CombatCorrection-EvidenceMotionZero','CombatCorrection-EvidenceCameraModes','CombatCorrection-EvidenceTimerCompletion','CombatCorrection-EvidenceCameraOffset','CombatCorrection-EvidenceRoundRetry')){
    Add-Content (Join-Path $destination 'README.txt') 'SHARED-STATE PREFLIGHT: failure evidence now records the rejected shared-record check, record kind/address/size, first differing byte, saved/next/current/live bytes and source line. The bundled analyzer decodes shared_state_preflight; live_valid distinguishes unread live data from zero values. Existing acceptance rules and timing are unchanged. Both PCs must install this revision.'
}
if($BuildLabel -in @('CombatCorrection-EvidenceMotionZero','CombatCorrection-EvidenceCameraModes','CombatCorrection-EvidenceTimerCompletion','CombatCorrection-EvidenceCameraOffset','CombatCorrection-EvidenceRoundRetry')){Add-Content (Join-Path $destination 'README.txt') 'EXPERIMENTAL MOTION ZERO: only the observed player motion field at +453FC may compare -0 and +0 as equal during replay, with pinned serializer and source addresses. Nonzero values and other fields remain exact. No game memory is normalized. Raw evidence and motion_zero report fields retain the difference. This is a bounded diagnostic experiment, not proof of subsequent-frame equivalence or continuous rollback.'}
if($BuildLabel -in @('CombatCorrection-EvidenceCameraModes','CombatCorrection-EvidenceTimerCompletion','CombatCorrection-EvidenceCameraOffset','CombatCorrection-EvidenceRoundRetry')){Add-Content (Join-Path $destination 'README.txt') 'CAMERA MODE RANGE: exact EBV camera +258 accepts saved/next modes 0..14, native loaded zero or unchanged saved value, and restores the saved mode after all guards. Sentinel15 and out-of-range values are rejected. All 225 transitions are locally tested. Camera failure diagnostics and earlier suite evidence remain enabled; live verification is pending.'}
if($BuildLabel -in @('CombatCorrection-EvidenceTimerCompletion','CombatCorrection-EvidenceCameraOffset','CombatCorrection-EvidenceRoundRetry')){Add-Content (Join-Path $destination 'README.txt') 'TIMER COMPLETION RESTORATION: restores cursor and state for an active 2 to completed 1 transition with positive finite source cursor/rate, exactly zero destination cursor, null callback, and otherwise identical slot bytes. Newer endpoint validation stays strict. Resource pointers and callbacks are never rewritten. Existing diagnostics remain enabled; live verification is pending.'}
if($BuildLabel -in @('CombatCorrection-EvidenceCameraOffset','CombatCorrection-EvidenceRoundRetry')){Add-Content (Join-Path $destination 'README.txt') 'CAMERA OFFSET: restores the exact saved finite base-camera +144 smoothing offset after native zero-reset; no guessed numeric range. Camera identity, unrelated bytes and live-state checks remain required. Existing diagnostics and baseline timing remain enabled.'}
if($BuildLabel -eq 'CombatCorrection-EvidenceRoundRetry'){
    $readme=Join-Path $destination 'README.txt'
    $retryText=[IO.File]::ReadAllText($readme).Replace('up to 12 attempts spaced 180 frames apart.','up to 12 attempts using fresh bilateral future-frame agreement, including after clean unstarted round-boundary cancellation.')
    [IO.File]::WriteAllText($readme,$retryText)
    Add-Content (Join-Path $destination 'README.txt') 'FRESH RETRY BOUNDARIES: inconclusive/neutral and safely retired pre-mutation windows retry with bilateral future-frame negotiation using fresh owner-thread clock/battle pairs. An unstarted round-boundary cancellation waits for cleanup and then renegotiates. No-divergence is never a correction PASS. Three bilateral changed corrections remain the goal; the bounded 12-attempt/time budget can end inconclusive. Unsafe restoration, failed ownership cleanup, and protocol failures still stop. Existing camera fixes, diagnostics and 100ms baseline remain.'}
if($BuildLabel -like 'TargetedRetention*'){
    [IO.File]::WriteAllText($installer,[IO.File]::ReadAllText($installer).Replace('PredictionCorrection suite','TargetedRetention suite'))
    @{profile='TargetedRetention';observeSeconds=30;friendCode='283215760';playerSlot=2;consent=$true} | ConvertTo-Json | Set-Content (Join-Path $destination 'TestSuite.json') -Encoding ascii
    @'
SC6Rollback Targeted Object Retention Suite
Both PCs must install this build with SC6 closed, using Install-Observer.cmd.
Partner defaults: C:\Program Files (x86)\Steam\steamapps\common\SoulcaliburVI, Ben's friend code, P2 and consent.
1. Launch SC6, enter a private warmup match, then return to the lobby without scripts.
2. Partner runs Steam-TestSuite.cmd; Ben can ask Codex to start his script. Start together.
3. Wait for connection, enter the next private match and PLAY NORMALLY from FIGHT.
4. Capture runs up to 300 four-snapshot windows, holding supported object references for
   three frames each, starting the next window on the following frame after releasing all previous snapshots. A round change ends capture safely.
5. Keep both games open through the 30-second observation and bilateral summary exchange.
6. Send the ZIP produced by Collect-RollbackReport.cmd. Restart before another suite.
This test retains only the already-supported wind/effect object types and their validated
snapshot dependencies. It never injects inputs, restores snapshots or replays combat.
PASS requires natural removal of a snapshot-pinned object and completed allocator cleanup
on BOTH PCs. No such event is NOT OBSERVED, never a PASS. Revival is not tested here.
Unknown dependencies and cleanup failures still stop the test. Brief capture stutters are possible.
'@ | Set-Content (Join-Path $destination 'README.txt') -Encoding ascii
}
if($BuildLabel -like 'RetainedObjectReplay*'){
    [IO.File]::WriteAllText($installer,[IO.File]::ReadAllText($installer).Replace('PredictionCorrection suite','RetainedObjectReplay suite'))
    @{profile='RetainedObjectReplay';observeSeconds=30;friendCode='283215760';playerSlot=2;consent=$true} | ConvertTo-Json | Set-Content (Join-Path $destination 'TestSuite.json') -Encoding ascii
    $instructions=@(
        'SC6Rollback Retained Object Replay Suite',
        'Both PCs: close SC6, extract this ZIP, run Install-Observer.cmd.',
        'Partner defaults: her Steam installation folder, Ben friend code, P2 and consent.',
        'Launch SC6, enter a private warmup match, then return to the lobby without scripts.',
        'Partner opens Steam-TestSuite.cmd. Start both scripts together, wait for connection, then enter the match.',
        'PLAY NORMALLY from FIGHT. Wall breaks are allowed; note the stage and any glitches.',
        'Three bounded restore/replay cycles are required. EACH cycle inspects up to 32 four-frame COMBAT snapshot windows for natural removal of a retained effect object.',
        'Windows without removal release all snapshots and retry. Round changes terminate and clean up; partial completion is not PASS.',
        'On removal: restore the retained object and native state, replay three ORIGINAL recorded-input frames, and compare the reference state and effect lists/payloads.',
        'There is no wrong-input prediction. This is bounded reference replay, not continuous rollback.',
        'PASS requires all THREE cycles to observe removal, revive the object, verify three replayed frames and clean up on BOTH PCs.',
        'No removal means NOT OBSERVED. Any restoration/replay failure stops; close both games if restart is required or gameplay freezes.',
        'Continue playing through the 30-second observation and leave both games open until scripts finish.',
        'Run Collect-RollbackReport.cmd and send the ZIP in Downloads. Restart before another suite.',
        'Full combat capture/replay may stutter. Existing restoration guards and mismatch diagnostics remain enabled.',
        'Timer activation restoration supports the validated category-zero inactive-to-active transition; all prior guards remain.',
        'Inline RNG diagnostics and pre-replay seed checks are included. Send the collected report ZIP.',
        'Animation copy diagnostics record the entry gate and before/after matrix-30 copies with native/replay phase. Collect-RollbackReport includes the binary trace automatically.'
    )
    $instructions | Set-Content (Join-Path $destination 'README.txt') -Encoding ascii
}
if($BuildLabel -eq 'RecordThreeMatches'){
    [IO.File]::WriteAllText($installer,[IO.File]::ReadAllText($installer).Replace('PredictionCorrection suite','Three Match recorder').Replace('Follow README.txt: warm up, return to the private lobby, then run Steam-TestSuite.cmd.','Follow README.txt: run Record-ThreeMatches.cmd in a fresh private lobby; no warmup.'))
    foreach($name in @('Steam-TestSuite.cmd','Steam-TestSuite.ps1','Steam-Inputs.ps1','TestSuite.json')){Remove-Item -LiteralPath (Join-Path $destination $name)}
    Copy-Item (Join-Path $PSScriptRoot 'Record-ThreeMatches.ps1'),(Join-Path $PSScriptRoot 'Record-ThreeMatches.cmd') -Destination $destination
    @('Record three normal private matches. Both PCs install with SC6 closed, then launch SC6 fresh.', 'Run Record-ThreeMatches.cmd, NOT Steam-TestSuite.cmd. Partner defaults to P2; Ben runs the PowerShell script with -PlayerSlot 1.', 'No warmup needed. Arm together in the lobby, play a full match, and press Enter at results to save. Repeat for three matches.', 'Snapshots are diagnostic data with session-specific pointers, not portable playable replays. No restoration, replay, injection or object retention is enabled.', 'Each PC creates SC6-ThreeMatches-DATE.zip in Downloads. Send both ZIPs.') | Set-Content (Join-Path $destination 'README.txt')
}
if($BuildLabel -eq 'OwnedRecordingReplay'){
    [IO.File]::WriteAllText($installer,[IO.File]::ReadAllText($installer).Replace('C:\Program Files (x86)\Steam\steamapps\common\SoulcaliburVI','D:\SteamLibrary\steamapps\common\SoulcaliburVI').Replace('PredictionCorrection suite','OwnedRecordingReplay suite'))
    @{profile='OwnedRecordingReplay';observeSeconds=30;friendCode='283215760';playerSlot=2;consent=$true} | ConvertTo-Json | Set-Content (Join-Path $destination 'TestSuite.json') -Encoding ascii
    @('SC6Rollback Owned Recording and Same-Session Replay',
      'Close SC6 on both PCs. Extract ZIP and run Install-Observer.cmd.',
      'Launch SC6. Play a private warmup match, then return to the lobby.',
      'Run Steam-TestSuite.cmd on both PCs; wait for connection before starting the next private match.',
      'Play normally from FIGHT. No idle period, wall break, or special move required.',
      'One four-snapshot window retains supported live dependencies, restores three frames, replays the original inputs and verifies the reference state.',
      'No deliberately wrong input or continuous rollback. A round change cancels safely, not PASS.',
      'Keep both games open through the 30-second observation and report exchange. If frozen or restart required, close both games.',
      'Run Collect-RollbackReport.cmd and send the ZIP. Four owned-snapshot evidence files and existing diagnostics are included.',
      'Archived snapshots remain diagnostic; only retained live objects are used for same-session replay. Brief capture/replay stutter is possible.') | Set-Content (Join-Path $destination 'README.txt') -Encoding ascii
}
if($BuildLabel -eq 'RepeatedOwnedReplay'){
    [IO.File]::WriteAllText($installer,[IO.File]::ReadAllText($installer).Replace('C:\Program Files (x86)\Steam\steamapps\common\SoulcaliburVI','D:\SteamLibrary\steamapps\common\SoulcaliburVI').Replace('PredictionCorrection suite','RepeatedOwnedReplay suite'))
    @{profile='RepeatedOwnedReplay';observeSeconds=30;friendCode='283215760';playerSlot=2;consent=$true} | ConvertTo-Json | Set-Content (Join-Path $destination 'TestSuite.json') -Encoding ascii
    @('SC6Rollback Repeated Same-Session Replay',
      'Both PCs install with SC6 closed, using Install-Observer.cmd.',
      'Launch SC6, play a private warmup match, return to the lobby without scripts.',
      'Start Steam-TestSuite.cmd on both PCs. Wait for connection, then enter the next match.',
      'PLAY NORMALLY. Three bounded three-frame original-input replays, spaced 120 simulation frames after the previous window.',
      'Each cycle retains four snapshots, restores, replays, verifies and retires dependencies before starting the next.',
      'Capture waits for verified native Normal combat mode on every boundary; preparation or readiness loss discards partial windows. Reports include mode and gate timing.',
      'No idle period, wrong-input injection, special move or wall break is required. Continuous rollback remains disabled.',
      'A round change cancels remaining cycles safely. Partial completion is not PASS. Unsafe failures stop and require restart.',
      'Each cycle keeps its own snapshot and replay evidence, animation and RNG diagnostics. Existing guards are unchanged.',
      'Keep both games open through the 30-second observation and result exchange; Collect-RollbackReport.cmd creates a ZIP in Downloads.',
      'Close both games if frozen or restart is requested. Restart before another suite.') | Set-Content (Join-Path $destination 'README.txt') -Encoding ascii
}
if($BuildLabel -in @('MultiRoundReplay','MultiRoundReplay-AdmissionFix','MultiRoundReplay-ZeroCamera','MultiRoundReplay-FightReady','MultiRoundReplay-TimerLookup','MultiRoundReplay-ScopedClear','MultiRoundReplay-MatrixUpdate','MultiRoundReplay-InactiveWeights','MultiRoundReplay-MatchEnd','MultiRoundReplay-EndState','MultiRoundReplay-MotionZero','MultiRoundReplay-SecondaryPaths','MultiRoundReplay-RepeatedCombat','MultiRoundReplay-BufferReuse','MultiRoundReplay-FirstMatch','MultiRoundReplay-GateDiagnostic','MultiRoundReplay-RecordedCopy','MultiRoundReplay-CopyEvidence','MultiRoundReplay-EmitterCountdown','MultiRoundReplay-DeferredEvidence','FullMatchReplay')){
    [IO.File]::WriteAllText($installer,[IO.File]::ReadAllText($installer).Replace('C:\Program Files (x86)\Steam\steamapps\common\SoulcaliburVI','D:\SteamLibrary\steamapps\common\SoulcaliburVI').Replace('PredictionCorrection suite','MultiRoundReplay suite'))
    @{profile='MultiRoundReplay';observeSeconds=30;friendCode='283215760';playerSlot=2;consent=$true} | ConvertTo-Json | Set-Content (Join-Path $destination 'TestSuite.json') -Encoding ascii
    @('SC6Rollback Multi-Round Same-Session Replay',
      'Both PCs install with SC6 closed, using Install-Observer.cmd.',
      'Launch SC6, play a private warmup match, return to the lobby without scripts.',
      'Start Steam-TestSuite.cmd on both PCs. Wait for connection, then enter the next match.',
      'PLAY NORMALLY. Three bounded three-frame original-input replay cycles, spaced at least 120 game frames apart within a round. Play normally until the script finishes.',
      'Each cycle retains four snapshots, restores, replays, verifies and retires dependencies before starting the next.',
      'Capture waits for verified native Normal combat mode on every boundary; preparation or readiness loss discards partial windows. Reports include mode and gate timing.',
      'No idle period, wrong-input injection, special move or wall break is required. Continuous rollback remains disabled.',
      'At a valid next-round boundary, discard partial windows and require fight readiness again; never replay across rounds. Rematches and resource invalidation still cancel. Partial completion is not PASS. Unsafe failures stop and require restart.',
      'Each cycle keeps its own snapshot and replay evidence, animation and RNG diagnostics. Existing guards are unchanged.',
      'Keep both games open through the 30-second observation and result exchange; Collect-RollbackReport.cmd creates a ZIP in Downloads.',
      'Close both games if frozen or restart is requested. Restart before another suite.') | Set-Content (Join-Path $destination 'README.txt') -Encoding ascii
}
if($BuildLabel -in @('MultiRoundReplay-TimerLookup','MultiRoundReplay-ScopedClear','MultiRoundReplay-MatrixUpdate','MultiRoundReplay-InactiveWeights','MultiRoundReplay-MatchEnd','MultiRoundReplay-EndState','MultiRoundReplay-MotionZero','MultiRoundReplay-SecondaryPaths','MultiRoundReplay-RepeatedCombat','MultiRoundReplay-BufferReuse','MultiRoundReplay-FirstMatch','MultiRoundReplay-GateDiagnostic','MultiRoundReplay-RecordedCopy','MultiRoundReplay-CopyEvidence','MultiRoundReplay-EmitterCountdown','MultiRoundReplay-DeferredEvidence','FullMatchReplay')){
    Copy-Item -LiteralPath (Join-Path $project 'docs/TIMER_LOOKUP_DIAGNOSTICS.txt') -Destination $destination
    Add-Content -LiteralPath (Join-Path $destination 'README.txt') -Value 'Includes read-only timer activation, lookup, controller and replay-boundary diagnostics. Collect-RollbackReport.cmd includes the new timer-lookup.bin automatically. Restoration and round-safety checks are unchanged.' -Encoding ascii
}
if($BuildLabel -in @('MultiRoundReplay-ScopedClear','MultiRoundReplay-MatrixUpdate','MultiRoundReplay-InactiveWeights','MultiRoundReplay-MatchEnd','MultiRoundReplay-EndState','MultiRoundReplay-MotionZero','MultiRoundReplay-SecondaryPaths','MultiRoundReplay-RepeatedCombat','MultiRoundReplay-BufferReuse','MultiRoundReplay-FirstMatch','MultiRoundReplay-GateDiagnostic','MultiRoundReplay-RecordedCopy','MultiRoundReplay-CopyEvidence','MultiRoundReplay-EmitterCountdown','MultiRoundReplay-DeferredEvidence','FullMatchReplay')){
    Copy-Item (Join-Path $project 'docs/SCOPED_RESOURCE_CLEAR.txt') $destination
    Add-Content (Join-Path $destination 'README.txt') 'Scoped clear handling ignores only validated unrelated containers. Relevant, unknown, unreadable, foreign-thread and unsupported clears still cancel. Existing round and replay guards remain enabled.'
}
if($BuildLabel -in @('MultiRoundReplay-MatrixUpdate','MultiRoundReplay-InactiveWeights','MultiRoundReplay-MatchEnd','MultiRoundReplay-EndState','MultiRoundReplay-MotionZero','MultiRoundReplay-SecondaryPaths','MultiRoundReplay-RepeatedCombat','MultiRoundReplay-BufferReuse','MultiRoundReplay-FirstMatch','MultiRoundReplay-GateDiagnostic','MultiRoundReplay-RecordedCopy','MultiRoundReplay-CopyEvidence','MultiRoundReplay-EmitterCountdown','MultiRoundReplay-DeferredEvidence','FullMatchReplay')){
    Copy-Item (Join-Path $project 'docs/MATRIX_UPDATE_DIAGNOSTICS.txt') $destination
    Add-Content (Join-Path $destination 'README.txt') 'Includes earlier matrix-update entry, branch and post-copy diagnostics in animation-copy schema2. Existing safety checks remain enabled.'
}
if($BuildLabel -in @('MultiRoundReplay-RepeatedCombat','MultiRoundReplay-BufferReuse','MultiRoundReplay-FirstMatch','MultiRoundReplay-GateDiagnostic','MultiRoundReplay-RecordedCopy','MultiRoundReplay-CopyEvidence','MultiRoundReplay-EmitterCountdown','MultiRoundReplay-DeferredEvidence','FullMatchReplay')){
    Add-Content -LiteralPath (Join-Path $destination 'README.txt') 'Includes per-cycle timing for capture, archive copies, restoration, replay/verification, evidence, traces, retirement, round cleanup and callback gaps; session reporting/I/O is measured separately. Summary shows counts and total/max milliseconds. These are wall times, not display-frame timings. All prior diagnostics and round guards remain enabled.'
}
if($BuildLabel -in @('MultiRoundReplay-FirstMatch','MultiRoundReplay-GateDiagnostic','MultiRoundReplay-RecordedCopy','MultiRoundReplay-CopyEvidence','MultiRoundReplay-EmitterCountdown','MultiRoundReplay-DeferredEvidence','FullMatchReplay')){
    [IO.File]::WriteAllText($installer,[IO.File]::ReadAllText($installer).Replace('Follow README.txt: warm up, return to the private lobby, then run Steam-TestSuite.cmd.','No warmup: join the first private lobby and start Steam-TestSuite.cmd before entering the match.'))
    $readme=Join-Path $destination 'README.txt'
    [IO.File]::WriteAllText($readme,[IO.File]::ReadAllText($readme).Replace('Launch SC6, play a private warmup match, return to the lobby without scripts.','Launch SC6 and join the first private lobby. NO WARMUP MATCH. Start both scripts before entering the match.'))
    Add-Content $readme 'First-match ownership must be confirmed by a valid game-thread boundary before resources become ready. All round and fight-readiness checks remain active. Both PCs require this build.'
}
if($BuildLabel -eq 'FullMatchReplay'){
    @{profile='FullMatchReplay';observeSeconds=0;friendCode='283215760';playerSlot=2;consent=$true} | ConvertTo-Json | Set-Content (Join-Path $destination 'TestSuite.json') -Encoding ascii
    [IO.File]::WriteAllText($installer,[IO.File]::ReadAllText($installer).Replace('D:\SteamLibrary\steamapps\common\SoulcaliburVI','C:\Program Files (x86)\Steam\steamapps\common\SoulcaliburVI').Replace('MultiRoundReplay suite','FullMatchReplay suite'))
    Copy-Item (Join-Path $project 'docs/FULL_MATCH_REPLAY.txt') $destination
    Copy-Item (Join-Path $project 'docs/FULL_MATCH_REPLAY.txt') (Join-Path $destination 'README.txt') -Force
}
if($BuildLabel -eq 'LivePrediction'){
    @{profile='LivePrediction';observeSeconds=0;friendCode='283215760';playerSlot=2;consent=$true} | ConvertTo-Json | Set-Content (Join-Path $destination 'TestSuite.json') -Encoding ascii
    Copy-Item (Join-Path $project 'docs/LIVE_PREDICTION.txt') (Join-Path $destination 'README.txt') -Force
    [IO.File]::WriteAllText($installer,[IO.File]::ReadAllText($installer).Replace('Installed and verified PredictionCorrection suite.','Installed and verified LivePrediction suite.').Replace('Follow README.txt: warm up, return to the private lobby, then run Steam-TestSuite.cmd.','No warmup: join the first private lobby and run Steam-TestSuite.cmd.'))
}
$zip=Join-Path $OutputDirectory ((Split-Path $destination -Leaf)+'.zip')
Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $zip
$dllHash=(Get-FileHash -LiteralPath (Join-Path $payload 'main.dll')).Hash
$builtHash=(Get-FileHash -LiteralPath (Join-Path $project 'package\Mods\SC6Rollback\dlls\main.dll')).Hash
if($dllHash -ne $builtHash){throw 'Packaged DLL mismatch'}
[pscustomobject]@{Folder=$destination;Zip=$zip;DllSHA256=$dllHash;ZipSHA256=(Get-FileHash -LiteralPath $zip).Hash}

