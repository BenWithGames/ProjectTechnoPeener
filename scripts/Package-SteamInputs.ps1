$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$destination=Join-Path $project ('build-network\steam-inputs-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$payload=Join-Path $destination 'observer-mod'
New-Item -ItemType Directory -Force -Path $payload | Out-Null
foreach($name in @('Steam-Inputs.cmd','Steam-Inputs.ps1','Install-Observer.cmd','Install-Observer.ps1','Stop-Steam-Ping.cmd','Stop-Steam-Ping.ps1')){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $destination
}
$installer=Join-Path $destination 'Install-Observer.ps1'
$installerText=[IO.File]::ReadAllText($installer).Replace('Installed and verified observer.','Installed and verified Steam input exchange build.').Replace('No recording starts until Observe-Inputs.cmd is explicitly run.','Start a private match with your consenting friend, then run Steam-Inputs.cmd on both PCs.')
[IO.File]::WriteAllText($installer,$installerText)
foreach($name in @('dwmapi.dll','UE4SS.dll','UE4SS-settings.ini','UE4SS-LICENSE.txt','MINHOOK-LICENSE.txt','main.dll')){
    $source=if($name -eq 'main.dll'){Join-Path $project 'package\Mods\SC6Rollback\dlls\main.dll'}else{Join-Path $project ('package\'+$name)}
    Copy-Item -LiteralPath $source -Destination (Join-Path $payload $name)
}
@'
SC6 Steam live shadow-prediction diagnostic -- NO rollback, restoration, or input injection

Both players:
1. Close SC6, extract this ZIP, run Install-Observer.cmd, and enter the Steam
   game installation folder. Wait for installation success and close installer.
2. Launch SC6 through Steam. Join the same PRIVATE lobby and START A MATCH.
3. Open Steam-Inputs.cmd. Enter the OTHER player's Steam friend code or SteamID64, then YOUR assigned Player 1/2 side.
4. Confirm consent. When both scripts are ready, press Enter together.
5. Immediately return focus to SC6 and move or attack normally for at least 35 seconds.
   The capture observes the next 1,800 frame boundaries after its Steam handshake.
6. Allow up to 3 minutes for delivery verification. Send BOTH reports, plus each
   machine's steam-inputs-PID-local.csv, -remote.csv, -timing.csv and -shadow.csv.

PING: after the mutual handshake, the overlay displays measured echo RTT.
A fresh ping is sent every 3 seconds. RTT includes application polling time.
It keeps running after the capture PASS while the selected peer responds.
Missing samples become -- after 9 seconds. No peer response for 15 seconds
after capture stops telemetry. Run Stop-Steam-Ping.cmd or close SC6 to stop.
Both players should stop this diagnostic before leaving private play.
DELAY and ROLLBACK stay -- because rollback is disabled.

No relay program, VPN, firewall prompt, or router forwarding is part of this test.
Do not run the old handshake or Network-Test scripts. Restart BOTH games before
any retry. Only one attempt is supported per game launch.

PASS proves recorded bytes were delivered and their checksums agreed.
The shadow scheduler simulates 4 frames of delay and repeats held inputs while
clearing newly-pressed bits. It does NOT delay the actual game.
shadow_over_limit counts estimated corrections exceeding 3 frames.
shadow_within_observed_bounds=1 is only this capture's diagnostic result.
The final 4 frames have no scheduling boundary and are excluded. Unseeded
frames, unmatched counters, timing censoring and input mismatches are reported.
No input replacement, state loading, or actual replay occurs. It does
NOT prove synchronized frame numbers, correct player mapping, prediction,
network rollback, or readiness for ranked. Play only with your consenting friend.
No automatic match-mode detection is implemented. Captures include raw input
words, local frame counters and QPC timestamps; timestamps are not synchronized.

Reports/CSVs: SoulcaliburVI/Binaries/Win64/Mods/SC6Rollback/captures.
Use channel 27861 for this diagnostic; SC6 was observed using channel 20001.
An observation is not proof the channel is unused in every other game mode.
Backups: Mods/SC6Rollback/install-backups. To revert, close SC6 and restore
main.dll.disabled from your backup as dlls/main.dll.
'@ | Set-Content -LiteralPath (Join-Path $destination 'README.txt') -Encoding ascii
$zip=Join-Path $project 'build-network\SC6SteamInputs-shadow.zip'
Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $zip -Force
Get-FileHash -LiteralPath $zip


