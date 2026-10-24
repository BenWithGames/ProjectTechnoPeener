$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$vs=& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC tools missing'}
$build=Join-Path $project 'build-native'
New-Item -ItemType Directory -Force $build | Out-Null
$batch=Join-Path $build 'test-traversal-optimization.cmd'
@('@echo off',('call "'+$vs+'\VC\Auxiliary\Build\vcvars64.bat" >nul'),'if errorlevel 1 exit /b 1',
('cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /I"'+$project+'\third_party\minhook\include" "'+$project+'\tests\TraversalOptimizationTest.cpp" /Fo:"'+$build+'\TraversalOptimizationTest.obj" /Fe:"'+$build+'\TraversalOptimizationTest.exe"'),
'if errorlevel 1 exit /b 1',('"'+$build+'\TraversalOptimizationTest.exe"')) | Set-Content $batch -Encoding ascii
& $batch
if($LASTEXITCODE -ne 0){throw 'Traversal optimization test failed'}







