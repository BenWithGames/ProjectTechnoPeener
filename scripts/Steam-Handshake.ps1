$ErrorActionPreference='Stop'
try {
    $gameProcess=@(Get-Process SoulcaliburVI -ErrorAction Stop)
    if($gameProcess.Count -ne 1){throw 'Start one SC6 instance through Steam first.'}
    $gameProcess=$gameProcess[0]
    $gameBin=Split-Path $gameProcess.Path -Parent
    if((Get-FileHash -LiteralPath $gameProcess.Path).Hash -ne 'F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553'){throw 'Unsupported game build.'}
    $captureDir=Join-Path $gameBin 'Mods\SC6Rollback\captures'
    $report=Join-Path $captureDir ('steam-handshake-'+$gameProcess.Id+'.txt')
    if(Test-Path -LiteralPath $report){throw 'This game launch already ran a handshake. Restart SC6 on both PCs before retrying.'}
    Write-Host 'Both players: install this build, launch SC6, and join the same private lobby.'
    Write-Host 'Stay in the private lobby for this test. Do not queue ranked. No game inputs are needed.'
    $peerText=(Read-Host "Enter your FRIEND'S numeric Steam friend code or 17-digit SteamID64").Trim()
    if($peerText -notmatch '^\d{1,17}$'){throw 'Enter a numeric friend code or SteamID64.'}
    [UInt64]$peer=$peerText
    if($peer -gt 0 -and $peer -le 4294967295){$peer+=[UInt64]'76561197960265728'}
    if($peer -le [UInt64]'76561197960265728' -or $peer -gt [UInt64]'76561202255233023'){throw 'Invalid individual Steam ID.'}
    if((Read-Host 'Both of you consent to this handshake and are in the same private lobby? Type YES') -cne 'YES'){throw 'Not started.'}
    Read-Host 'When both scripts are ready, press Enter on both PCs to start' | Out-Null
    if($gameProcess.HasExited){throw 'SC6 closed before starting.'}
    New-Item -ItemType Directory -Force -Path $captureDir | Out-Null
    if(@(Get-ChildItem -LiteralPath $captureDir -Filter '*.arm').Count){throw 'Another diagnostic is pending.'}
    $arm=Join-Path $captureDir 'steam-handshake.arm'
    $stream=[IO.File]::Open($arm,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$bytes=[Text.Encoding]::ASCII.GetBytes("PRIVATE_STEAM_HANDSHAKE $($gameProcess.Id) $peer`n");$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
    Write-Host 'Running for 60 seconds. Leave SC6 open in the private lobby.'
    $deadline=(Get-Date).AddSeconds(75)
    do {
        Start-Sleep -Seconds 2
        if($gameProcess.HasExited){throw 'SC6 exited during the test.'}
        if(Test-Path -LiteralPath $report){$text=[IO.File]::ReadAllText($report);if($text -match '(?m)^status=(?!running\b)\S+') {break}}
    }while((Get-Date) -lt $deadline)
    if(Test-Path -LiteralPath $report){
        Get-Content -LiteralPath $report;Write-Host "Report: $report"
        if([IO.File]::ReadAllText($report) -notmatch '(?m)^status=PASS\r?$'){throw 'Handshake did not finish with PASS. Share the report; restart both games before retrying.'}
    }else{throw 'No report. Check that the new DLL is installed, then restart SC6.'}
}catch{Write-Host ('ERROR: '+$_.Exception.Message) -ForegroundColor Red;exit 1}
