$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$destination=Join-Path $project ('build-network\steam-handshake-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$payload=Join-Path $destination 'observer-mod'
New-Item -ItemType Directory -Force -Path $payload | Out-Null
foreach($name in @('Steam-Handshake.cmd','Steam-Handshake.ps1','Install-Observer.cmd','Install-Observer.ps1')){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $destination
}
$installer=Join-Path $destination 'Install-Observer.ps1'
$installerText=[IO.File]::ReadAllText($installer).Replace('Installed and verified observer.','Installed and verified Steam handshake build.').Replace('No recording starts until Observe-Inputs.cmd is explicitly run.','Join your consenting friend in a private lobby, then run Steam-Handshake.cmd on both PCs.')
[IO.File]::WriteAllText($installer,$installerText)
foreach($name in @('dwmapi.dll','UE4SS.dll','UE4SS-settings.ini','UE4SS-LICENSE.txt','MINHOOK-LICENSE.txt','main.dll')){
    $source=if($name -eq 'main.dll'){Join-Path $project 'package\Mods\SC6Rollback\dlls\main.dll'}else{Join-Path $project ('package\'+$name)}
    Copy-Item -LiteralPath $source -Destination (Join-Path $payload $name)
}
@'
SC6 Steam handshake diagnostic -- rollback is DISABLED

Both players:
1. Extract this ZIP. Close SC6. Run Install-Observer.cmd and enter the
   game installation folder (Steam > Manage > Browse local files).
2. Launch SC6 through Steam and join the SAME PRIVATE LOBBY.
3. Run Steam-Handshake.cmd. Enter the OTHER player's numeric friend code
   (Steam > Friends > Add a Friend), or their numeric SteamID64.
4. Confirm private-lobby consent. Press Enter on both PCs when ready.
5. Stay in the lobby for 60 seconds. Share BOTH resulting reports.

No relay app, router forwarding, VPN, or administrator firewall prompt is
part of this test. Steam must already have an active connection to the peer.
The final report must say status=PASS on BOTH PCs. A local PASS alone does
not prove the other side finished. This is NOT gameplay rollback.

One attempt per game launch. Restart SC6 on BOTH PCs before retrying.
Reports: SoulcaliburVI/Binaries/Win64/Mods/SC6Rollback/captures/
steam-handshake-PID.txt. Reports include the chosen peer's SteamID.

Channel 27861 is used only for diagnostic challenge messages; observed
game traffic used 20001. The capture does not establish that 27861 is unused
in every game mode. Use only this consenting private-lobby test.
No automatic private/ranked detection is implemented in this diagnostic.
Do not run any offline save/restore or observer test at the same time.
Existing Steam sessions are not closed and relay settings are not changed.

The installer backs up the previous mod DLL in Mods/SC6Rollback/install-backups.
To undo, close SC6 and restore your backed-up main.dll.disabled as dlls/main.dll.
For a first installation, disable SC6Rollback in Mods/mods.txt while SC6 is closed.
'@ | Set-Content -LiteralPath (Join-Path $destination 'README.txt') -Encoding ascii
$zip=Join-Path $project 'build-network\SC6SteamHandshake.zip'
Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $zip -Force
Get-FileHash -LiteralPath $zip
