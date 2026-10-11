$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$destination=Join-Path $project ('build-network\steam-inputs-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$payload=Join-Path $destination 'observer-mod'
New-Item -ItemType Directory -Force -Path $payload | Out-Null
foreach($name in @('Steam-WindowReuse.cmd','Steam-LateInputs.cmd','Steam-Replay.cmd','Steam-Checkpoints.cmd','Steam-Inputs.cmd','Steam-Inputs.ps1','Install-Observer.cmd','Install-Observer.ps1','Stop-Steam-Ping.cmd','Stop-Steam-Ping.ps1')){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $destination
}
$installer=Join-Path $destination 'Install-Observer.ps1'
$installerText=[IO.File]::ReadAllText($installer).Replace('Installed and verified observer.','Installed and verified Steam input exchange build.').Replace('No recording starts until Observe-Inputs.cmd is explicitly run.','Read README.txt: warm up in a private match, return to the lobby, then run Steam-WindowReuse.cmd before starting the next match.')
[IO.File]::WriteAllText($installer,$installerText)
foreach($name in @('dwmapi.dll','UE4SS.dll','UE4SS-settings.ini','UE4SS-LICENSE.txt','MINHOOK-LICENSE.txt','main.dll')){
    $source=if($name -eq 'main.dll'){Join-Path $project 'package\Mods\SC6Rollback\dlls\main.dll'}else{Join-Path $project ('package\'+$name)}
    Copy-Item -LiteralPath $source -Destination (Join-Path $payload $name)
}
@'
SC6Rollback - Owned Replay - Topology Retry
Build: ownership-backed three-frame reference replay with lobby-time hooks.
This is an explicit private test. Continuous rollback remains disabled.

Both players:
1. Close SC6, extract the ZIP, then run Install-Observer.cmd.
   Paste your SC6 installation folder when prompted.
2. Launch SC6. Briefly enter a private match together, then return to the lobby.
   Do not run any test scripts during this warmup.
3. In the lobby run Steam-WindowReuse.cmd. Enter the OTHER player's friend code,
   your assigned P1/P2 slot, and YES. Stop at the final Enter prompt until both
   players are ready. If Codex starts Ben's side, only the partner runs this CMD.
4. Press Enter when coordinated. Wait for both scripts to connect before
   starting the next match. Ownership hooks must be armed before stage loading.
5. Leave BOTH players' controls untouched for 10 seconds after FIGHT.
6. Then play normally during the 7200-frame recording (about 2 minutes).
7. Share captures/steam-inputs-PID.txt from the installed mod folder.

The test attempts two three-frame reference replays using retained resources.
It may discard up to three untouched windows with changing transform objects.
native_window_topology_retries reports skips; this does not support replay across object creation/deletion.
Transport PASS alone is insufficient. Check native_owned_replay_started=1,
native_owned_replay_captures, native_owned_replay_cleanup=1,
native_window_reuse_completed=2, native_replay_verified=1 and
native_checkpoint_failure=0. Guards may reject before restoration.
Cancellation reason: 1 descriptor thread, 2 descriptor changed, 3 capacity,
4 descriptor acquire, 5 buffer thread, 6 buffer acquire, 7 resource clear,
8 setup thread, 9 retained root replaced. Zero means no cancellation recorded.
Reports include cancellation details and the first serialized mismatch aligned 64-bit values and changed-byte count.
If native_restart_required=1, close both games before another attempt.
Use only consenting private matches for this explicit test.
Automatic compatibility and ping remain included.
'@ | Set-Content -LiteralPath (Join-Path $destination 'README.txt') -Encoding ascii
$zip=Join-Path $project 'build-network\SC6Rollback-Owned-Replay-TopologyRetry.zip'
Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $zip -Force
Get-FileHash -LiteralPath $zip







