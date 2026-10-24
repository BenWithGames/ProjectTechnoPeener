$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$build=Join-Path $project 'build-compact-boundary'
New-Item -ItemType Directory -Force $build | Out-Null
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC tools not found.'}
$commands=@('@echo off',"call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul",'if errorlevel 1 exit /b 1',
"cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /Ob1 /fp:precise /MD /I`"$project\third_party\minhook\include`" `"$project\tests\CompactBoundaryTest.cpp`" /Fo`"$build\CompactBoundaryTest.obj`" /Fe`"$build\CompactBoundaryTest.exe`"",'if errorlevel 1 exit /b 1')
Set-Content "$build\build.cmd" $commands -Encoding ascii
& "$build\build.cmd"
if($LASTEXITCODE -ne 0){throw 'Compact boundary test build failed.'}
& "$build\CompactBoundaryTest.exe"
if($LASTEXITCODE -ne 0){throw 'Compact boundary tests failed.'}






