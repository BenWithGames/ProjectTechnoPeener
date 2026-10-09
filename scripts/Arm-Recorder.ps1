param([Parameter(Mandatory=$true)][switch]$TrainingConfirmed,
      [switch]$NativeAdjacentIdle,
      [switch]$NativeIdleReplay,
      [switch]$NativeTwoIdleReplay,
      [switch]$NativePrediction,
      [switch]$NativeDirection,
      [switch]$NativeDirectionRestore,
      [switch]$NativeTwoDirectionCapture,
      [switch]$NativeTwoMovement,
      [switch]$NativeThreeMovement,
      [switch]$RepeatedMovement,
      [switch]$NativeLeftMovement,
      [switch]$SaveOnly,
      [switch]$IdleTrajectory,
      [switch]$ChargeTrajectory,
      [switch]$OneFrameCharge,
      [switch]$TwoFrameCharge,
      [switch]$TwoFrameChargeInputs,
      [switch]$ThreeFrameChargeInputs,
      [switch]$ThreeFrameAttackInputs,
      [switch]$ThreeFrameEdgeInputs,
      [switch]$ThreeFrameReversalInputs,
      [switch]$ReversalStructure,
      [switch]$AttackStructure,
      [switch]$ThrowTrajectory,
      [switch]$ThrowTrajectoryLong,
      [switch]$TwoFrameIdle,
      [switch]$TwoFrameThrow,
      [switch]$TwoFrameThrowInputs,
      [switch]$ThreeFrameThrowInputs,
      [switch]$DelayAudit,
      [switch]$ThrowAudit,
      [switch]$ThrowReplay,
      [switch]$ThrowReplayLater,
      [switch]$DelayRestore,
      [switch]$SharedRestore,
      [switch]$FullRestore,
      [switch]$PreserveScalars,
      [switch]$ThrowCapture,
      [switch]$SoulChargeCapture,
      [switch]$SoulAttackCapture,
      [switch]$CriticalEdgeCapture,
      [switch]$ReversalEdgeCapture,
      [switch]$ObserveSoulCharge,
      [switch]$ObserveSoulAttack,
      [string]$GameDirectory='E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64')
$ErrorActionPreference='Stop'
if(([int]$NativeIdleReplay.IsPresent+[int]$NativeAdjacentIdle.IsPresent+[int]$NativeTwoIdleReplay.IsPresent+[int]$NativePrediction.IsPresent+[int]$NativeDirection.IsPresent+[int]$NativeDirectionRestore.IsPresent+[int]$NativeTwoDirectionCapture.IsPresent+[int]$NativeTwoMovement.IsPresent+[int]$NativeThreeMovement.IsPresent+[int]$RepeatedMovement.IsPresent+[int]$NativeLeftMovement.IsPresent) -gt 1){throw 'Select one native mode.'}
if($NativeAdjacentIdle -or $NativeIdleReplay -or $NativeTwoIdleReplay -or $NativePrediction -or $NativeDirection -or $NativeDirectionRestore -or $NativeTwoDirectionCapture -or $NativeTwoMovement -or $NativeThreeMovement -or $RepeatedMovement -or $NativeLeftMovement){
 if(!$FullRestore){throw 'NativeAdjacentIdle requires FullRestore.'}
 foreach($key in $PSBoundParameters.Keys){
  if($key -notin @('TrainingConfirmed','FullRestore','NativeAdjacentIdle','NativeIdleReplay','NativeTwoIdleReplay','NativePrediction','NativeDirection','NativeDirectionRestore','NativeTwoDirectionCapture','NativeTwoMovement','NativeThreeMovement','RepeatedMovement','NativeLeftMovement','GameDirectory')){throw 'NativeAdjacentIdle cannot combine with another mode.'}
 }
}

if($ReversalStructure -and (!$IdleTrajectory -or $ThreeFrameReversalInputs -or $ThreeFrameEdgeInputs -or $ThreeFrameAttackInputs -or $AttackStructure -or $ChargeTrajectory -or $OneFrameCharge -or $TwoFrameCharge -or $TwoFrameChargeInputs -or $ThreeFrameChargeInputs)){throw 'ReversalStructure requires IdleTrajectory and FullRestore without other combat modes.'}
if($ThreeFrameReversalInputs -and (!$IdleTrajectory -or $ThreeFrameEdgeInputs -or $ThreeFrameAttackInputs -or $AttackStructure -or $ChargeTrajectory -or $OneFrameCharge -or $TwoFrameCharge -or $TwoFrameChargeInputs -or $ThreeFrameChargeInputs)){throw 'ThreeFrameReversalInputs requires IdleTrajectory and FullRestore without other combat modes.'}
if($ThreeFrameEdgeInputs -and (!$IdleTrajectory -or $ThreeFrameAttackInputs -or $AttackStructure -or $ChargeTrajectory -or $OneFrameCharge -or $TwoFrameCharge -or $TwoFrameChargeInputs -or $ThreeFrameChargeInputs)){throw 'ThreeFrameEdgeInputs requires IdleTrajectory and FullRestore without other combat modes.'}
if($AttackStructure -and (!$IdleTrajectory -or $ThreeFrameAttackInputs -or $ChargeTrajectory -or $OneFrameCharge -or $TwoFrameCharge -or $TwoFrameChargeInputs -or $ThreeFrameChargeInputs)){throw 'AttackStructure requires IdleTrajectory and FullRestore without other combat modes.'}
if($ThreeFrameAttackInputs -and (!$IdleTrajectory -or $ChargeTrajectory -or $OneFrameCharge -or $TwoFrameCharge -or $TwoFrameChargeInputs -or $ThreeFrameChargeInputs)){throw 'ThreeFrameAttackInputs requires IdleTrajectory and FullRestore without charge modes.'}
if($ThreeFrameChargeInputs -and !$TwoFrameChargeInputs){throw 'ThreeFrameChargeInputs requires TwoFrameChargeInputs and TwoFrameCharge.'}
if($TwoFrameChargeInputs -and !$TwoFrameCharge){throw 'TwoFrameChargeInputs requires TwoFrameCharge.'}
if($TwoFrameCharge -and (!$IdleTrajectory -or $ChargeTrajectory -or $OneFrameCharge)){throw 'TwoFrameCharge requires IdleTrajectory and FullRestore, without other charge modes.'}
if($OneFrameCharge -and (!$IdleTrajectory -or $ChargeTrajectory)){throw 'OneFrameCharge requires IdleTrajectory and FullRestore, without ChargeTrajectory.'}
if($ChargeTrajectory -and !$IdleTrajectory){throw 'ChargeTrajectory requires IdleTrajectory and FullRestore transport.'}
if($ThreeFrameThrowInputs -and !$TwoFrameThrowInputs){throw 'ThreeFrameThrowInputs requires TwoFrameThrowInputs and TwoFrameThrow.'}
if($TwoFrameThrowInputs -and !$TwoFrameThrow){throw 'TwoFrameThrowInputs requires TwoFrameThrow.'}
if($ThrowTrajectoryLong -and !$ThrowTrajectory){throw 'ThrowTrajectoryLong requires ThrowTrajectory.'}
if(([int]$TwoFrameThrow.IsPresent+[int]$TwoFrameIdle.IsPresent+[int]$IdleTrajectory.IsPresent+[int]$ThrowTrajectory.IsPresent) -gt 1){throw 'Select either reference capture or two-frame replay.'}
if(($TwoFrameThrow -or $ThrowTrajectory -or $IdleTrajectory -or $TwoFrameIdle) -and (!$FullRestore -or $SaveOnly -or $SharedRestore -or $DelayAudit -or $ThrowAudit -or $ThrowReplay -or $ThrowReplayLater -or $DelayRestore -or $PreserveScalars -or $ThrowCapture -or $SoulChargeCapture -or $SoulAttackCapture -or $CriticalEdgeCapture -or $ReversalEdgeCapture -or $ObserveSoulCharge -or $ObserveSoulAttack)){throw 'IdleTrajectory requires FullRestore transport only, with no other modes.'}
if(!$TrainingConfirmed){throw 'Offline Training confirmation is required.'}
if(([int]$SaveOnly.IsPresent+[int]$SharedRestore.IsPresent+[int]$FullRestore.IsPresent) -gt 1){throw 'Select one probe mode.'}
if($PreserveScalars -and !$FullRestore){throw 'PreserveScalars requires FullRestore.'}
if($ThrowCapture -and !($FullRestore -and $PreserveScalars)){throw 'ThrowCapture requires FullRestore and PreserveScalars.'}
if($SoulChargeCapture -and (!($FullRestore -and $PreserveScalars) -or $ThrowCapture)){throw 'SoulChargeCapture requires FullRestore, PreserveScalars, and no ThrowCapture.'}
if($SoulAttackCapture -and (!($FullRestore -and $PreserveScalars) -or $ThrowCapture -or $SoulChargeCapture)){throw 'SoulAttackCapture requires FullRestore, PreserveScalars, and no other combat capture.'}
if($CriticalEdgeCapture -and (!($FullRestore -and $PreserveScalars) -or $ThrowCapture -or $SoulChargeCapture -or $SoulAttackCapture)){throw 'CriticalEdgeCapture requires FullRestore, PreserveScalars, and no other combat capture.'}
if($ReversalEdgeCapture -and (!($FullRestore -and $PreserveScalars) -or $ThrowCapture -or $SoulChargeCapture -or $SoulAttackCapture -or $CriticalEdgeCapture)){throw 'ReversalEdgeCapture requires FullRestore, PreserveScalars, and no other combat capture.'}
if($ObserveSoulCharge -and ($SaveOnly -or $SharedRestore -or $FullRestore -or $PreserveScalars -or $ThrowCapture -or $SoulChargeCapture)){throw 'ObserveSoulCharge cannot combine with save/restore modes.'}
if($ObserveSoulAttack -and ($ObserveSoulCharge -or $SaveOnly -or $SharedRestore -or $FullRestore -or $PreserveScalars -or $ThrowCapture -or $SoulChargeCapture)){throw 'ObserveSoulAttack cannot combine with another capture mode.'}
if($DelayAudit -and (!$FullRestore -or $PreserveScalars -or $ThrowCapture -or $SoulChargeCapture -or $SoulAttackCapture -or $CriticalEdgeCapture -or $ReversalEdgeCapture)){throw 'DelayAudit requires FullRestore as transport only, with no preservation or combat modes.'}
if($DelayRestore -and (!$FullRestore -or $DelayAudit -or $PreserveScalars -or $ThrowCapture -or $SoulChargeCapture -or $SoulAttackCapture -or $CriticalEdgeCapture -or $ReversalEdgeCapture)){throw 'DelayRestore requires FullRestore only, with no other modes.'}
if($ThrowAudit -and (!$FullRestore -or $DelayAudit -or $DelayRestore -or $PreserveScalars -or $ThrowCapture -or $SoulChargeCapture -or $SoulAttackCapture -or $CriticalEdgeCapture -or $ReversalEdgeCapture -or $ObserveSoulCharge -or $ObserveSoulAttack)){throw 'ThrowAudit requires FullRestore transport only, with no other modes.'}
if($ThrowReplay -and (!$FullRestore -or $ThrowAudit -or $DelayAudit -or $DelayRestore -or $PreserveScalars -or $ThrowCapture -or $SoulChargeCapture -or $SoulAttackCapture -or $CriticalEdgeCapture -or $ReversalEdgeCapture -or $ObserveSoulCharge -or $ObserveSoulAttack)){throw 'ThrowReplay requires FullRestore only.'}
if($ThrowReplayLater -and !$ThrowReplay){throw 'ThrowReplayLater requires ThrowReplay.'}
$game=Get-Process SoulcaliburVI
if(@($game).Count -ne 1){throw 'Expected one SC6 process.'}
$exe=Join-Path $GameDirectory 'SoulcaliburVI.exe'
if($game.Path -ne $exe){throw 'Process path differs from specified game directory.'}
if((Get-FileHash -LiteralPath $exe).Hash -ne 'F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553'){throw 'Unsupported game executable.'}
$root=Join-Path $GameDirectory 'Mods\SC6Rollback'
$staged=Join-Path (Split-Path $PSScriptRoot -Parent) 'package\Mods\SC6Rollback\dlls\main.dll'
if((Get-FileHash "$root\dlls\main.dll").Hash -ne (Get-FileHash $staged).Hash){throw 'Installed recorder DLL does not match the staged build.'}
$dir=Join-Path $root 'captures'
if(!(Test-Path "$dir\status.txt")){throw 'Recorder has not initialized. Check UE4SS.log.'}
if(Get-ChildItem -LiteralPath $dir -Filter '*.arm'){throw 'An arm request is already pending.'}
$arm=Join-Path $dir $(if($FullRestore){'offline-full-restore.arm'}elseif($SharedRestore){'offline-shared-restore.arm'}elseif($SaveOnly){'offline-save.arm'}else{'offline-training.arm'})
$stream=[System.IO.File]::Open($arm,[System.IO.FileMode]::CreateNew,[System.IO.FileAccess]::Write,[System.IO.FileShare]::None)
try {
 $token=if($NativeLeftMovement){'OFFLINE_NATIVE_LEFT_MOVEMENT'}elseif($RepeatedMovement){'OFFLINE_REPEATED_MOVEMENT'}elseif($NativeThreeMovement){'OFFLINE_NATIVE_THREE_MOVEMENT'}elseif($NativeTwoMovement){'OFFLINE_NATIVE_TWO_MOVEMENT'}elseif($NativeTwoDirectionCapture){'OFFLINE_NATIVE_TWO_DIRECTION_CAPTURE'}elseif($NativeDirectionRestore){'OFFLINE_NATIVE_DIRECTION_RESTORE'}elseif($NativeDirection){'OFFLINE_NATIVE_DIRECTION'}elseif($NativePrediction){'OFFLINE_NATIVE_PREDICTION'}elseif($NativeTwoIdleReplay){'OFFLINE_NATIVE_TWO_IDLE_REPLAY'}elseif($NativeIdleReplay){'OFFLINE_NATIVE_IDLE_REPLAY'}elseif($NativeAdjacentIdle){'OFFLINE_NATIVE_ADJACENT_IDLE'}elseif($ReversalStructure){'OFFLINE_REVERSAL_STRUCTURE'}elseif($ThreeFrameReversalInputs){'OFFLINE_THREE_FRAME_REVERSAL_INPUTS'}elseif($ThreeFrameEdgeInputs){'OFFLINE_THREE_FRAME_EDGE_INPUTS'}elseif($AttackStructure){'OFFLINE_ATTACK_STRUCTURE'}elseif($ThreeFrameAttackInputs){'OFFLINE_THREE_FRAME_ATTACK_INPUTS'}elseif($ThreeFrameChargeInputs){'OFFLINE_THREE_FRAME_CHARGE_INPUTS'}elseif($TwoFrameChargeInputs){'OFFLINE_TWO_FRAME_CHARGE_INPUTS'}elseif($TwoFrameCharge){'OFFLINE_TWO_FRAME_CHARGE'}elseif($OneFrameCharge){'OFFLINE_ONE_FRAME_CHARGE'}elseif($ChargeTrajectory){'OFFLINE_CHARGE_TRAJECTORY'}elseif($ThreeFrameThrowInputs){'OFFLINE_THREE_FRAME_THROW_INPUTS'}elseif($TwoFrameThrowInputs){'OFFLINE_TWO_FRAME_THROW_INPUTS'}elseif($ThrowTrajectoryLong){'OFFLINE_THROW_TRAJECTORY_LONG'}elseif($TwoFrameThrow){'OFFLINE_TWO_FRAME_THROW'}elseif($ThrowTrajectory){'OFFLINE_THROW_TRAJECTORY'}elseif($TwoFrameIdle){'OFFLINE_TWO_FRAME_IDLE'}elseif($IdleTrajectory){'OFFLINE_IDLE_TRAJECTORY'}elseif($ThrowReplayLater){'OFFLINE_THROW_REPLAY_LATER'}elseif($ThrowReplay){'OFFLINE_THROW_REPLAY'}elseif($ThrowAudit){'OFFLINE_THROW_AUDIT'}elseif($DelayRestore){'OFFLINE_DELAY_RESTORE'}elseif($DelayAudit){'OFFLINE_DELAY_AUDIT'}elseif($ReversalEdgeCapture){'OFFLINE_REVERSAL_PRESERVE'}elseif($CriticalEdgeCapture){'OFFLINE_EDGE_PRESERVE'}elseif($SoulAttackCapture){'OFFLINE_ATTACK_PRESERVE'}elseif($ObserveSoulAttack){'OFFLINE_ATTACK_OBSERVE'}elseif($ObserveSoulCharge){'OFFLINE_CHARGE_OBSERVE'}elseif($SoulChargeCapture){'OFFLINE_CHARGE_PRESERVE'}elseif($ThrowCapture){'OFFLINE_THROW_PRESERVE'}elseif($PreserveScalars){'OFFLINE_FULL_PRESERVE'}elseif($FullRestore){'OFFLINE_FULL_RESTORE'}elseif($SharedRestore){'OFFLINE_SHARED_RESTORE'}elseif($SaveOnly){'OFFLINE_SAVE'}else{'OFFLINE_TRAINING'}
 $bytes=[System.Text.Encoding]::ASCII.GetBytes("$token $($game.Id)`n")
 $stream.Write($bytes,0,$bytes.Length)
} finally {$stream.Dispose()}
if($NativeLeftMovement){Write-Host 'Armed: three-frame screen-left prediction and neutral correction. Keep controls untouched.'}elseif($RepeatedMovement){Write-Host 'Armed: three correction cycles with three predicted movement frames each. Keep controls untouched.'}elseif($NativeThreeMovement){Write-Host 'Armed: three-frame movement prediction, guarded rewind and neutral replay. Keep controls untouched.'}elseif($NativeTwoMovement){Write-Host 'Armed: two-frame movement prediction, guarded rewind and neutral replay. Keep controls untouched.'}elseif($NativeTwoDirectionCapture){Write-Host 'Armed: two predicted right-press frames; capture only after the initial idle restore. Keep controls untouched.'}elseif($NativeDirectionRestore){Write-Host 'Armed: screen-right prediction, guarded movement restore, neutral replay. Keep controls untouched.'}elseif($NativeDirection){Write-Host 'Armed: one screen-right update then movement-state save only; no movement restoration. Keep controls untouched.'}elseif($NativePrediction){Write-Host 'Armed: one auxiliary-float prediction error and correction. Leave controls untouched.'}elseif($NativeTwoIdleReplay){Write-Host 'Armed: two-frame native idle restore/replay. Leave controls untouched.'}elseif($NativeIdleReplay){Write-Host 'Armed: native idle restore then one recorded-input natural replay update. Keep controls untouched.'}elseif($NativeAdjacentIdle){Write-Host 'Armed: one-frame idle restore from live current state. Leave controls untouched; restart after any failure.'}elseif($ReversalStructure){Write-Host 'Armed: connect Reversal Edge, enter clash, release controls. Four reference saves only; no restoration.'}elseif($ThreeFrameReversalInputs){Write-Host 'Armed: connect Reversal Edge and enter clash, then release controls; three-update replay. Restart after failure.'}elseif($ThreeFrameEdgeInputs){Write-Host 'Armed: land Critical Edge, release controls; guarded three-update replay. Restart after failure.'}elseif($AttackStructure){Write-Host 'Armed: land Soul Attack, release controls. Four reference saves only; no restoration.'}elseif($ThreeFrameAttackInputs){Write-Host 'Armed: land Soul Attack within45seconds, then release controls. Guarded three-update replay; restart after failure.'}elseif($ThreeFrameChargeInputs){Write-Host 'Armed: activate Soul Charge, release controls; guarded three-update recorded-input replay. Restart after any failure.'}elseif($TwoFrameCharge){Write-Host 'Armed: activate Soul Charge, release controls; guarded two-update replay. Restart after any failure.'}elseif($OneFrameCharge){Write-Host 'Armed: activate Soul Charge, release controls; guarded one-update replay. Restart after any failure.'}elseif($ChargeTrajectory){Write-Host 'Armed: activate Soul Charge within 45 seconds, then release controls. Four reference saves only; no restoration.'}elseif($ThreeFrameThrowInputs){Write-Host 'Armed: connect A+G within 45 seconds for three-update recorded-input replay. Release controls; no movement, pause, or reset. Restart after failure.'}elseif($ThrowTrajectoryLong){Write-Host 'Armed: connect A+G within 45 seconds; four reference saves and three pair checks, no restore. Release controls; no movement, pause, or reset.'}elseif($TwoFrameThrow){Write-Host 'Armed: connect A+G within 45 seconds for guarded two-update rewind/replay. Release controls; no movement, pause, or reset. Restart after any failure.'}elseif($ThrowTrajectory){Write-Host 'Armed: connect A+G within 45 seconds; three reference saves, no restore. Release controls; do not move, pause, or reset.'}elseif($TwoFrameIdle){Write-Host 'Armed: experimental two-update idle rewind/replay via two adjacent restores. Leave controls untouched; restart SC6 after any failure.'}elseif($IdleTrajectory){Write-Host 'Armed: three consecutive idle saves across two updates, no restore. Leave controls untouched.'}elseif($ThrowReplay){Write-Host "Armed: connect A+G within 45 seconds for guarded one-frame rewind/replay. Release controls; do not move, pause or reset. Restart SC6 after any failure."}elseif($ThrowAudit){Write-Host "Armed: connect Yoshimitsu A+G within 45 seconds; two adjacent saves, no restore, then 600 updates."}elseif($DelayRestore){Write-Host "Armed: experimental one-frame idle restore. Leave controls untouched. Captures: $dir"}elseif($DelayAudit){Write-Host "Armed: two adjacent idle saves, no restore. Leave controls untouched. Captures: $dir"}elseif($ReversalEdgeCapture){Write-Host "Armed: connect Reversal Edge and enter its clash within 45 seconds, then 600 updates. Captures: $dir"}elseif($CriticalEdgeCapture){Write-Host "Armed: activate Critical Edge within 45 seconds, then 600 updates. Startup trigger is not proof of a hit. Captures: $dir"}elseif($SoulAttackCapture){Write-Host "Armed: activate Soul Attack within 45 seconds, then 600 updates. Startup trigger is not proof of a hit. Captures: $dir"}elseif($ObserveSoulAttack){Write-Host "Observation only: activate Soul Attack within 45 seconds, then 600 updates. No save/load. Captures: $dir"}elseif($ObserveSoulCharge){Write-Host "Observation only: activate Soul Charge within 45 seconds, then 600 updates. No save/load. Captures: $dir"}elseif($SoulChargeCapture){Write-Host "Armed: up to 45 seconds to activate Soul Charge, then 600 updates. Captures: $dir"}elseif($ThrowCapture){Write-Host "Armed: up to 45 seconds to connect A+G, then 600 updates. Captures: $dir"}else{Write-Host "Armed for 600 updates or 20 seconds. Stay in offline Training. Captures: $dir"}
if($SaveOnly){Write-Host 'One save attempt on the first valid entry. No battle-state restore. Inspect the -save.txt report.'}

if($SharedRestore){Write-Host 'One immediate shared-state save/load per process. Stay idle. Inspect the -restore.txt report.'}

if($FullRestore -and !$NativeLeftMovement -and !$RepeatedMovement -and !$NativeThreeMovement -and !$NativeTwoMovement -and !$NativeTwoDirectionCapture -and !$NativeDirectionRestore -and !$NativeDirection -and !$NativePrediction -and !$NativeTwoIdleReplay -and !$NativeIdleReplay -and !$NativeAdjacentIdle -and !$TwoFrameThrow -and !$ThrowTrajectory -and !$TwoFrameIdle -and !$IdleTrajectory -and !$ThrowReplay -and !$ThrowAudit -and !$DelayRestore -and !$DelayAudit -and !$ThrowCapture -and !$SoulChargeCapture -and !$SoulAttackCapture -and !$CriticalEdgeCapture -and !$ReversalEdgeCapture){Write-Host 'One immediate full-serializer save/load per process. Stay idle. Inspect -restore.txt; restart SC6 if the probe fails.'}

if($PreserveScalars){Write-Host 'Experimental preservation enabled: measured scalars and RNG only; rebuilt pointers retained.'}
