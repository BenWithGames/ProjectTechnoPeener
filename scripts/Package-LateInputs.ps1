$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$destination=Join-Path $project ('build-network\steam-inputs-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$payload=Join-Path $destination 'observer-mod'
New-Item -ItemType Directory -Force -Path $payload | Out-Null
foreach($name in @('Steam-LateInputs.cmd','Steam-Replay.cmd','Steam-Checkpoints.cmd','Steam-Inputs.cmd','Steam-Inputs.ps1','Install-Observer.cmd','Install-Observer.ps1','Stop-Steam-Ping.cmd','Stop-Steam-Ping.ps1')){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $destination
}
$installer=Join-Path $destination 'Install-Observer.ps1'
$installerText=[IO.File]::ReadAllText($installer).Replace('Installed and verified observer.','Installed and verified Steam input exchange build.').Replace('No recording starts until Observe-Inputs.cmd is explicitly run.','Start a private match with your consenting friend, then run Steam-LateInputs.cmd on both PCs.')
[IO.File]::WriteAllText($installer,$installerText)
foreach($name in @('dwmapi.dll','UE4SS.dll','UE4SS-settings.ini','UE4SS-LICENSE.txt','MINHOOK-LICENSE.txt','main.dll')){
    $source=if($name -eq 'main.dll'){Join-Path $project 'package\Mods\SC6Rollback\dlls\main.dll'}else{Join-Path $project ('package\'+$name)}
    Copy-Item -LiteralPath $source -Destination (Join-Path $payload $name)
}
@'
SC6 live late-input scheduler v28
This build records prediction decisions on the game thread BEFORE confirmation.
It reports real late-input mismatches and requested correction depths. No prediction
is applied to gameplay, no snapshot is restored, and default networking remains active.

Both players:
1. Close SC6, extract ZIP, run Install-Observer.cmd and select your Steam game folder.
2. Launch SC6, join the same private lobby, run Steam-LateInputs.cmd on BOTH PCs.
3. Enter your friend's code and your assigned P1/P2 side, confirm private consent,
   then press Enter on both PCs before starting the match.
4. Play normally from FIGHT; no idle period is required. Record 7200 frames (~2 min).
5. Share steam-inputs-PID.txt and steam-inputs-PID-late.csv from the captures folder.

Transport PASS is not a correction result. late_errors counts real mismatches against
hold-last-input predictions (edge bits cleared); late_requests counts boundary batches
within 3 frames. Over-limit and expired records are reported separately. The history
horizon is 15 frames. No new input delay is introduced. Existing stock input delay
remains; these records cannot establish the latency of a replacement netcode system.
Automatic compatibility/ping and unmodded-opponent default fallback are included.
Steam-Replay.cmd remains the separate explicit three-frame fixture, not this test.
'@ | Set-Content -LiteralPath (Join-Path $destination 'README.txt') -Encoding ascii
$zip=Join-Path $project 'build-network\SC6Rollback-Late-Inputs-v28.zip'
Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $zip -Force
Get-FileHash -LiteralPath $zip


