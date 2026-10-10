param([switch]$CheckpointCapture,[switch]$NativeReplay,[switch]$LateInputs)
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
    $gameProcess=@(Get-Process SoulcaliburVI -ErrorAction Stop)
    if($gameProcess.Count -ne 1){throw 'Start one SC6 instance through Steam first.'}
    $gameProcess=$gameProcess[0]
    $gameBin=Split-Path $gameProcess.Path -Parent
    if((Get-FileHash -LiteralPath $gameProcess.Path).Hash -ne 'F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553'){throw 'Unsupported game build.'}
    $captureDir=Join-Path $gameBin 'Mods\SC6Rollback\captures'
    $report=Join-Path $captureDir ('steam-inputs-'+$gameProcess.Id+'.txt')
    if(Test-Path -LiteralPath $report){throw 'This game launch already ran an input exchange. Restart SC6 on both PCs before retrying.'}
    Write-Host 'Both players: install this build, launch SC6, and join the same private lobby.'
    Write-Host 'Both PCs need this build and opposite assigned player slots.'
    if($LateInputs){Write-Host 'LIVE LATE-INPUT SCHEDULER: play normally. Predictions and correction requests are recorded only; no inputs are replaced and no state is restored.'}
    if($NativeReplay){Write-Host 'THREE-FRAME REMOTE-PLAYER RIGHTWARD MOVEMENT PREDICTION/CORRECTION EXPERIMENT: both players idle for 10 seconds after round start. If restoration fails, close both games before retrying.'}else{Write-Host 'No restoration or replay occurs in this mode.'}
    Write-Host 'You can start these scripts in the lobby. Then ready up, select characters and stage, and start your PRIVATE match within 10 minutes.'
    if($CheckpointCapture){Write-Host 'Native checkpoint READINESS test: leave BOTH players idle for 5 seconds after the round starts. No restoration or replay.'}
    $peerText=(Read-Host "Enter your FRIEND'S numeric Steam friend code or 17-digit SteamID64").Trim()
    if($peerText -notmatch '^\d{1,17}$'){throw 'Enter a numeric friend code or SteamID64.'}
    [UInt64]$peer=$peerText
    if($peer -gt 0 -and $peer -le 4294967295){$peer+=[UInt64]'76561197960265728'}
    if($peer -le [UInt64]'76561197960265728' -or $peer -gt [UInt64]'76561202255233023'){throw 'Invalid individual Steam ID.'}
    $localSlot=(Read-Host 'Are YOU assigned Player 1 or Player 2 in this match? Enter 1 or 2').Trim()
    if($localSlot -notin @('1','2')){throw 'Enter your assigned player number, not current screen position.'}
    if((Read-Host 'Both consent and are in the same PRIVATE lobby or match? Type YES') -ne 'YES'){throw 'Not started.'}
    Read-Host 'When both scripts are ready, press Enter on both PCs to start' | Out-Null
    if($gameProcess.HasExited){throw 'SC6 closed before starting.'}
    New-Item -ItemType Directory -Force -Path $captureDir | Out-Null
    if(@(Get-ChildItem -LiteralPath $captureDir -Filter '*.arm').Count){throw 'Another diagnostic is pending.'}
    $arm=Join-Path $captureDir 'steam-inputs.arm'
    $stream=[IO.File]::Open($arm,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    $armMode=if($LateInputs){'PRIVATE_STEAM_LATE'}elseif($NativeReplay){'PRIVATE_STEAM_REPLAY'}elseif($CheckpointCapture){'PRIVATE_STEAM_CHECKPOINTS'}else{'PRIVATE_STEAM_INPUTS'}
    try{$bytes=[Text.Encoding]::ASCII.GetBytes("$armMode $($gameProcess.Id) $peer $localSlot`n");$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
    Write-Host 'Return to SC6. You have 10 minutes to start the match. Recording waits for a NEW round-start event, then captures 7,200 frames (about 2 minutes). Allow up to 4 more minutes for capture and delivery. If already in a match, finish the current round.'
    $deadline=(Get-Date).AddSeconds(855)
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
                if($NativeReplay -and $text -match '(?m)^native_restart_required=1\r?$'){$finalText=$text;break}
                if($currentStatus -notin @('running','waiting_for_local_round_marker')){$finalText=$text;break}
            }
        }
    }while((Get-Date) -lt $deadline)
    if($null -ne $finalText){
        Write-Host $finalText;Write-Host "Report: $report"
        Write-Host 'PASS refers to transport. Inspect checkpoint and handoff results separately; rollback remains disabled.'
        if($NativeReplay -and $finalText -match '(?m)^native_restart_required=1\r?$'){throw 'Restoration was attempted but replay did not verify. Close both games; send this report.'}
        if($NativeReplay -and ($finalText -notmatch '(?m)^native_depth_compared=3\r?$' -or $finalText -notmatch '(?m)^native_depth_rewound=3\r?$')){throw 'Three-frame remote correction did not pass. Send the report.'}
        if($NativeReplay -and $finalText -notmatch '(?m)^native_replay_verified=1\r?$'){throw 'Three-frame replay did not pass. Send this report; do not treat transport PASS as replay success.'}
        if($finalText -notmatch '(?m)^status=PASS\r?$'){throw 'Input exchange did not finish with PASS. Share the report; restart both games before retrying.'}
    }else{throw "No complete final report before the deadline. The report may still be updating: $report"}

}catch{Write-Host ('ERROR: '+$_.Exception.Message) -ForegroundColor Red;exit 1}


