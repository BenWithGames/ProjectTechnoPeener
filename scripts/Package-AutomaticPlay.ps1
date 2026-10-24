param([string]$OutputDirectory=(Join-Path $env:USERPROFILE 'Downloads'),[string]$InstallGameFolder,[switch]$UpdateOnly,[switch]$IncludeResearchNotes)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$name='SC6Rollback-'+$(if($UpdateOnly){'Update'}else{'AutomaticPlay-Alpha'})+'-'+$stamp
$folder=Join-Path $project ('build-network\'+$name)
$payload=Join-Path $folder 'observer-mod'
New-Item -ItemType Directory -Path $payload -Force | Out-Null
foreach($file in @('dwmapi.dll','UE4SS.dll','UE4SS-settings.ini','UE4SS-LICENSE.txt','MINHOOK-LICENSE.txt','main.dll')){
 $source=if($file -eq 'main.dll'){Join-Path $project 'package\Mods\SC6Rollback\dlls\main.dll'}else{Join-Path $project ('package\'+$file)}
 if($UpdateOnly -and $file -in @('dwmapi.dll','UE4SS.dll')){continue}
 Copy-Item -LiteralPath $source -Destination (Join-Path $payload $file)
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Archive-AutomaticReport.ps1') -Destination $payload
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'SC6Astral-Updater.ps1') -Destination $payload
$updateVersion=Join-Path $project 'package\Mods\SC6Rollback\UpdateVersion.json'
$version=Get-Content -LiteralPath $updateVersion -Raw | ConvertFrom-Json
if($version.dllSHA256 -ne (Get-FileHash -LiteralPath (Join-Path $payload 'main.dll')).Hash){throw 'Update version does not match staged DLL'}
Copy-Item -LiteralPath $updateVersion -Destination $payload
Copy-Item -LiteralPath (Join-Path $project 'assets\connection.mp3') -Destination (Join-Path $payload 'connection.mp3')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Install-AutomaticPlay.ps1') -Destination (Join-Path $folder 'Install.ps1')
@('@echo off','powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" -ChoosePath %*','echo.','pause') | Set-Content (Join-Path $folder 'Install.cmd') -Encoding ascii
@'
SC6Rollback Automatic Play - experimental alpha

Close SC6, extract this ZIP, and run Install.cmd once. Launch through Steam and play.
Signed updates: the overlay shows the installed build and a Check for updates button. The helper checks GitHub Pages on startup and every 15 minutes, verifies the publisher signature, and stages newer updates from the private R2 Worker. Verified updates install only after SC6 exits. Settings and match logs are never update targets. Backups and local updater diagnostics are in Mods\SC6Rollback\updates. If the updater cannot write to the install directory, use the manual installer; it never silently elevates. Publishing update.json and its matching ZIP is required before checks can succeed.
Both players must install this build to use automatic rollback. There is no test-suite script, friend-code prompt, side selection, warmup, or consent prompt. Steam's existing connection identifies the peer; the native clock identifies the local side. Ambiguous peers do not activate rollback.

Reports are saved automatically inside the game's Win64 folder:
Mods\SC6Rollback\logs\pass\match-<shared-match-id>-<process>-<time>.zip (clean matches)
Mods\SC6Rollback\logs\fails\match-<shared-match-id>-<process>-<time>.zip (failures)
The shared match ID is identical on both PCs. Pre-handshake failures use unpaired.
Send the matching ZIP after a problem. Nothing is uploaded automatically.
Each of pass and fails keeps its newest 10 completed ZIP reports, deleting older reports automatically.
Clean completed matches rearm for a new match in the same process. A failed restoration remains fail-closed: restart SC6 after a failure. This build does not promise recovery from corrupted or unverified state.

This build uses adaptive 0 to 2 frame input delay and a maximum of 3 prediction frames. Inputs are sent immediately. Both peers agree on delay before each round; it never changes during combat. Missing measurements use 2 frames. Both PCs must use this build.
Delay uses the higher raw 80th-percentile RTT from the latest two complete five-sample bursts: below 25 ms = 0f, 25-70 ms inclusive = 1f, above 70 ms = 2f. Two supporting bursts allow a drop at the next round agreement; one isolated outlier per burst is tolerated. This can differ from the minimum ping shown on the overlay.

Hooks and first-fight intent are prepared during loading/intros when both PCs are ready in time. The game-thread check still waits for safe fighting before activation. Late preparation uses the agreed-frame fallback; reports distinguish these paths.

First-match startup and fallback use a synchronized held-boundary release. Both players must update together. This does not add input-delay frames during combat. Reports include planned and actual release times for that path.
Fresh connection timing is retained between rounds to reduce repeated measurement waiting. Every round still requires fresh peer agreement and native safety checks; stale timing falls back to collecting new samples.
Next-round preparation now reserves the exact upcoming fighting frame during verified Start state. Both peers exchange and acknowledge that reservation while the countdown continues. Activation still checks the actual fighting state, clock, round identity and owned resources and binds a local hold. When early agreement is complete it does not add another timed handshake at Fight. Short or unsuitable transitions retain the held-boundary agreement. A changed advertised boundary fails closed; it cannot silently activate at another frame. Live pause reduction still needs two-PC measurement.

All exact verification, low-latency prediction, diagnostics and round safety checks remain enabled. Added automatic startup and clean-match retirement need real two-PC validation, including rematches, lobby returns, switching sides, switching opponents, and disconnects. Treat this as an alpha, not a stable release.

If automatic Steam-library discovery finds multiple installs, run:
Install.cmd -GameFolder "D:\SteamLibrary\steamapps\common\SoulcaliburVI"

To disable automatic startup, remove Mods\SC6Rollback\automatic-play.enabled while SC6 is closed. To disable the whole mod, set SC6Rollback : 0 in Mods\mods.txt.
'@ | Set-Content (Join-Path $folder 'README.txt')
if($IncludeResearchNotes){
Copy-Item -LiteralPath (Join-Path $project 'docs\OVERLAY_PING_WORKER.md') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\CONFIRMATION_PHASE_AUDIT.md') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\INTRO_STARTUP.md') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\CORRECTION_ENDPOINT_COPY.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\CAPTURE_CAMERA_INDEX.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\AUTOMATIC_OWNER_STARTUP.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\CRITICAL_EDGE_REFERENCE_LAYOUT.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\TWO_FRAME_INPUT_DELAY.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\RETIREMENT_BRANCH_RETRY.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\MATCH_END_PRISTINE_STORAGE.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\ADAPTIVE_INPUT_DELAY.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\REMATCH_HANDOFF_CLEANUP.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\STARTUP_POOL_REUSE.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\STARTUP_WAIT_REUSE.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\ROUND_TRANSITION_PREPARATION.md') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\ROUND_TRANSITION_RESERVATION.md') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\START_COMMIT_DELIVERY.md') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\CORRECTION_IMBALANCE.md') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\CONFIRMATION_PIPELINE.md') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\TIMING_IMBALANCE_RECOVERY.md') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\MISSED_UPDATE_RECOVERY.md') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\ADMISSION_EXECUTION_GAP.md') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\MATCH_LOADING_GATE_AUDIT.md') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\POSTLOAD_READINESS_DIAGNOSTICS.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\SWAY_BRANCH_STATE.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\BLEND_BLOCK5_SCALAR.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\INTERMEDIATE_CAPTURE_REUSE.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\INACTIVE_TABLE_BOTH_SIDES.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\BLOCK5_SELECTED_TABLE.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\ANIMATION_CONTROLLER_STATE.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\CORRECTION_WORK_REUSE.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\COMPACT_ROLLING_SNAPSHOTS.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\CORRECTION_WORKSPACE_REUSE.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\ONE_FRAME_DELAY_TEST.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\CAMERA_ALTERNATE_STATE.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\INPUT_PATH_TIMING.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\NULL_BLEND_SCALAR_RESET.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\FAST_SENDER_RESTART.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\LATE_CONFIRMATION_ADMISSION.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\COMPLETED_UPDATE_CONFIRMATIONS.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\OWNERSHIP_CAPTURE_REUSE.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\PREDICTION_NATIVE_PAUSE.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\NULL_ACTIVE_BLEND_RESTORE.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\SNAPSHOT_PREFLIGHT_DIAGNOSTICS.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\SNAPSHOT_PREFLIGHT_SOURCE_MAP.txt') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\ANIMATION_LIST_TRAVERSAL.md') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\SWAY_POSITION_RESTORE.md') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\SWAY_D78_RESTORE.md') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\SWAY_CONTROL_RESTORE.md') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\CALLBACK_ATTACHMENT_PREFLIGHT.md') -Destination $folder
Copy-Item -LiteralPath (Join-Path $project 'docs\CAMERA_REFERENCE_OWNERSHIP.md') -Destination $folder
}
if($UpdateOnly){
 $requirements=@{schema=1;dwmapi=(Get-FileHash (Join-Path $project 'package\dwmapi.dll')).Hash;ue4ss=(Get-FileHash (Join-Path $project 'package\UE4SS.dll')).Hash}
 $requirements | ConvertTo-Json | Set-Content (Join-Path $folder 'RuntimeRequirements.json')
 Add-Content (Join-Path $folder 'README.txt') '
UPDATE PACKAGE: requires an existing matching runtime. Installer verifies it before changing files. Use the full package for a first installation.'
}
$dllHash=(Get-FileHash (Join-Path $payload 'main.dll')).Hash
@{build=$name;updateOnly=[bool]$UpdateOnly;researchNotes=[bool]$IncludeResearchNotes;dllSHA256=$dllHash;signedUpdates=$true;updaterBuild=$version.build;automaticPlay=$true;swayPositionRestore=$true;swayD78StateRestore=$true;swayOwnerControlRestore=$true;callbackParticleAttachmentPreflight=$true;cameraPlayerReferenceOwnership=$true;cameraReferencePostloadVerification=$true;cameraResolvedPlayerReference=$true;activeSelectedAnimationTransition=$true;selectedAnimationCompletionRestore=$true;postSettlementFightRecheck=$true;blendCompletionTimerDiagnostics=$true;boundedEffectAllocationRetry=$true;effectBeginDiagnostics=$true;perCaptureTraversalIndexes=$true;perRestoreDependencyIndexes=$true;animationListCycleGuard=$true;animationListTraversalBudget=4096;immediateStartControlCopies=$true;predictionLimitReceiptGraceUs=0;nonblockingMissedUpdateRecovery=$true;predictionBudgetPauseRelease=$true;fastInputRedundancyFrames=2;roundTransitionPreparation=$true;roundTransitionReservation=$true;retainedConnectionTiming=$true;loadingSynchronization=$true;loadingGateEnabled=$true;finalBattleAgreementRetained=$true;finalBattleValidationRetained=$true;lowLatency=$true;adaptiveInputDelay=$true;inputDelayMinFrames=0;inputDelayMaxFrames=2;inputDelayFallbackFrames=2;predictionLimitFrames=3;logs='Mods/SC6Rollback/logs';liveValidated=$false} | ConvertTo-Json | Set-Content (Join-Path $folder 'Build.json')
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=Join-Path $OutputDirectory ($name+'.zip')
[IO.Compression.ZipFile]::CreateFromDirectory($folder,$zip)
if($InstallGameFolder){& (Join-Path $folder 'Install.ps1') -GameFolder $InstallGameFolder;if(-not $?){throw 'Installation failed'}}
[pscustomobject]@{Folder=$folder;Zip=$zip;DllSHA256=$dllHash;ZipSHA256=(Get-FileHash $zip).Hash}
