$ErrorActionPreference='Stop'
try {
    $gameProcess=@(Get-Process SoulcaliburVI -ErrorAction Stop)
    if($gameProcess.Count -ne 1){throw 'Start one SC6 instance through Steam first.'}
    $gameProcess=$gameProcess[0]
    $gameBin=Split-Path $gameProcess.Path -Parent
    if((Get-FileHash -LiteralPath $gameProcess.Path).Hash -ne 'F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553'){throw 'Unsupported game build.'}
    $captureDir=Join-Path $gameBin 'Mods\SC6Rollback\captures'
    $report=Join-Path $captureDir ('steam-inputs-'+$gameProcess.Id+'.txt')
    if(Test-Path -LiteralPath $report){throw 'This game launch already ran an input exchange. Restart SC6 on both PCs before retrying.'}
    Write-Host 'Both players: install this build, launch SC6, and start a match in the same private lobby.'
    Write-Host 'Start a PRIVATE MATCH before proceeding. Return to the game immediately after Enter and move or attack normally for at least 35 seconds.'
    $peerText=(Read-Host "Enter your FRIEND'S numeric Steam friend code or 17-digit SteamID64").Trim()
    if($peerText -notmatch '^\d{1,17}$'){throw 'Enter a numeric friend code or SteamID64.'}
    [UInt64]$peer=$peerText
    if($peer -gt 0 -and $peer -le 4294967295){$peer+=[UInt64]'76561197960265728'}
    if($peer -le [UInt64]'76561197960265728' -or $peer -gt [UInt64]'76561202255233023'){throw 'Invalid individual Steam ID.'}
    $localSlot=(Read-Host 'Are YOU assigned Player 1 or Player 2 in this match? Enter 1 or 2').Trim()
    if($localSlot -notin @('1','2')){throw 'Enter your assigned player number, not current screen position.'}
    if((Read-Host 'Both consent and are in an active PRIVATE MATCH? Type YES') -ne 'YES'){throw 'Not started.'}
    Read-Host 'When both scripts are ready, press Enter on both PCs to start' | Out-Null
    if($gameProcess.HasExited){throw 'SC6 closed before starting.'}
    New-Item -ItemType Directory -Force -Path $captureDir | Out-Null
    if(@(Get-ChildItem -LiteralPath $captureDir -Filter '*.arm').Count){throw 'Another diagnostic is pending.'}
    $arm=Join-Path $captureDir 'steam-inputs.arm'
    $stream=[IO.File]::Open($arm,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$bytes=[Text.Encoding]::ASCII.GetBytes("PRIVATE_STEAM_INPUTS $($gameProcess.Id) $peer $localSlot`n");$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
    Write-Host 'Return to SC6 NOW and play normally for at least 35 seconds. The test records 1,800 frames (about 30 seconds), then verifies delivery; allow up to 3 minutes.'
    $deadline=(Get-Date).AddSeconds(195)
    do {
        Start-Sleep -Seconds 2
        if($gameProcess.HasExited){throw 'SC6 exited during the test.'}
        if(Test-Path -LiteralPath $report){$text=[IO.File]::ReadAllText($report);if($text -match '(?m)^status=(?!running\b)\S+') {break}}
    }while((Get-Date) -lt $deadline)
    if(Test-Path -LiteralPath $report){
        Get-Content -LiteralPath $report;Write-Host "Report: $report"
        Write-Host 'PASS refers to transport. Check shadow_over_limit and shadow_within_observed_bounds separately; rollback is disabled.'
        if([IO.File]::ReadAllText($report) -notmatch '(?m)^status=PASS\r?$'){throw 'Input exchange did not finish with PASS. Share the report; restart both games before retrying.'}
    }else{throw 'No report. Check that the new DLL is installed, then restart SC6.'}
}catch{Write-Host ('ERROR: '+$_.Exception.Message) -ForegroundColor Red;exit 1}

