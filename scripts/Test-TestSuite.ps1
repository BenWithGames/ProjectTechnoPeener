$ErrorActionPreference='Stop'
$dir=Join-Path (Split-Path $PSScriptRoot -Parent) 'build-native/suite-summary-tests'
New-Item -ItemType Directory -Path $dir -Force | Out-Null
$report=Join-Path $dir 'fixture.txt'
@'
status=SUITE_COMPLETED_WITH_FAILURES
suite_reason=snapshot_capture_failed
suite_local_pass=31
suite_local_fail=32
suite_local_skip=0
suite_peer_received=1
suite_peer_ack=1
suite_peer_pass=63
suite_peer_fail=0
suite_peer_skip=0
suite_round_changes=2
suite_rematch_candidates=0
suite_restart_required=0
'@ | Set-Content -LiteralPath $report
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Side -eq 'local' -and $_.Result -eq 'Failed'}).Count -ne 1){throw 'Local failure missing'}
if(@($result.stages | Where-Object {$_.Side -eq 'peer' -and $_.Result -eq 'Passed'}).Count -ne 6){throw 'Peer result missing'}
'status=timeout' | Set-Content -LiteralPath $report
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Result -eq 'Passed'}).Count){throw 'Missing results treated as passing'}
@'
status=SUITE_COMPLETED_WITH_FAILURES
suite_profile=5
suite_local_pass=127
suite_local_fail=128
suite_local_skip=0
suite_peer_received=1
suite_peer_pass=255
suite_peer_fail=0
suite_peer_skip=0
suite_correction_restart_required=1
'@ | Set-Content -LiteralPath $report
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(-not $result.restartRequired){throw 'Correction restart flag lost'}
if(@($result.stages | Where-Object {$_.Side -eq 'peer' -and $_.Result -eq 'Passed'}).Count -ne 8){throw 'Eight-stage peer correction result missing'}
if(@($result.stages | Where-Object {$_.Side -eq 'local' -and $_.Stage -eq 'Changed prediction corrected' -and $_.Result -eq 'Failed'}).Count -ne 1){throw 'Missing divergent correction counted as success'}
Write-Output 'PASS: suite summaries distinguish local failure, peer success, unavailable replay and missing results'

(Get-Content -LiteralPath $report -Raw).Replace("suite_profile=5","suite_profile=7") | Set-Content -LiteralPath $report
& (Join-Path $PSScriptRoot "Summarize-TestSuite.ps1") -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir "fixture-summary.json") -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Side -eq "peer" -and $_.Result -eq "Passed"}).Count -ne 8){throw "Attack correction stages missing"}
(Get-Content -LiteralPath $report -Raw).Replace('suite_profile=7','suite_profile=8') | Set-Content -LiteralPath $report
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Side -eq 'peer' -and $_.Result -eq 'Passed'}).Count -ne 8){throw 'Repeated attack stages missing'}
if(@($result.stages | Where-Object {$_.Side -eq 'local' -and $_.Stage -eq 'Three cycles: changed predictions corrected' -and $_.Result -eq 'Failed'}).Count -ne 1){throw 'Repeated attack failure lost'}
Write-Output 'PASS: repeated attack summary preserves failed correction and eight peer stages'
(Get-Content -LiteralPath $report -Raw).Replace('suite_profile=8','suite_profile=9') | Set-Content -LiteralPath $report
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Side -eq 'peer' -and $_.Result -eq 'Passed'}).Count -ne 8){throw 'Combat correction stages missing'}
if(@($result.stages | Where-Object {$_.Side -eq 'local' -and $_.Stage -eq 'Changed prediction corrected' -and $_.Result -eq 'Failed'}).Count -ne 1){throw 'Combat correction failure lost'}
Write-Output 'PASS: combat correction eight stages and failed correction retained'

@'
status=SUITE_NO_SUITABLE_WINDOW
suite_reason=no_suitable_window_observed
suite_profile=9
suite_local_pass=15
suite_local_fail=0
suite_local_skip=240
suite_peer_received=1
suite_peer_pass=15
suite_peer_fail=0
suite_peer_skip=240
'@ | Set-Content -LiteralPath $report
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Result -eq 'Not observed: no suitable input window'}).Count -ne 8){throw 'Neutral exhaustion must remain unobserved on both sides'}
if(@($result.stages | Where-Object {$_.Result -eq 'Failed'}).Count){throw 'Neutral exhaustion mislabeled as failure'}
Write-Output 'PASS: neutral exhaustion is not a correction failure or success'
@'
suite_cycle_1_topology_added_nodes=1
suite_cycle_1_topology_removed_nodes=0
suite_cycle_1_topology_added_vtable_rva=65572072
suite_cycle_1_topology_before_count=2053
suite_cycle_1_topology_after_count=2055
'@ | Add-Content -LiteralPath $report
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if($result.windLifecycleEvents.Count -ne 1 -or $result.windLifecycleEvents[0].added -ne 1 -or $result.windLifecycleEvents[0].removed -ne 0){throw 'Wind creation evidence lost'}
if(@($result.stages | Where-Object {$_.Stage -eq 'Wind object lifecycle' -and $_.Result -eq 'Passed'}).Count){throw 'Unconnected wind restoration must not pass'}
Write-Output 'PASS: wind lifecycle evidence retained without claiming native restoration or peer success'
@'
suite_wind_native_integrated=1
suite_wind_failure=0
suite_wind_captures=4
suite_wind_retirements=4
suite_wind_loads=3
suite_wind_comparisons=3
suite_wind_added=0
suite_wind_removed=0
'@ | Add-Content -LiteralPath $report
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Stage -eq 'Wind object lifecycle' -and $_.Result -eq 'Passed'}).Count){throw 'No-change wind run must not prove lifecycle restoration'}
'suite_wind_removed=1' | Add-Content -LiteralPath $report
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Stage -eq 'Wind object lifecycle' -and $_.Result -eq 'Passed'}).Count -ne 1){throw 'Restored and retired local lifecycle event missing'}
'suite_wind_failure=3' | Add-Content -LiteralPath $report
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Stage -eq 'Wind object lifecycle' -and $_.Result -eq 'Passed'}).Count){throw 'Wind failure must override successful earlier comparisons'}
Write-Output 'PASS: native wind lifecycle requires list change, comparison, cleanup and no failure'

@('suite_method_schema=2','suite_cycles_active=1','suite_cycle_1_reference_attempted=1','suite_cycle_1_reference_verified=0','suite_cycle_1_predicted=0','suite_cycle_1_stop_outcome=3') | Add-Content -LiteralPath $report
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Stage -eq 'Attempt 1 original-input replay' -and $_.Result -eq 'Failed'}).Count -ne 1){throw 'Reference prerequisite failure lost'}
if(@($result.stages | Where-Object {$_.Stage -eq 'Attempt 1 prediction correction' -and $_.Result -eq 'Not exercised'}).Count -ne 1){throw 'Unrun prediction misclassified'}
if(@($result.stages | Where-Object {$_.Stage -eq 'Attempt 1 stop/recovery' -and $_.Result -like 'FAILED*'}).Count -ne 1){throw 'Unsafe stop must be a separate failure'}
Write-Output 'PASS: reference-first results distinguish unrun prediction and unsafe stopping'
