param([switch]$CheckpointCapture,[switch]$NativeReplay,[switch]$LateInputs,[switch]$WindowReuse,[switch]$StartupPreparation,[switch]$Continuous,
      [switch]$TestSuite,[ValidateSet('Boundary','Hold','Snapshots','ReferenceReplay','PredictionCorrection','RepeatedCorrection','AttackCorrection','RepeatedAttackCorrection','CombatCorrection','CombatCorrectionBaseline','TargetedRetention','RetainedObjectReplay','OwnedRecordingReplay','RepeatedOwnedReplay','MultiRoundReplay','FullMatchReplay')][string]$SuiteProfile='Snapshots',
      [ValidateRange(0,180)][int]$ObserveSeconds=60,
      [string]$FriendCode,[ValidateRange(0,2)][int]$PlayerSlot=0,[switch]$Consent)
$ErrorActionPreference='Stop'
function Read-ReportSnapshot([string]$Path) {
    # A writer may briefly deny reads. Let the caller retry on its next poll.
    try {
        $file=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
        try {
            $reader=New-Object IO.StreamReader($file)
            try { $snapshot=$reader.ReadToEnd() } finally { $reader.Dispose() }
        } finally { $file.Dispose() }
        # Reports are rewritten in place. Do not accept a truncated terminal report.
        if($snapshot -notmatch '(?m)^shadow_within_observed_bounds=[01]\r?$'){return $null}
        return $snapshot
    } catch {
        $cause=$_.Exception
        while($cause.InnerException){$cause=$cause.InnerException}
        if($cause -is [IO.IOException] -and (($cause.HResult -band 0xffff) -in @(2,32,33))){return $null}
        throw
    }
}

try {
    if($Continuous -and ($TestSuite -or $StartupPreparation -or $CheckpointCapture -or $NativeReplay -or $LateInputs -or $WindowReuse)){throw 'Continuous mode cannot be combined with another test.'}
    if($Continuous){$StartupPreparation=$true}
    if($TestSuite){$StartupPreparation=$true}
    if($StartupPreparation -and ($CheckpointCapture -or $NativeReplay -or $LateInputs -or $WindowReuse)){throw 'Startup preparation cannot be combined with other test modes.'}
    $gameProcess=@(Get-Process SoulcaliburVI -ErrorAction Stop)
    if($gameProcess.Count -ne 1){throw 'Start one SC6 instance through Steam first.'}
    $gameProcess=$gameProcess[0]
    $gameBin=Split-Path $gameProcess.Path -Parent
    if((Get-FileHash -LiteralPath $gameProcess.Path).Hash -ne 'F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553'){throw 'Unsupported game build.'}
    $captureDir=Join-Path $gameBin 'Mods\SC6Rollback\captures'
    $report=Join-Path $captureDir ('steam-inputs-'+$gameProcess.Id+'.txt')
    if(Test-Path -LiteralPath $report){throw 'This game launch already ran an input exchange. Restart SC6 on both PCs before retrying.'}
    if($TestSuite -or $Continuous){& (Join-Path $PSScriptRoot 'Limit-CaptureStorage.ps1') -CaptureDirectory $captureDir}
    Write-Host 'Both players: install this build, launch SC6, and join the same private lobby.'
    Write-Host 'Both PCs need this build and opposite assigned player slots.'
    if($TestSuite -and $SuiteProfile -notin @('CombatCorrection','CombatCorrectionBaseline') -and $SuiteProfile -in @('ReferenceReplay','PredictionCorrection','RepeatedCorrection','AttackCorrection','RepeatedAttackCorrection','CombatCorrection','CombatCorrectionBaseline')){Write-Host 'CONTROLLED REPLAY: leave both players idle until the script says observing_suite_gameplay, then play normally. This explicitly enables a guarded three-frame restore/replay. Restart immediately if the report requires it.'}
    if($TestSuite -and $SuiteProfile -eq 'RetainedObjectReplay'){Write-Host 'RETAINED OBJECT REPLAY: play normally. Three bounded cycles, each with up to 32 four-frame combat windows; windows without a removed object are discarded. On an observed removal, restore and replay three ORIGINAL input frames, then verify the reference state. No wrong-input prediction. Stop after three verified cycles, a round change, or any failure; restart if requested.'}
    if($TestSuite -and $SuiteProfile -eq 'TargetedRetention'){Write-Host 'TARGETED RETENTION: play normally from FIGHT. Up to 300 four-snapshot windows retain supported effect objects for three frames, starting the next window on the following frame after releasing all previous snapshots. No input injection, restoration or replay. A round change ends capture and releases snapshots. No natural retained-object removal means not observed, not PASS.'}
    if($TestSuite -and $SuiteProfile -eq 'OwnedRecordingReplay'){Write-Host "TEST SUITE: OwnedRecordingReplay; one retained four-snapshot window and three original-input replay frames, followed by $ObserveSeconds seconds of observation. Play normally; restart after the run."}
    elseif($TestSuite -and $SuiteProfile -eq 'RepeatedOwnedReplay'){Write-Host "TEST SUITE: RepeatedOwnedReplay; three spaced original-input replay cycles followed by $ObserveSeconds seconds of observation. Play normally; restart after the run."}
    elseif($TestSuite -and $SuiteProfile -eq 'FullMatchReplay'){Write-Host 'TEST SUITE: FullMatchReplay; verified three-frame original-input windows throughout one match, at least 120 frames apart. Completes only at native match end.'}
    elseif($TestSuite -and $SuiteProfile -eq 'MultiRoundReplay'){Write-Host "TEST SUITE: MultiRoundReplay; three spaced original-input replay cycles during combat, then $ObserveSeconds seconds of observation."}
    elseif($TestSuite){Write-Host "TEST SUITE: $SuiteProfile; then $ObserveSeconds seconds of normal-play observation. Both PCs must choose identical settings. PredictionCorrection runs one controlled correction; RepeatedCorrection runs three while both players stay idle; AttackCorrection injects one A then corrects three frames; RepeatedAttackCorrection repeats that attack correction three times; continuous rollback remains disabled. Restart after the run."}
    elseif($Continuous){Write-Host 'LIVE PREDICTION TEST: hold-last remote raw input, up to three unconfirmed frames, correction replay and guarded round rearming. Start in the first private lobby; no warmup. Play normally. Close BOTH games after a stop. Two-player validation is pending.'}
    elseif($StartupPreparation){Write-Host 'STARTUP CHECK: agreement, brief clock hold and four snapshots. No restoration or replay. Restart afterward.'}
    if($TestSuite -and $SuiteProfile -in @('MultiRoundReplay','FullMatchReplay')){Write-Host 'No warmup required. Start both scripts in the first private lobby before entering the match. Loading nominates the resource thread; a verified battle boundary must confirm it before replay.'}
    elseif($StartupPreparation -and -not $Continuous){Write-Host 'Before this check: after restarting SC6, enter a private match briefly, then return to the lobby without running scripts. This identifies the game thread before resource acquisition.'}
    if($LateInputs){Write-Host 'LIVE LATE-INPUT SCHEDULER: play normally. Predictions and correction requests are recorded only; no inputs are replaced and no state is restored.'}
    if($WindowReuse){Write-Host 'NATIVE WINDOW REUSE TEST: both players idle for 10 seconds after FIGHT, then play normally. Two three-frame reference replays; no prediction.'}elseif($NativeReplay){Write-Host 'THREE-FRAME REMOTE-PLAYER RIGHTWARD MOVEMENT PREDICTION/CORRECTION EXPERIMENT: both players idle for 10 seconds after round start. If restoration fails, close both games before retrying.'}elseif(-not $TestSuite -and -not $Continuous){Write-Host 'No restoration or replay occurs in this mode.'}
    Write-Host 'You can start these scripts in the lobby. Then ready up, select characters and stage, and start your PRIVATE match within 10 minutes.'
    if($CheckpointCapture){Write-Host 'Native checkpoint READINESS test: leave BOTH players idle for 5 seconds after the round starts. No restoration or replay.'}
    $peerText=if($FriendCode){$FriendCode.Trim()}else{(Read-Host "Enter your FRIEND'S numeric Steam friend code or 17-digit SteamID64").Trim()}
    if($peerText -notmatch '^\d{1,17}$'){throw 'Enter a numeric friend code or SteamID64.'}
    [UInt64]$peer=$peerText
    if($peer -gt 0 -and $peer -le 4294967295){$peer+=[UInt64]'76561197960265728'}
    if($peer -le [UInt64]'76561197960265728' -or $peer -gt [UInt64]'76561202255233023'){throw 'Invalid individual Steam ID.'}
    $localSlot=if($PlayerSlot){[string]$PlayerSlot}else{(Read-Host 'Are YOU assigned Player 1 or Player 2 in this match? Enter 1 or 2').Trim()}
    if($localSlot -notin @('1','2')){throw 'Enter your assigned player number, not current screen position.'}
    if(-not $Consent -and (Read-Host 'Both consent and are in the same PRIVATE lobby or match? Type YES') -ne 'YES'){throw 'Not started.'}
    Read-Host 'When both scripts are ready, press Enter on both PCs to start' | Out-Null
    if($gameProcess.HasExited){throw 'SC6 closed before starting.'}
    New-Item -ItemType Directory -Force -Path $captureDir | Out-Null
    if(@(Get-ChildItem -LiteralPath $captureDir -Filter '*.arm').Count){throw 'Another diagnostic is pending.'}
    $arm=Join-Path $captureDir 'steam-inputs.arm'
    $stream=[IO.File]::Open($arm,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    $armMode=if($Continuous){'PRIVATE_STEAM_CONTINUOUS'}elseif($TestSuite){'PRIVATE_STEAM_SUITE'}elseif($StartupPreparation){'PRIVATE_STEAM_PREPARE'}elseif($WindowReuse){'PRIVATE_STEAM_REUSE'}elseif($LateInputs){'PRIVATE_STEAM_LATE'}elseif($NativeReplay){'PRIVATE_STEAM_REPLAY'}elseif($CheckpointCapture){'PRIVATE_STEAM_CHECKPOINTS'}else{'PRIVATE_STEAM_INPUTS'}
    if($TestSuite -and $SuiteProfile -in @('CombatCorrection','CombatCorrectionBaseline')){Write-Host 'COMBAT CORRECTION: play normally from round start. Each attempt first restores and replays the three original-input frames. Prediction runs only after that reference replay passes. Clean pre-mutation rejections and inconclusive windows are skipped automatically: up to 12 attempts with fresh bilateral frame negotiation between attempts; clean unstarted round-boundary cancellations wait and retry. Stop after three corrections verified on both PCs. Stop if restart is required.'}
    if($TestSuite -and $SuiteProfile -eq 'OwnedRecordingReplay'){Write-Host 'OWNED RECORDING: play normally. Retain four consecutive snapshots, rewind three frames, replay ORIGINAL inputs and verify, then release dependencies. One bounded attempt; no wall break or prediction difference required. Restart if requested.'}
    if($TestSuite -and $SuiteProfile -eq 'RepeatedOwnedReplay'){Write-Host 'REPEATED OWNED REPLAY: play normally. Three four-snapshot windows, spaced 120 simulation frames after each completed window. Three original frames replayed per cycle. No deliberate wrong inputs or wall break required. Round changes cancel; any unsafe failure stops.'}
    if($TestSuite -and $SuiteProfile -eq 'FullMatchReplay'){Write-Host 'FULL MATCH: play normally through the entire match, then keep both games open until reports finish. No warmup. Original inputs only; no prediction. Writer backpressure safely skips captures and is reported.'}
    if($TestSuite -and $SuiteProfile -eq 'MultiRoundReplay'){Write-Host 'MULTI-ROUND REPLAY: play normally. Three three-frame original-input replay cycles, spaced at least 120 game frames apart, with full retirement between cycles and round-transition guards. No continuous rollback. Keep games open for reports.'}
    $suiteArgs=if($TestSuite){' '+(@{Boundary=1;Hold=2;Snapshots=3;ReferenceReplay=4;PredictionCorrection=5;RepeatedCorrection=6;AttackCorrection=7;RepeatedAttackCorrection=8;CombatCorrection=9;CombatCorrectionBaseline=10;TargetedRetention=11;RetainedObjectReplay=12;OwnedRecordingReplay=13;RepeatedOwnedReplay=14;MultiRoundReplay=15;FullMatchReplay=16}[$SuiteProfile])+' '+$ObserveSeconds}else{''}
    try{$bytes=[Text.Encoding]::ASCII.GetBytes("$armMode $($gameProcess.Id) $peer $localSlot$suiteArgs`n");$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
    if($TestSuite -and $SuiteProfile -in @('CombatCorrection','CombatCorrectionBaseline')){Write-Host 'Wait for both scripts to connect, then start the match and play normally from FIGHT. Keep SC6 open until the suite finishes.'}
    elseif($TestSuite -and $SuiteProfile -in @('TargetedRetention','RetainedObjectReplay','OwnedRecordingReplay','RepeatedOwnedReplay','MultiRoundReplay','FullMatchReplay')){Write-Host 'Wait for both scripts to connect, then start the match and play normally from FIGHT. Keep both games open until the suite finishes.'}
    elseif($TestSuite){Write-Host 'Wait for both scripts to connect, then start the private match. For any replay/correction profile, stay idle until observing_suite_gameplay. Keep SC6 open until the suite finishes.'}
    elseif($Continuous){Write-Host 'Start the private match and play normally. Activation follows boundary agreement. If either script stops or the game freezes, close BOTH games and share the report ZIPs.'}
    elseif($StartupPreparation){Write-Host 'Start a private match within four minutes. No idle inputs or round change are required. Startup preparation ends once the live clock is observed, at least ten seconds after attachment.'}
    else{Write-Host 'Return to SC6. You have 10 minutes to start the match. Recording waits for a NEW round-start event, then captures 7,200 frames (about 2 minutes). Allow up to 4 more minutes for capture and delivery. If already in a match, finish the current round.'}
    $deadline=(Get-Date).AddSeconds($(if($TestSuite -and $SuiteProfile -in @('MultiRoundReplay','FullMatchReplay')){1215}else{855}))
    $lastStatus=""
    $finalText=$null
    do {
        Start-Sleep -Seconds 2
        if($gameProcess.HasExited){throw 'SC6 exited during the test.'}
        if(Test-Path -LiteralPath $report){
            $text=Read-ReportSnapshot $report
            if($null -eq $text){continue}
            if($text -match '(?m)^status=([^\r\n]+)'){
                $currentStatus=$Matches[1]
                if($currentStatus -ne $lastStatus){Write-Host "Status: $currentStatus";$lastStatus=$currentStatus}
                if(($NativeReplay -or $WindowReuse) -and $text -match '(?m)^native_restart_required=1\r?$'){$finalText=$text;break}
                if($currentStatus -notin @('running','continuous_running','waiting_for_local_round_marker','waiting_for_combat_clock','waiting_for_combat_resources','waiting_for_resource_retirement','waiting_for_boundary_agreement','waiting_for_boundary_hold','waiting_for_boundary_snapshots','observing_suite_gameplay','observing_targeted_retention','observing_retained_object_replay','flushing_full_match_evidence','waiting_for_retained_failure_evidence','waiting_for_suite_peer_result','waiting_for_reference_replay','waiting_for_prediction_correction','waiting_for_repeated_correction','waiting_for_continuous_activation')){$finalText=$text;break}
            }
        }
    }while((Get-Date) -lt $deadline)
    if($null -ne $finalText){
        Write-Host $finalText;Write-Host "Report: $report"
        if($Continuous){
            Start-Sleep -Seconds 2
            & (Join-Path $PSScriptRoot 'Collect-RollbackReport.ps1') -ReportPath $report
            if($finalText -match '(?m)^status=continuous_unavailable_default_networking\r?$'){Write-Host 'Continuous rollback unavailable: general combat restoration is not implemented. Default gameplay remains active; no restart is required by this check.';exit 0}
            Write-Host 'Experiment stopped. Close BOTH games and send both report ZIPs. No PASS is inferred from transport or update counts.'
            if($finalText -match '(?m)^status=continuous_capture_ended_restart_required\r?$'){exit 0}
            exit 1
        }
        if($TestSuite){
            & (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report
            if($finalText -match '(?m)^status=SUITE_MATCH_ENDED_INCOMPLETE\r?$'){Write-Host 'Match ended with verified partial replay coverage and clean retirement. Incomplete, not a restoration failure or PASS. Restart before another suite.';return}
            if($finalText -match '(?m)^status=SUITE_RETENTION_NOT_OBSERVED\r?$'){Write-Host 'Retention lifecycle was not observed on both PCs. Read individual stage results; this is inconclusive, not a failure or PASS. Restart before another suite.';return}
            if($finalText -match '(?m)^status=SUITE_NO_SUITABLE_WINDOW\r?$'){Write-Host 'No suitable window observed. Neutral windows were cleaned up; no correction was verified. Restart before another suite.';return}
            if($finalText -notmatch '(?m)^status=SUITE_COMPLETE\r?$'){throw 'Suite did not pass all selected stages on both PCs. Read the summary; restart before another run.'}
            Write-Host 'Selected stages passed on both PCs. Rollback remains inactive. Restart before another suite.'
            exit 0
        }
        if($StartupPreparation){
            if($finalText -notmatch '(?m)^status=PREPARED_NOT_ACTIVATED\r?$'){throw 'Startup preparation failed. Share this report and restart SC6.'}
            if($finalText -notmatch '(?m)^startup_snapshots_captured=4\r?$' -or $finalText -notmatch '(?m)^startup_snapshots_retired=1\r?$' -or $finalText -notmatch '(?m)^startup_snapshots_failed=0\r?$' -or $finalText -notmatch '(?m)^startup_hold_state=3\r?$' -or $finalText -notmatch '(?m)^startup_boundary_agreed=1\r?$' -or $finalText -notmatch '(?m)^startup_clock_resource_ready=1\r?$' -or $finalText -notmatch '(?m)^startup_resource_ready=1\r?$' -or $finalText -notmatch '(?m)^startup_resource_retired=1\r?$' -or $finalText -notmatch '(?m)^startup_resource_failed=0\r?$'){throw 'Resource preparation or cleanup did not pass. Share the report and restart SC6.'}
            Write-Host 'Startup preparation completed. Rollback was NOT activated. Share both reports and restart SC6 before another test.'
            exit 0
        }
        Write-Host 'PASS refers to transport. Inspect checkpoint and handoff results separately; rollback remains disabled.'
        if(($NativeReplay -or $WindowReuse) -and $finalText -match '(?m)^native_restart_required=1\r?$'){throw 'Restoration was attempted but replay did not verify. Close both games; send this report.'}
        if($NativeReplay -and ($finalText -notmatch '(?m)^native_depth_compared=3\r?$' -or $finalText -notmatch '(?m)^native_depth_rewound=3\r?$')){throw 'Three-frame remote correction did not pass. Send the report.'}
        if($NativeReplay -and $finalText -notmatch '(?m)^native_replay_verified=1\r?$'){throw 'Three-frame replay did not pass. Send this report; do not treat transport PASS as replay success.'}
        if($WindowReuse -and ($finalText -notmatch '(?m)^native_window_reuse_completed=2\r?$' -or $finalText -notmatch '(?m)^native_replay_verified=1\r?$' -or $finalText -notmatch '(?m)^native_checkpoint_failure=0\r?$')){throw 'Native window reuse did not pass; send report and restart both games.'}
        if($finalText -notmatch '(?m)^status=PASS\r?$'){throw 'Input exchange did not finish with PASS. Share the report; restart both games before retrying.'}
    }else{throw "No complete final report before the deadline. The report may still be updating: $report"}

}catch{
    if($Continuous -and $report -and (Test-Path -LiteralPath $report)){
        try { & (Join-Path $PSScriptRoot 'Collect-RollbackReport.ps1') -ReportPath $report } catch { Write-Host 'Report ZIP could not be created; retain the captures folder.' }
    }
    if($TestSuite -and $report -and (Test-Path -LiteralPath $report)){
        try { & (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report } catch { Write-Host 'Summary could not be written; retain the raw report.' }
    }
    Write-Host ('ERROR: '+$_.Exception.Message) -ForegroundColor Red;exit 1
}



