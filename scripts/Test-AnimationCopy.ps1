$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$build=Join-Path $project 'build-animation-copy'
New-Item -ItemType Directory -Force $build | Out-Null
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC tools not found.'}
$commands=@('@echo off',"call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul",'if errorlevel 1 exit /b 1',
"ml64 /nologo /c /Fo`"$build\AnimationCopyBridge.obj`" `"$project\src\AnimationCopyBridge.asm`"",'if errorlevel 1 exit /b 1',
"ml64 /nologo /c /Fo`"$build\AnimationCopyHarness.obj`" `"$project\tests\AnimationCopyHarness.asm`"",'if errorlevel 1 exit /b 1',
"cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MD /I`"$project\third_party\minhook\include`" `"$project\tests\AnimationCopyTest.cpp`" /Fo`"$build\AnimationCopyTest.obj`" /Fe`"$build\AnimationCopyTest.exe`" /link `"$build\AnimationCopyBridge.obj`" `"$build\AnimationCopyHarness.obj`" `"$project\build-recorder\RecorderHook.lib`"",'if errorlevel 1 exit /b 1')
Set-Content "$build\build.cmd" $commands -Encoding ascii
& "$build\build.cmd"
if($LASTEXITCODE -ne 0){throw 'Animation copy test build failed.'}
& "$build\AnimationCopyTest.exe"
if($LASTEXITCODE -ne 0){throw 'Animation copy tests failed.'}



python -B (Join-Path $project 'tests/AnimationCopyEvidenceTest.py')
if($LASTEXITCODE -ne 0){throw 'Animation evidence tests failed.'}
