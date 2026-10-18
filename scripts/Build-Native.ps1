param([string]$DependencyRoot)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
if (!$DependencyRoot) { $DependencyRoot = Join-Path (Split-Path (Split-Path $project)) 'work' }
$DependencyRoot = (Resolve-Path -LiteralPath $DependencyRoot).Path
$source = Join-Path $DependencyRoot 'RE-UE4SS'
$runtime = Join-Path $DependencyRoot 'ue4ss-dev\UE4SS.dll'
if ((Get-FileHash -LiteralPath $runtime).Hash -ne '8AC18FBFFC1EF96B0662D4A2D537B3F224C26D65CAABA7989A9404C566102B26') {
    throw 'This build targets the verified UE4SS v3.0.1 runtime hash only.'
}
$revision = & git -c "safe.directory=$($source.Replace('\','/'))" -C $source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $revision -ne 'd935b5b23bac03b65c14ae38382b02007204cc2e') {
    throw 'Expected the pinned v3.0.1 UE4SS source checkout.'
}
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC x64 tools not found.' }
& (Join-Path $PSScriptRoot 'Test-DelayedInput.ps1')
& (Join-Path $PSScriptRoot 'Test-OfflineRollback.ps1')
& (Join-Path $PSScriptRoot 'Test-BoundedRollback.ps1')
& (Join-Path $PSScriptRoot 'Test-OuterStackContinuation.ps1')
& (Join-Path $PSScriptRoot 'Test-OuterStackContinuation.ps1') -Optimized
& (Join-Path $PSScriptRoot 'Test-CombatUpdateAdapter.ps1')
& (Join-Path $PSScriptRoot 'Test-NativeClockAccess.ps1')
& (Join-Path $PSScriptRoot 'Test-NativePredictionFrontier.ps1')
& (Join-Path $PSScriptRoot 'Test-NativeInputWords.ps1')
& (Join-Path $PSScriptRoot 'Test-EffectLifetime.ps1')
& (Join-Path $PSScriptRoot 'Test-MatchDataset.ps1')
& (Join-Path $PSScriptRoot 'Test-EffectObserver.ps1')
& (Join-Path $PSScriptRoot 'Test-NativeSnapshotPool.ps1')
& (Join-Path $PSScriptRoot 'Test-NativeRestoreReuse.ps1')
& (Join-Path $PSScriptRoot 'Test-NativeRollbackWindow.ps1')
& (Join-Path $PSScriptRoot 'Test-NativeSnapshotReader.ps1')
& (Join-Path $PSScriptRoot 'Test-Recorder.ps1')
& (Join-Path $PSScriptRoot 'Test-Recorder.ps1') -Optimized
& (Join-Path $PSScriptRoot 'Test-SaveStream.ps1')
& (Join-Path $PSScriptRoot 'Test-SharedRestore.ps1')
& (Join-Path $PSScriptRoot 'Test-FullRestore.ps1')
& (Join-Path $PSScriptRoot 'Test-SteamObserver.ps1')
& (Join-Path $PSScriptRoot 'Test-AutomaticSteam.ps1')
& (Join-Path $PSScriptRoot 'Test-SteamHandshake.ps1')
& (Join-Path $PSScriptRoot 'Test-SteamInputExchange.ps1')
& (Join-Path $PSScriptRoot 'Test-SteamPing.ps1')
& (Join-Path $PSScriptRoot 'Test-SingleUpdateCall.ps1')
& (Join-Path $PSScriptRoot 'Test-RngCallerBridge.ps1')
& (Join-Path $PSScriptRoot 'Test-CameraCalc.ps1')
& (Join-Path $PSScriptRoot 'Test-SelectorSnapshot.ps1')
& (Join-Path $PSScriptRoot 'Test-ActiveAnimationGroup.ps1')
& (Join-Path $PSScriptRoot 'Test-RestorePerformance.ps1')
& (Join-Path $PSScriptRoot 'Test-EffectClock.ps1')
& (Join-Path $PSScriptRoot 'Test-EffectBirthJournal.ps1')
& (Join-Path $PSScriptRoot 'Test-CaptureStorage.ps1')
& (Join-Path $PSScriptRoot 'Test-SharedRngBridge.ps1')
& (Join-Path $PSScriptRoot 'Test-InlineRng.ps1')
& (Join-Path $PSScriptRoot 'Test-TimerLookup.ps1')
& (Join-Path $PSScriptRoot 'Test-ScopedResourceClear.ps1')
& (Join-Path $PSScriptRoot 'Test-AnimationCopy.ps1')
& (Join-Path $PSScriptRoot 'Test-LiveShadowPrediction.ps1')
& (Join-Path $PSScriptRoot 'Test-NetworkInputComposer.ps1')
& (Join-Path $PSScriptRoot 'Test-LiveInputComposition.ps1')
& (Join-Path $PSScriptRoot 'Test-NetworkGameHandoff.ps1')
& (Join-Path $PSScriptRoot 'Test-RoundBoundaryIdentity.ps1')
$build = Join-Path $project 'build-native'
$dllDir = Join-Path $project 'package\Mods\SC6Rollback\dlls'
New-Item -ItemType Directory -Force -Path $build,$dllDir | Out-Null
$includes = @((Join-Path $source 'UE4SS\include'), (Join-Path $project 'third_party\minhook\include'))
$includes += Get-ChildItem (Join-Path $source 'deps\first') -Directory | ForEach-Object { Join-Path $_.FullName 'include' } | Where-Object { Test-Path $_ }
$includes += @('generated_include','include\Unreal','include\Unreal\Core') | ForEach-Object { Join-Path $source "deps\first\Unreal\$_" }
foreach ($dependency in @('imgui','textedit')) {
    $folder = Get-ChildItem (Join-Path $DependencyRoot $dependency) -Directory | Select-Object -First 1
    if (!$folder) { throw "Missing downloaded dependency: $dependency" }
    $includes += $folder.FullName
}
$options = @('/O2','/Ob1','/fp:precise','/nologo','/std:c++latest','/EHsc','/MD','/c','/DUNICODE','/D_UNICODE','/DUE_GAME','/DUE_BUILD_SHIPPING','/DPLATFORM_WINDOWS','/DPLATFORM_MICROSOFT','/DOVERRIDE_PLATFORM_HEADER_NAME=Windows','/DUBT_COMPILED_PLATFORM=Win64')
$options += $includes | ForEach-Object { '/I"' + $_ + '"' }
$options += '"' + (Join-Path $project 'src\Mod.cpp') + '"'
$options += '/Fo:"' + (Join-Path $build 'Mod.obj') + '"'
Set-Content (Join-Path $build 'mod.rsp') $options -Encoding ascii
function Invoke-DeveloperCommands([string[]]$Commands) {
    $lines = @('@echo off',('call "' + $vs + '\VC\Auxiliary\Build\vcvars64.bat" >nul'),'if errorlevel 1 exit /b 1')
    foreach ($command in $Commands) { $lines += $command; $lines += 'if errorlevel 1 exit /b 1' }
    $batch = Join-Path $build 'build-step.cmd'
    Set-Content $batch $lines -Encoding ascii
    & $batch
    if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
}
Invoke-DeveloperCommands @("cl @`"$build\mod.rsp`"", "cl /nologo /O2 /Ob1 /fp:precise /std:c++20 /EHsc /MD /W4 /WX /I`"$project\third_party\minhook\include`" /c `"$project\src\FrameRecorder.cpp`" /Fo:`"$build\FrameRecorder.obj`"", "cl /nologo /O2 /Ob1 /fp:precise /std:c++20 /EHsc /MD /W4 /WX /c `"$project\src\StatusOverlay.cpp`" /Fo:`"$build\StatusOverlay.obj`"", "dumpbin /exports `"$runtime`" > `"$build\exports.txt`"")
$exports = Get-Content "$build\exports.txt" | ForEach-Object {
    if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)') { $Matches[1] }
}
if (!$exports) { throw 'No runtime exports found.' }
@('LIBRARY UE4SS.dll','EXPORTS') + $exports | Set-Content "$build\UE4SS.def" -Encoding ascii
Invoke-DeveloperCommands @(
    "lib /nologo /def:`"$build\UE4SS.def`" /out:`"$build\UE4SS.lib`" /machine:x64",
    "link /nologo /dll /out:`"$dllDir\main.dll`" /implib:`"$build\main.lib`" `"$build\Mod.obj`" `"$build\FrameRecorder.obj`" `"$build\StatusOverlay.obj`" `"$project\build-rng-bridge\RngCallerBridge.obj`" `"$project\build-shared-rng\SharedRngBridge.obj`" `"$project\build-camera-calc\CameraCalcBridge.obj`" `"$project\build-animation-copy\AnimationCopyBridge.obj`" `"$project\build-inline-rng\InlineRngBridge.obj`" `"$project\build-timer-lookup\TimerLookupBridge.obj`" `"$project\build-recorder\RecorderHook.lib`" `"$build\UE4SS.lib`" bcrypt.lib user32.lib gdi32.lib gdiplus.lib",
    "dumpbin /exports `"$dllDir\main.dll`""
)
Write-Host 'DLL staged locally. This does not install or launch the game.'



