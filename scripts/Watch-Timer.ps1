param([Parameter(Mandatory=$true)][int]$GameProcessId,
      [Parameter(Mandatory=$true)][switch]$TrainingConfirmed,
      [Parameter(Mandatory=$true)][string]$OutputPath,
      [ValidateSet('Writes','Callers','Outer')][string]$Mode='Writes',
      [switch]$BuildOnly,
      [switch]$SmokeTest)
$ErrorActionPreference='Stop'
if (!$BuildOnly -and !$SmokeTest) {
    throw 'Live game tracing remains disabled after the reported crash. Use Test-Debugger.ps1 for isolated validation.'
}
if (!$TrainingConfirmed) { throw 'Offline training confirmation is required.' }
$process=Get-Process -Id $GameProcessId
if ($process.ProcessName -ne 'SoulcaliburVI') { throw 'Unexpected process.' }
if ((Get-FileHash -LiteralPath $process.Path).Hash -ne 'F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553') {
    throw 'Unsupported executable hash.'
}
if (Test-Path -LiteralPath $OutputPath) { throw 'Output already exists; choose a new capture name.' }
$project=Split-Path $PSScriptRoot -Parent
$build=Join-Path $project ('build-watch\'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $build | Out-Null
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC tools not found.' }
$batch=@"
@echo off
call "$vs\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX "$project\tools\WatchTimer.cpp" /Fo:"$build\WatchTimer.obj" /Fe:"$build\WatchTimer.exe" /link dbghelp.lib
"@
Set-Content "$build\build.cmd" $batch -Encoding ascii
& "$build\build.cmd"
if ($LASTEXITCODE -ne 0) { throw 'Watchpoint tool compilation failed.' }
if ($BuildOnly) { Write-Host 'Compilation succeeded; no debugger attached.'; return }
Write-Host 'Starting short caller test: 100 events or 2 seconds, then checking timer progression after detach. Keep SC6 in offline Training.'
$traceMode=if ($Mode -eq 'Outer') { 'outer-smoke' } else { 'smoke' }
& "$build\WatchTimer.exe" $GameProcessId --training-confirmed $OutputPath $traceMode
Get-Content -LiteralPath $OutputPath -Tail 4
if ($LASTEXITCODE -ne 0) { throw "Watchpoint test did not complete successfully. Inspect $OutputPath" }

