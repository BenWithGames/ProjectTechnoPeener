param([switch]$Optimized)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$vs=& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC tools missing'}
$build=Join-Path $project 'build-native'
New-Item -ItemType Directory -Force $build | Out-Null
$batch=Join-Path $build 'test-outer-stack-continuation.cmd'
$options=if($Optimized){'/O2 /Ob1 /fp:precise /MD'}else{'/Od /MD'}
@('@echo off',('call "'+$vs+'\VC\Auxiliary\Build\vcvars64.bat" >nul'),'if errorlevel 1 exit /b 1',
('cl /nologo /std:c++20 /EHsc /W4 /WX '+$options+' "'+$project+'\tests\OuterStackContinuationTest.cpp" /Fo:"'+$build+'\OuterStackContinuationTest.obj" /Fe:"'+$build+'\OuterStackContinuationTest.exe"'),
'if errorlevel 1 exit /b 1',('"'+$build+'\OuterStackContinuationTest.exe"')) | Set-Content $batch -Encoding ascii
& $batch
if($LASTEXITCODE -ne 0){throw 'Outer stack continuation test failed'}
