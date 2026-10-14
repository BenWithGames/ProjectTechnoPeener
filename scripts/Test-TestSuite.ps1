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
