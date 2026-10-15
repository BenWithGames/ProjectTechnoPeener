param([string]$OutputDirectory=(Join-Path $env:USERPROFILE 'Downloads'),
      [ValidateSet('PredictionCorrection','PredictionCorrection-Diagnostic','RepeatedCorrection','AttackCorrection','AttackCorrection-Diagnostic','AttackCorrection-IdleCamera','AttackCorrection-CameraTransition','AttackCorrection-TopologyDiagnostic','AttackCorrection-ReplayEndpoint','AttackCorrection-ScalarDiagnostic','AttackCorrection-BlendReadiness','AttackCorrection-NodeIdentity','RepeatedAttackCorrection','CombatCorrection','CombatCorrection-Readiness','CombatCorrection-RejectionCleanup','CombatCorrection-NeutralRetry','CombatCorrection-BlendWeights','CombatCorrection-ClockDuplicates','CombatCorrection-LoadedBlend','CombatCorrection-BlendPointer','CombatCorrection-NodePayload','CombatCorrection-InactiveBlend','CombatCorrection-ResetSelection','CombatCorrection-PayloadInventory','CombatCorrection-DivergenceRetry','CombatCorrection-HoldDiagnostic','CombatCorrection-ImmediateRelease','CombatCorrection-ReplayDifference','CombatCorrection-EvidenceTiming','CombatCorrection-EvidencePublication','CombatCorrection-EvidenceBlendTimers','CombatCorrection-EvidenceTimerRestore','CombatCorrection-EvidenceCameraCounters','CombatCorrection-EvidenceCleanStop','CombatCorrection-EvidenceWindLifecycle','CombatCorrection-EvidenceStageReferences','CombatCorrection-EvidenceCameraValidation','CombatCorrection-EvidenceFreshClock','CombatCorrection-EvidenceTimerCompatibility','CombatCorrection-EvidenceReferenceFirst','CombatCorrection-EvidenceCameraState','CombatCorrection-EvidenceCorrectedFlag','CombatCorrection-EvidenceRngCallers','CombatCorrection-EvidenceRngBridge','CombatCorrection-EvidenceSharedState','CombatCorrection-EvidenceMotionZero','CombatCorrection-EvidenceCameraModes','CombatCorrection-EvidenceTimerCompletion','CombatCorrection-EvidenceCameraOffset')][string]$BuildLabel='PredictionCorrection')
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$name='SC6Rollback-'+$BuildLabel+'-Suite-'+$stamp
$destination=Join-Path $project ('build-network\'+$name)
$payload=Join-Path $destination 'observer-mod'
New-Item -ItemType Directory -Path $payload -Force | Out-Null
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
if($BuildLabel -in @('CombatCorrection-EvidenceReferenceFirst','CombatCorrection-EvidenceCameraState','CombatCorrection-EvidenceCorrectedFlag','CombatCorrection-EvidenceRngCallers','CombatCorrection-EvidenceRngBridge','CombatCorrection-EvidenceSharedState','CombatCorrection-EvidenceMotionZero','CombatCorrection-EvidenceCameraModes','CombatCorrection-EvidenceTimerCompletion','CombatCorrection-EvidenceCameraOffset')){
    $readme=Join-Path $destination 'README.txt'
    $text=[IO.File]::ReadAllText($readme).Replace('One bounded correction with real recorded combat inputs and neutral remote prediction.','Three bilateral corrections, each gated by original-input reference replay.').Replace('captures four combat states, predicts neutral remote buttons,','captures four combat states, verifies original-input replay, then predicts neutral remote buttons,').Replace('The test stops after one verified correction','The test stops after three corrections verified by BOTH PCs')
    [IO.File]::WriteAllText($readme,$text)
    Add-Content $readme 'SIX-STEP METHOD: (1) capture four reference states; (2) restore and replay the three original inputs; (3) retain strict safety guards and compare replay endpoints exactly, deferring only the validated inactive-weight rebuild; (4) predict and correct only after reference replay passes; (5) separately report clean rejection, verified endpoint/cleanup, or unsafe stop requiring restart; (6) require three bilateral corrections, then observe normal gameplay and report round/rematch coverage. Cross-round correction and general post-mutation recovery remain unavailable: observation alone never certifies them. No continuous rollback is enabled. Clean pre-mutation rejections retry; unknown mismatches after mutation stop.'
}
if($BuildLabel -in @('CombatCorrection-EvidenceRngCallers','CombatCorrection-EvidenceRngBridge','CombatCorrection-EvidenceSharedState','CombatCorrection-EvidenceMotionZero','CombatCorrection-EvidenceCameraModes','CombatCorrection-EvidenceTimerCompletion','CombatCorrection-EvidenceCameraOffset')){
    Add-Content (Join-Path $destination 'README.txt') 'RNG CALLER DIAGNOSTICS: failure evidence includes a rolling 8192-call trace for helper 34F550. The analyzer reports callers, frame, thread, seeds and phases (natural/unclassified, load, reference replay, prediction, correction), dropped and overwritten events. Inline RNG writes are not intercepted. Do not interpret missing helper calls as no RNG activity. Existing camera fixes and exact replay checks remain enabled.'
}
if($BuildLabel -in @('CombatCorrection-EvidenceSharedState','CombatCorrection-EvidenceMotionZero','CombatCorrection-EvidenceCameraModes','CombatCorrection-EvidenceTimerCompletion','CombatCorrection-EvidenceCameraOffset')){
    Add-Content (Join-Path $destination 'README.txt') 'SHARED-STATE PREFLIGHT: failure evidence now records the rejected shared-record check, record kind/address/size, first differing byte, saved/next/current/live bytes and source line. The bundled analyzer decodes shared_state_preflight; live_valid distinguishes unread live data from zero values. Existing acceptance rules and timing are unchanged. Both PCs must install this revision.'
}
if($BuildLabel -in @('CombatCorrection-EvidenceMotionZero','CombatCorrection-EvidenceCameraModes','CombatCorrection-EvidenceTimerCompletion','CombatCorrection-EvidenceCameraOffset')){Add-Content (Join-Path $destination 'README.txt') 'EXPERIMENTAL MOTION ZERO: only the observed player motion field at +453FC may compare -0 and +0 as equal during replay, with pinned serializer and source addresses. Nonzero values and other fields remain exact. No game memory is normalized. Raw evidence and motion_zero report fields retain the difference. This is a bounded diagnostic experiment, not proof of subsequent-frame equivalence or continuous rollback.'}
if($BuildLabel -in @('CombatCorrection-EvidenceCameraModes','CombatCorrection-EvidenceTimerCompletion','CombatCorrection-EvidenceCameraOffset')){Add-Content (Join-Path $destination 'README.txt') 'CAMERA MODE RANGE: exact EBV camera +258 accepts saved/next modes 0..14, native loaded zero or unchanged saved value, and restores the saved mode after all guards. Sentinel15 and out-of-range values are rejected. All 225 transitions are locally tested. Camera failure diagnostics and earlier suite evidence remain enabled; live verification is pending.'}
if($BuildLabel -in @('CombatCorrection-EvidenceTimerCompletion','CombatCorrection-EvidenceCameraOffset')){Add-Content (Join-Path $destination 'README.txt') 'TIMER COMPLETION RESTORATION: restores cursor and state for an active 2 to completed 1 transition with positive finite source cursor/rate, exactly zero destination cursor, null callback, and otherwise identical slot bytes. Newer endpoint validation stays strict. Resource pointers and callbacks are never rewritten. Existing diagnostics remain enabled; live verification is pending.'}
if($BuildLabel -eq 'CombatCorrection-EvidenceCameraOffset'){Add-Content (Join-Path $destination 'README.txt') 'CAMERA OFFSET: restores the exact saved finite base-camera +144 smoothing offset after native zero-reset; no guessed numeric range. Camera identity, unrelated bytes and live-state checks remain required. Existing diagnostics and baseline timing remain enabled.'}
$zip=Join-Path $OutputDirectory ((Split-Path $destination -Leaf)+'.zip')
Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $zip
$dllHash=(Get-FileHash -LiteralPath (Join-Path $payload 'main.dll')).Hash
$builtHash=(Get-FileHash -LiteralPath (Join-Path $project 'package\Mods\SC6Rollback\dlls\main.dll')).Hash
if($dllHash -ne $builtHash){throw 'Packaged DLL mismatch'}
[pscustomobject]@{Folder=$destination;Zip=$zip;DllSHA256=$dllHash;ZipSHA256=(Get-FileHash -LiteralPath $zip).Hash}
