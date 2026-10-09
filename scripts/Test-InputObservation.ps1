$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$build=Join-Path $project 'build-network'
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC tools not found.'}
$commands=@('@echo off',"call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul",'if errorlevel 1 exit /b 1',
"cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT `"$project\tests\ObservationFixture.cpp`" /Fo`"$build\ObservationFixture.obj`" /Fe`"$build\ObservationFixture.exe`"",'if errorlevel 1 exit /b 1')
Set-Content "$build\build-observation-test.cmd" $commands -Encoding ascii
& "$build\build-observation-test.cmd"
if($LASTEXITCODE -ne 0){throw 'Observation fixture build failed.'}
