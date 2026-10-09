param([ValidateRange(1,100)][int]$Repetitions=10)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
# A fresh build directory also keeps failed historical builds separate.
$run=Join-Path $project ('build-debug-tests\'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $run | Out-Null
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC x64 tools not found.'}
@"
@echo off
call "$vs\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX "$project\tools\WatchTimer.cpp" /Fo:"$run\WatchTimer.obj" /Fe:"$run\WatchTimerSelfTest.exe" /link dbghelp.lib
"@ | Set-Content "$run\build.cmd" -Encoding ascii
& "$run\build.cmd"
if($LASTEXITCODE -ne 0){throw 'Self-test build failed.'}
$results=@()
foreach($scenario in @('limit','timeout','cancel','idle')) {
    foreach($iteration in 1..$Repetitions) {
        $log=Join-Path $run "$scenario-$iteration.txt"
        & "$run\WatchTimerSelfTest.exe" --self-test $scenario $log
        $code=$LASTEXITCODE
        $results += [pscustomobject]@{Scenario=$scenario;Iteration=$iteration;Passed=($code -eq 0);ExitCode=$code;Log=$log}
        if($code -ne 0) {
            $results | ConvertTo-Json | Set-Content "$run\results.json"
            Get-Content $log -Tail 10
            throw "Debugger self-test failed; stopped further tests. Logs: $run"
        }
    }
    Write-Host "$scenario`: $Repetitions passed"
}
$results | ConvertTo-Json | Set-Content "$run\results.json"
Write-Host "All $($results.Count) tests passed. Results: $run\results.json"
