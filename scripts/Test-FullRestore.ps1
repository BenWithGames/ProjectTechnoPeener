$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$build=Join-Path $project 'build-full-restore'
New-Item -ItemType Directory -Force $build | Out-Null
# Separate test invocations cannot overwrite a still-running test executable.
$testExe=Join-Path $build ('FullStateProbeTest-'+$PID+'.exe')
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC tools not found.'}
$commands=@('@echo off',"call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul",'if errorlevel 1 exit /b 1',
"cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MD `"$project\tests\FullStateProbeTest.cpp`" /Fo`"$build\FullStateProbeTest.obj`" /Fe`"$testExe`"",'if errorlevel 1 exit /b 1')
Set-Content "$build\build.cmd" $commands -Encoding ascii
& "$build\build.cmd"
if($LASTEXITCODE -ne 0){throw 'Full restore test build failed.'}
& $testExe
if($LASTEXITCODE -ne 0){throw 'Full restore tests failed.'}


