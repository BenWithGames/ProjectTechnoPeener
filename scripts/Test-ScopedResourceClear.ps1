$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$vs=& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC tools missing'}
# Isolate outputs so a terminated assertion process cannot lock the next build.
$build=Join-Path (Join-Path $project 'build-native') ('scoped-clear-test-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $build | Out-Null
$batch=Join-Path $build 'test-scoped-resource-clear.cmd'
@('@echo off',('call "'+$vs+'\VC\Auxiliary\Build\vcvars64.bat" >nul'),'if errorlevel 1 exit /b 1',
('cl /nologo /std:c++20 /EHsc /W4 /WX /I"'+$project+'\third_party\minhook\include" "'+$project+'\tests\ScopedResourceClearTest.cpp" /Fo:"'+$build+'\ScopedResourceClearTest.obj" /Fe:"'+$build+'\ScopedResourceClearTest.exe"'),
'if errorlevel 1 exit /b 1',('"'+$build+'\ScopedResourceClearTest.exe"')) | Set-Content $batch -Encoding ascii
& $batch
if($LASTEXITCODE -ne 0){throw 'Scoped clear test failed'}









