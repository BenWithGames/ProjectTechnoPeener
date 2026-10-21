param([string]$OutputDirectory=(Join-Path $env:USERPROFILE 'Downloads'),[string]$InstallGameFolder)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$name='SC6Rollback-AutomaticPlay-Alpha-'+$stamp
$folder=Join-Path $project ('build-network\'+$name)
$payload=Join-Path $folder 'observer-mod'
New-Item -ItemType Directory -Path $payload -Force | Out-Null
foreach($file in @('dwmapi.dll','UE4SS.dll','UE4SS-settings.ini','UE4SS-LICENSE.txt','MINHOOK-LICENSE.txt','main.dll')){
 $source=if($file -eq 'main.dll'){Join-Path $project 'package\Mods\SC6Rollback\dlls\main.dll'}else{Join-Path $project ('package\'+$file)}
 Copy-Item -LiteralPath $source -Destination (Join-Path $payload $file)
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Install-AutomaticPlay.ps1') -Destination (Join-Path $folder 'Install.ps1')
@('@echo off','powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" %*','echo.','pause') | Set-Content (Join-Path $folder 'Install.cmd') -Encoding ascii
@'
SC6Rollback Automatic Play - experimental alpha

Close SC6, extract this ZIP, and run Install.cmd once. Launch through Steam and play.
Both players must install this build to use automatic rollback. There is no test-suite script, friend-code prompt, side selection, warmup, or consent prompt. Steam's existing connection identifies the peer; the native clock identifies the local side. Ambiguous peers do not activate rollback.

Reports are saved automatically inside the game's Win64 folder:
Mods\SC6Rollback\logs\match-<process>-<time>\
Send the entire matching folder after a problem. Nothing is uploaded automatically.
Clean completed matches rearm for a new match in the same process. A failed restoration remains fail-closed: restart SC6 after a failure. This build does not promise recovery from corrupted or unverified state.

All exact verification, low-latency prediction, diagnostics and round safety checks remain enabled. Added automatic startup and clean-match retirement need real two-PC validation, including rematches, lobby returns, switching sides, switching opponents, and disconnects. Treat this as an alpha, not a stable release.

If automatic Steam-library discovery finds multiple installs, run:
Install.cmd -GameFolder "D:\SteamLibrary\steamapps\common\SoulcaliburVI"

To disable automatic startup, remove Mods\SC6Rollback\automatic-play.enabled while SC6 is closed. To disable the whole mod, set SC6Rollback : 0 in Mods\mods.txt.
'@ | Set-Content (Join-Path $folder 'README.txt')
Copy-Item -LiteralPath (Join-Path $project 'docs\OVERLAY_PING_WORKER.md') -Destination $folder
$dllHash=(Get-FileHash (Join-Path $payload 'main.dll')).Hash
@{build=$name;dllSHA256=$dllHash;automaticPlay=$true;lowLatency=$true;inputDelayFrames=0;predictionLimitFrames=3;logs='Mods/SC6Rollback/logs';liveValidated=$false} | ConvertTo-Json | Set-Content (Join-Path $folder 'Build.json')
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=Join-Path $OutputDirectory ($name+'.zip')
[IO.Compression.ZipFile]::CreateFromDirectory($folder,$zip)
if($InstallGameFolder){& (Join-Path $folder 'Install.ps1') -GameFolder $InstallGameFolder;if(-not $?){throw 'Installation failed'}}
[pscustomobject]@{Folder=$folder;Zip=$zip;DllSHA256=$dllHash;ZipSHA256=(Get-FileHash $zip).Hash}
