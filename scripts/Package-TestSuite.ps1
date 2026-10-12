param([string]$OutputDirectory=(Join-Path $env:USERPROFILE 'Downloads'),
      [ValidateSet('PredictionCorrection','PredictionCorrection-Diagnostic')][string]$BuildLabel='PredictionCorrection')
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$name='SC6Rollback-'+$BuildLabel+'-Suite-'+$stamp
$destination=Join-Path $project ('build-network\'+$name)
$payload=Join-Path $destination 'observer-mod'
New-Item -ItemType Directory -Path $payload -Force | Out-Null
foreach($name in @('Steam-TestSuite.cmd','Steam-TestSuite.ps1','Steam-Inputs.ps1','Summarize-TestSuite.ps1','Install-Observer.cmd','Install-Observer.ps1','Collect-RollbackReport.cmd','Collect-RollbackReport.ps1')){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $destination
}
$installer=Join-Path $destination 'Install-Observer.ps1'
$text=[IO.File]::ReadAllText($installer).Replace('Installed and verified observer.','Installed and verified PredictionCorrection suite.').Replace('No recording starts until Observe-Inputs.cmd is explicitly run.','Follow README.txt: warm up, return to the private lobby, then run Steam-TestSuite.cmd.')
[IO.File]::WriteAllText($installer,$text)
foreach($name in @('dwmapi.dll','UE4SS.dll','UE4SS-settings.ini','UE4SS-LICENSE.txt','MINHOOK-LICENSE.txt','main.dll')){
    $source=if($name -eq 'main.dll'){Join-Path $project 'package\Mods\SC6Rollback\dlls\main.dll'}else{Join-Path $project ('package\'+$name)}
    Copy-Item -LiteralPath $source -Destination (Join-Path $payload $name)
}
'{"profile":"PredictionCorrection","observeSeconds":60}' | Set-Content -LiteralPath (Join-Path $destination 'TestSuite.json') -Encoding ascii
@'
SC6Rollback PredictionCorrection suite
One controlled three-frame wrong-movement prediction and network-confirmed correction.
Continuous combat rollback remains disabled. This test uses a consenting private match.

Both PCs:
1. Close SC6. Extract this ZIP and run Install-Observer.cmd.
2. Launch SC6, enter one private warmup match, and return to the lobby without scripts.
3. Run Steam-TestSuite.cmd. Enter the OTHER player's friend code and your assigned
   P1/P2 slot. Keep the supplied TestSuite.json on both PCs. Wait at the final Enter prompt.
   If Codex starts Ben's script, only the partner launches this CMD.
4. Start the scripts together, wait for both to connect, then enter the next private match.
5. Leave BOTH controls untouched until observing_suite_gameplay appears. There are
   two brief native clock holds: initial agreement and a bounded input-confirmation wait.
   The test captures four clean states, simulates three wrong remote movement inputs,
   rewinds that prediction, and checks three corrected frames against the clean reference.
6. After observing_suite_gameplay, play normally for 60 seconds. Leave SC6 open until
   the suite reports completion; stage results are exchanged automatically.
7. Run Collect-RollbackReport.cmd and paste the raw report path printed by the test.
   Send the resulting ZIP in Downloads to Ben. Nothing is uploaded automatically.
   Restart both games before another suite.

SUITE_COMPLETE requires eight selected stages to pass on BOTH PCs, including an
actual player-state change and three verified corrected updates. Transport alone
is insufficient. A no-change prediction is a failed correction test.
If prediction_correction_failed_restart_required appears, or gameplay freezes,
close both games and retain the reports. Do not retry in the same game process.
Safe pre-mutation rejection may continue observation with failed stages.
Text and JSON summaries are saved beside captures/steam-inputs-PID.txt.
Automatic compatibility/ping is included; this script does not enable ranked rollback.
'@ | Set-Content -LiteralPath (Join-Path $destination 'README.txt') -Encoding ascii
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$zip=Join-Path $OutputDirectory ((Split-Path $destination -Leaf)+'.zip')
Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $zip
$dllHash=(Get-FileHash -LiteralPath (Join-Path $payload 'main.dll')).Hash
$builtHash=(Get-FileHash -LiteralPath (Join-Path $project 'package\Mods\SC6Rollback\dlls\main.dll')).Hash
if($dllHash -ne $builtHash){throw 'Packaged DLL mismatch'}
[pscustomobject]@{Folder=$destination;Zip=$zip;DllSHA256=$dllHash;ZipSHA256=(Get-FileHash -LiteralPath $zip).Hash}
