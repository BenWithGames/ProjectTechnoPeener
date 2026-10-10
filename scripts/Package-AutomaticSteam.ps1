$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$destination=Join-Path $project ('build-network\auto-connect-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$payload=Join-Path $destination 'observer-mod'
New-Item -ItemType Directory -Force -Path $payload | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Install-Observer.cmd') -Destination $destination
$text=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Install-Observer.ps1')).Replace('Installed and verified observer.','Installed and verified automatic compatibility build.').Replace('Launch SC6 through Steam. No recording starts until Observe-Inputs.cmd is explicitly run.','Launch SC6 through Steam and connect normally. Compatibility handshake and ping start automatically. Game networking stays unchanged.')
[IO.File]::WriteAllText((Join-Path $destination 'Install-Observer.ps1'),$text)
foreach($name in @('dwmapi.dll','UE4SS.dll','UE4SS-settings.ini','UE4SS-LICENSE.txt','MINHOOK-LICENSE.txt','main.dll')){
    $source=if($name -eq 'main.dll'){Join-Path $project 'package\Mods\SC6Rollback\dlls\main.dll'}else{Join-Path $project ('package\'+$name)}
    Copy-Item -LiteralPath $source -Destination (Join-Path $payload $name)
}
@'
SC6Rollback Automatic Connection v23

1. Close SC6, extract this ZIP, and run Install-Observer.cmd.
2. Launch SC6 through Steam. Connect to another player normally.
3. No friend code, YES prompt, or test script is needed.

Automatic compatibility and ping operate independently of private/ranked mode.
Discovery requires recent successful traffic in BOTH directions on the game's
observed Steam channel 20001 and an active Steam session. If multiple peers
are present, automatic negotiation waits instead of guessing an opponent.
A peer expires after 2 seconds without two-way native game traffic.
Both peers need this automatic build. Old test-only builds do not negotiate.

The handshake uses channel 27862, fresh challenges and a version check.
Ping refreshes every 3 seconds. It estimates RTT, including residual Steam/OS
polling and queueing; it is not pure wire latency.

This build does NOT enable continuous rollback or run prediction/restoration
experiments automatically. Default game networking remains active for everyone,
including compatible peers. Unmodded, incompatible, absent or ambiguous peers
leave default networking unchanged. No router or firewall setup is added.

Reports: Mods/SC6Rollback/captures/steam-auto-PID.txt
compatible_default_networking means compatibility succeeded, not rollback.
Live private/ranked validation is still pending for this new feature.
If ping is unavailable, share that report; do not run an old replay script.

To remove: close SC6 and disable/remove Mods/SC6Rollback/dlls/main.dll.
Installer backups are in Mods/SC6Rollback/install-backups.
'@ | Set-Content -LiteralPath (Join-Path $destination 'README.txt') -Encoding ascii
$zip=Join-Path $project 'build-network\SC6Rollback-Auto-Connect-v23.zip'
Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $zip -Force
Get-FileHash -LiteralPath $zip
