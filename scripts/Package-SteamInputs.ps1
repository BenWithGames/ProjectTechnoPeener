$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$destination=Join-Path $project ('build-network\steam-inputs-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$payload=Join-Path $destination 'observer-mod'
New-Item -ItemType Directory -Force -Path $payload | Out-Null
foreach($name in @('Steam-Replay.cmd','Steam-Checkpoints.cmd','Steam-Inputs.cmd','Steam-Inputs.ps1','Install-Observer.cmd','Install-Observer.ps1','Stop-Steam-Ping.cmd','Stop-Steam-Ping.ps1')){
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
SC6 replay comparison and confirmation diagnostic v21 -- use Steam-Replay.cmd for the next test.
This simulates one P1 rightward press, restores, and replays confirmed neutral input.
Both machines use the same controlled P1 fixture. This is not automatic late-input correction.
Success requires measured state divergence followed by verified correction. Continuous rollback remains disabled.

Both players:
1. Close SC6, extract this ZIP, run Install-Observer.cmd, and enter the Steam
   game installation folder. Wait for installation success and close installer.
2. Launch SC6 through Steam. Join the same PRIVATE lobby. Start the scripts BEFORE readying up.
3. For the movement prediction/correction test, open Steam-Replay.cmd on BOTH PCs.
   Stay idle for 5 seconds after FIGHT, when normal combat begins. This mode can load the previous frame
   simulate a P1 rightward press, rewind that branch, and replay confirmed input. Late confirmation discards the pair, with at most two fresh-pair retries.
   native_network_input_delivered=1 and native_replay_verified=1 are required; transport PASS alone is insufficient.
   If native_restart_required=1, close BOTH games and share the report.
   For the save-only checkpoint test, open Steam-Checkpoints.cmd. Leave both players idle for 5 seconds after the round starts. The test attempts two native saves, links them to authenticated inputs, and checks captured-pair restoration readiness. It never loads or replays. Read native_readiness_prepared separately from transport PASS.
   For observation only, open Steam-Inputs.cmd. Enter the OTHER player's Steam friend code or SteamID64, then YOUR assigned Player 1/2 side.
4. Confirm consent. When both scripts are ready, press Enter together.
5. Ready up, select characters and stage, and begin the private match within 10 minutes.
   Recording waits for a NEW round-start event after the script starts, then
   captures 7,200 gameplay frames (about 2 minutes). If already in a match,
   finish the current round so the next starts. Play normally.
   The capture observes the next 7,200 frame boundaries after its Steam handshake.
6. Allow up to 3 additional minutes for capture and delivery verification. Send BOTH reports, plus each
   machine's steam-inputs-PID-local.csv, -remote.csv, -timing.csv, -pipeline.csv, -shadow.csv, -composed.csv, -rounds.csv, -round-events.csv and -round-events-upstream.csv.

NEW BUILD REQUIRED ON BOTH PCS: this uses a new diagnostic handshake version.
The peers announce their assigned slots; choosing the same slot stops the test.
Incoming records are matched to local counters, checked for button agreement,
and composed while retaining this PC's historical auxiliary input values.
composition_staged counts buffered compositions, NOT game updates or rewinds.
composition_failure must be none. Round identity and game handoff remain unverified.
Avoid ending the round during this capture; counter resets stop staging.

PING ~: after the mutual handshake, the overlay displays an estimated RTT.
Every 3 seconds a burst of up to five probes is sent, spaced at least20ms apart.
The lowest valid RTT in the burst is displayed after subtracting the responder's
measured reply-handling duration. A burst closes within500ms; partial bursts are
allowed. Unknown Steam/OS queueing and time waiting for receive polling remain.
This is not pure wire latency or one-way delay. Both PCs need this ping build.
Reports retain the selected raw RTT, subtracted handling time and sample count.
It keeps running after the capture PASS while the selected peer responds.
Missing samples become -- after 9 seconds. No peer response for 15 seconds
after capture stops telemetry. Run Stop-Steam-Ping.cmd or close SC6 to stop.
Both players should stop this diagnostic before leaving private play.
DELAY and ROLLBACK stay -- because rollback is disabled.

No relay program, VPN, firewall prompt, or router forwarding is part of this test.
Do not run the old handshake or Network-Test scripts. Restart BOTH games before
any retry. Only one attempt is supported per game launch.

Received records are handed to the game thread for bounded metadata/input validation.
Steam-Inputs captures no native checkpoints. Steam-Checkpoints saves two and checks their compatibility; neither mode restores or injects inputs.
Round metadata is compared at matching counters. Missing event metadata prevents
staging; differing round numbers or boundary counters stop the diagnostic.
Agreement is evidence only, not a verified shared match lifetime or permission to restore.

PASS proves recorded bytes were delivered and their checksums agreed.
The shadow scheduler simulates 4 frames of delay and repeats held inputs while
clearing newly-pressed bits. It does NOT delay the actual game.
shadow_over_limit counts estimated corrections exceeding 3 frames.
shadow_within_observed_bounds=1 is only this capture's diagnostic result.
The final 4 frames have no scheduling boundary and are excluded. Unseeded
frames, unmatched counters, timing censoring and input mismatches are reported.
Only Steam-Replay invokes experimental native loading and a recorded-input update. Other modes do not. Transport PASS does
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
$zip=Join-Path $project 'build-network\SC6Rollback-Replay-Comparison-v21.zip'
Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $zip -Force
Get-FileHash -LiteralPath $zip


