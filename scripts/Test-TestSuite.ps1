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
