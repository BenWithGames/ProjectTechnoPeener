$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$destination=Join-Path $project ('build-network\steam-inputs-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$payload=Join-Path $destination 'observer-mod'
New-Item -ItemType Directory -Force -Path $payload | Out-Null
foreach($name in @('Steam-WindowReuse.cmd','Steam-LateInputs.cmd','Steam-Replay.cmd','Steam-Checkpoints.cmd','Steam-Inputs.cmd','Steam-Inputs.ps1','Install-Observer.cmd','Install-Observer.ps1','Stop-Steam-Ping.cmd','Stop-Steam-Ping.ps1')){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $destination
}
$installer=Join-Path $destination 'Install-Observer.ps1'
$installerText=[IO.File]::ReadAllText($installer).Replace('Installed and verified observer.','Installed and verified Steam input exchange build.').Replace('No recording starts until Observe-Inputs.cmd is explicitly run.','Start a private match with your consenting friend, then run Steam-WindowReuse.cmd on both PCs.')
[IO.File]::WriteAllText($installer,$installerText)
foreach($name in @('dwmapi.dll','UE4SS.dll','UE4SS-settings.ini','UE4SS-LICENSE.txt','MINHOOK-LICENSE.txt','main.dll')){
    $source=if($name -eq 'main.dll'){Join-Path $project 'package\Mods\SC6Rollback\dlls\main.dll'}else{Join-Path $project ('package\'+$name)}
    Copy-Item -LiteralPath $source -Destination (Join-Path $payload $name)
}
@'
SC6 native window reuse test v37
Includes blend-block rejection stage, side, block and values. Includes the first non-exempt serialized mismatch and its writer RVA. Includes independent blend/transform/camera/native comparison results. Includes the exact replay hop, expected/actual RNG, and first ring payload/header difference. Includes the first differing transform topology record. Includes up to two fresh-window retries per cycle for missing confirmations. Includes per-frame RNG values before/after saving and bounded RNG step-distance diagnostics. The RNG restoration guard is unchanged.
This explicit private test captures four idle states, rewinds three frames,
replays the recorded inputs, verifies them, then repeats using the SAME storage.
It does not apply continuous prediction or replace default networking.

Both players:
1. Close SC6. Extract this ZIP and run Install-Observer.cmd.
2. Launch SC6 and join the same private lobby.
3. Run Steam-WindowReuse.cmd, enter the other player's friend code and your P1/P2
   side, confirm consent, and press Enter when both sides are ready.
4. Start the match. Leave controls untouched for 10 seconds after FIGHT.
5. Then play normally for the rest of the 7200-frame recording (about2 minutes).
6. Share steam-inputs-PID.txt. Success requires native_window_reuse_completed=2,
   native_replay_verified=1, native_checkpoint_failure=0 and transport PASS.
If native_restart_required=1, close both games before another attempt.
Automatic compatibility/ping remains included. No ranked experimental replay.
'@ | Set-Content -LiteralPath (Join-Path $destination 'README.txt') -Encoding ascii
$zip=Join-Path $project 'build-network\SC6Rollback-Window-Reuse-v37.zip'
Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $zip -Force
Get-FileHash -LiteralPath $zip







