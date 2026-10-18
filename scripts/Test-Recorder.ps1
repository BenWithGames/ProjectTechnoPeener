param([switch]$TestOnly,[switch]$Optimized)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$build=Join-Path $project 'build-recorder'
New-Item -ItemType Directory -Force $build | Out-Null
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC tools not found.'}
$mh=Join-Path $project 'third_party\minhook'
$commands=@(
"@echo off", "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul",'if errorlevel 1 exit /b 1',
"cl /nologo /c /O2 /MD /I`"$mh\include`" /Fo`"$build\\`" `"$mh\src\buffer.c`" `"$mh\src\hook.c`" `"$mh\src\trampoline.c`" `"$mh\src\hde\hde64.c`"",
'if errorlevel 1 exit /b 1',
"ml64 /nologo /c /Fo`"$build\EntryBridge.obj`" `"$project\src\EntryBridge.asm`"",'if errorlevel 1 exit /b 1',
"ml64 /nologo /c /Fo`"$build\FieldUpdateBridge.obj`" `"$project\src\FieldUpdateBridge.asm`"",'if errorlevel 1 exit /b 1',
"ml64 /nologo /c /Fo`"$build\FieldResetBridge.obj`" `"$project\src\FieldResetBridge.asm`"",'if errorlevel 1 exit /b 1',
"ml64 /nologo /c /Fo`"$build\FieldAfterBridge.obj`" `"$project\src\FieldAfterBridge.asm`"",'if errorlevel 1 exit /b 1',
"ml64 /nologo /c /Fo`"$build\BattleAfterBridge.obj`" `"$project\src\BattleAfterBridge.asm`"",'if errorlevel 1 exit /b 1',
"ml64 /nologo /c /Fo`"$build\SetterFullBridge.obj`" `"$project\src\SetterFullBridge.asm`"",'if errorlevel 1 exit /b 1',
"ml64 /nologo /c /Fo`"$build\SetterZeroBridge.obj`" `"$project\src\SetterZeroBridge.asm`"",'if errorlevel 1 exit /b 1',
"ml64 /nologo /c /Fo`"$build\TailAfterBridge.obj`" `"$project\src\TailAfterBridge.asm`"",'if errorlevel 1 exit /b 1',
"ml64 /nologo /c /Fo`"$build\EventsAfterBridge.obj`" `"$project\src\EventsAfterBridge.asm`"",'if errorlevel 1 exit /b 1',
"ml64 /nologo /c /Fo`"$build\CleanupAfterBridge.obj`" `"$project\src\CleanupAfterBridge.asm`"",'if errorlevel 1 exit /b 1',
"ml64 /nologo /c /Fo`"$build\FieldAfterHarness.obj`" `"$project\tests\FieldAfterHarness.asm`"",'if errorlevel 1 exit /b 1',
"lib /nologo /out:`"$build\RecorderHook.lib`" `"$build\buffer.obj`" `"$build\hook.obj`" `"$build\trampoline.obj`" `"$build\hde64.obj`" `"$build\EntryBridge.obj`" `"$build\FieldUpdateBridge.obj`" `"$build\FieldResetBridge.obj`" `"$build\FieldAfterBridge.obj`" `"$build\CleanupAfterBridge.obj`" `"$build\EventsAfterBridge.obj`" `"$build\TailAfterBridge.obj`" `"$build\SetterZeroBridge.obj`" `"$build\SetterFullBridge.obj`" `"$build\BattleAfterBridge.obj`"",'if errorlevel 1 exit /b 1',
"cl /nologo /std:c++20 /EHsc /W4 /WX /Od /MD /I`"$mh\include`" `"$project\tests\RecorderHookTest.cpp`" /Fo`"$build\RecorderHookTest.obj`" /Fe`"$build\RecorderHookTest.exe`" /link `"$build\RecorderHook.lib`" bcrypt.lib",'if errorlevel 1 exit /b 1')
$commands += @("cl /nologo /std:c++20 /EHsc /W4 /WX /Od /MD `"$project\tests\RecordedInputBridgeTest.cpp`" /Fo`"$build\RecordedInputBridgeTest.obj`" /Fe`"$build\RecordedInputBridgeTest.exe`" /link `"$build\EntryBridge.obj`"",'if errorlevel 1 exit /b 1')
$commands += @("cl /nologo /std:c++20 /EHsc /W4 /WX /Od /MD `"$project\tests\FieldUpdateBridgeTest.cpp`" /Fo`"$build\FieldUpdateBridgeTest.obj`" /Fe`"$build\FieldUpdateBridgeTest.exe`" /link `"$build\FieldUpdateBridge.obj`" `"$build\FieldAfterBridge.obj`" `"$build\FieldAfterHarness.obj`"",'if errorlevel 1 exit /b 1')
if($Optimized){$commands=$commands | ForEach-Object { $_.Replace("/Od /MD","/O2 /Ob1 /fp:precise /MD") }}
Set-Content "$build\build.cmd" $commands -Encoding ascii
& "$build\build.cmd"
if($LASTEXITCODE -ne 0){throw 'Recorder test build failed.'}
& "$build\RecorderHookTest.exe"
if($LASTEXITCODE -ne 0){throw 'Recorder tests failed.'}

& "$build\RecordedInputBridgeTest.exe"
if($LASTEXITCODE -ne 0){throw 'Recorded input bridge tests failed.'}

& "$build\FieldUpdateBridgeTest.exe"
if($LASTEXITCODE -ne 0){throw "Field trace bridge tests failed."}
