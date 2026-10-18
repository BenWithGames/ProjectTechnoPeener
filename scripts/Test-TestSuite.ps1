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

@'
status=SUITE_RETENTION_NOT_OBSERVED
suite_profile=11
suite_local_pass=63
suite_local_fail=0
suite_local_skip=64
suite_peer_received=1
suite_peer_pass=127
suite_peer_fail=0
suite_peer_skip=0
'@ | Set-Content -LiteralPath $report
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Side -eq 'local' -and $_.Stage -eq 'Pinned removal and allocator cleanup' -and $_.Result -eq 'Not observed (not PASS)'}).Count -ne 1){throw 'Unobserved retention counted as pass'}
if(@($result.stages | Where-Object {$_.Side -eq 'peer' -and $_.Stage -eq 'Pinned removal and allocator cleanup' -and $_.Result -eq 'Passed'}).Count -ne 1){throw 'Peer retention result missing'}
Write-Output 'PASS: retention summary distinguishes bilateral evidence from no lifecycle coverage'

@'
status=SUITE_COMPLETE
suite_profile=12
suite_local_pass=255
suite_local_fail=0
suite_local_skip=0
suite_peer_received=1
suite_peer_pass=255
suite_peer_fail=0
suite_peer_skip=0
suite_retained_replay_restart=0
'@ | Set-Content -LiteralPath $report
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Stage -eq 'Retained object revived' -and $_.Result -eq 'Passed'}).Count -ne 2){throw 'Bilateral revival stage missing'}
$text=[IO.File]::ReadAllText($report).Replace('suite_local_pass=255','suite_local_pass=63').Replace('suite_local_fail=0','suite_local_fail=192').Replace('suite_retained_replay_restart=0','suite_retained_replay_restart=1')
[IO.File]::WriteAllText($report,$text)
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(-not $result.restartRequired -or @($result.stages | Where-Object {$_.Side -eq 'local' -and $_.Result -eq 'Failed'}).Count -ne 2){throw 'Replay/revival failure lost'}
Write-Output 'PASS: retained replay summary requires both replay and revival; restart failures preserved'

Add-Content -LiteralPath $report -Value "`nsuite_retained_replay_cycle_target=3`nsuite_retained_replay_cycles_completed=1"
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Stage -eq 'Bounded retained replay cycles' -and $_.Result -eq '1/3 verified and retired'}).Count -ne 1){throw 'Partial retained cycle coverage missing'}

@'
status=SUITE_COMPLETE
suite_profile=13
suite_local_pass=255
suite_local_fail=0
suite_local_skip=0
suite_peer_received=1
suite_peer_pass=255
suite_peer_fail=0
suite_peer_skip=0
suite_retained_replay_cycle_target=1
suite_retained_replay_cycles_completed=1
'@ | Set-Content -LiteralPath $report
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Stage -eq 'Same-session replay and cleanup' -and $_.Result -eq 'Passed'}).Count -ne 2){throw 'Owned recording stage missing'}
if(@($result.stages | Where-Object {$_.Stage -eq 'Retained object revived'}).Count){throw 'Owned recorder falsely claims revival'}
Write-Output 'PASS: owned recorder requires reference replay and cleanup without claiming revival'

$text=[IO.File]::ReadAllText($report).Replace('suite_profile=13','suite_profile=14').Replace('suite_retained_replay_cycle_target=1','suite_retained_replay_cycle_target=3').Replace('suite_retained_replay_cycles_completed=1','suite_retained_replay_cycles_completed=3')
[IO.File]::WriteAllText($report,$text)
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Stage -eq 'Three cycles verified and cleaned up' -and $_.Result -eq 'Passed'}).Count -ne 2){throw 'Repeated owned replay stage missing'}
$text=$text.Replace('suite_local_pass=255','suite_local_pass=63').Replace('suite_local_skip=0','suite_local_skip=192').Replace('suite_retained_replay_cycles_completed=3','suite_retained_replay_cycles_completed=1')
[IO.File]::WriteAllText($report,$text)
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Side -eq 'local' -and $_.Stage -eq 'Three cycles verified and cleaned up' -and $_.Result -eq 'Not observed (not PASS)'}).Count -ne 1){throw 'Partial repeated recording counted as PASS'}
Write-Output 'PASS: repeated recording summary preserves partial completion without PASS'

$text=$text.Replace('suite_profile=14','suite_profile=15')
[IO.File]::WriteAllText($report,$text)
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Side -eq 'local' -and $_.Stage -eq 'Replay cycles verified and cleaned up' -and $_.Result -eq 'Not observed (not PASS)'}).Count -ne 1){throw 'Partial multi-round replay counted as PASS'}
Write-Output 'PASS: multi-round replay summary distinguishes incomplete coverage'

Add-Content -LiteralPath $report -Value "`nsuite_fight_gate_schema=1`nsuite_fight_gate_enabled=1`nsuite_fight_wait_frames=119`nsuite_fight_partial_discards=1"
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Side -eq 'local' -and $_.Stage -eq 'Fight-ready capture gate' -and $_.Result -like '*waited 119*discarded 1*'}).Count -ne 1){throw 'Fight gate evidence missing'}
if(@($result.stages | Where-Object {$_.Side -eq 'peer' -and $_.Stage -eq 'Fight-ready capture gate' -and $_.Result -eq 'Passed'}).Count){throw 'Peer fight gate falsely inferred'}
Write-Output 'PASS: fight gate summary includes waits/discards without inventing peer evidence'
Add-Content -LiteralPath $report -Value "`nsuite_scoped_clear_schema=1`nsuite_scoped_clear_seen=3`nsuite_scoped_clear_ignored=2`nsuite_scoped_clear_related=0`nsuite_scoped_clear_unknown=1`nsuite_scoped_clear_reason=4"
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Side -eq 'local' -and $_.Stage -eq 'Scoped resource clears' -and $_.Result -like '*unrelated ignored: 2*unknown: 1*'}).Count -ne 1){throw 'Scoped clear evidence missing'}
if(@($result.stages | Where-Object {$_.Side -eq 'peer' -and $_.Stage -eq 'Scoped resource clears' -and $_.Result -eq 'Passed'}).Count){throw 'Peer clear evidence falsely inferred'}
Write-Output 'PASS: scoped clear summary preserves unknown outcomes without inventing peer evidence'

Add-Content -LiteralPath $report -Value "`nsuite_match_end_clean=1`nsuite_retained_replay_cycles_completed=2"
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Stage -eq 'Match-end cleanup' -and $_.Result -like '*2/3*not PASS*'}).Count -ne 1){throw 'Clean match-end summary missing'}
if(@($result.stages | Where-Object {$_.Side -eq 'local' -and $_.Stage -eq 'Replay cycles verified and cleaned up' -and $_.Result -eq 'Passed'}).Count){throw 'Match end incorrectly promoted incomplete coverage'}
Write-Output 'PASS: clean match end preserves incomplete coverage and does not imply peer details'
Add-Content -LiteralPath $report -Value "`nsuite_scoped_clear_schema=2`nsuite_timing_diagnostics_schema=1`nsuite_timing_qpc_frequency=10000000`nsuite_timing_1_restore_count=2`nsuite_timing_1_restore_total_ticks=50000`nsuite_timing_1_restore_max_ticks=30000"
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Stage -eq 'Cycle 1 timing: restore' -and $_.Result -match 'Count 2; total 5[.,]000 ms; max 3[.,]000 ms'}).Count -ne 1){throw 'Timing conversion missing or incorrect'}
if(@($result.stages | Where-Object {$_.Side -eq 'peer' -and $_.Stage -like '*timing:*'}).Count){throw 'Peer timing invented'}
if(@($result.stages | Where-Object {$_.Side -eq 'local' -and $_.Stage -eq 'Scoped resource clears'}).Count -ne 1){throw 'Schema2 clear diagnostics missing'}
Write-Output 'PASS: timing converts QPC ticks to ms, preserves scope and schema2 clears'

Add-Content -LiteralPath $report -Value "`nsuite_wind_gate_schema=1`nsuite_wind_gate_outcome=2`nsuite_wind_gate_attempts=5`nsuite_wind_gate_contentions=1`nsuite_wind_gate_stops=0`nsuite_wind_gate_last_rejection=2`nsuite_wind_gate_thread=10`nsuite_wind_gate_owner=10`nsuite_wind_gate_frame=77"
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Side -eq 'local' -and $_.Stage -eq 'Effect capture gate' -and $_.Result -like 'Lock contended; attempts: 5; contentions: 1; stops: 0*'}).Count -ne 1){throw 'Exact gate rejection summary missing'}
if(@($result.stages | Where-Object {$_.Side -eq 'peer' -and $_.Stage -eq 'Effect capture gate'}).Count){throw 'Peer gate outcome invented'}
Write-Output 'PASS: effect gate reports exact rejection without inventing peer evidence'
Add-Content -LiteralPath $report -Value "`nsuite_emitter_schema=1`nsuite_emitter_check=0`nsuite_emitter_count=2`nsuite_emitter_restores=3`nsuite_emitter_compares=4"
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Side -eq 'local' -and $_.Stage -eq 'Wind emitter state' -and $_.Result -like '*restores: 3; comparisons: 4*'}).Count -ne 1){throw 'Emitter diagnostic missing'}
if(@($result.stages | Where-Object {$_.Side -eq 'peer' -and $_.Stage -eq 'Wind emitter state'}).Count){throw 'Peer emitter evidence invented'}
Write-Output 'PASS: emitter restoration counters retained in summary'
Add-Content -LiteralPath $report -Value "`nsuite_evidence_deferred=1`nsuite_evidence_arena_limit=33554432"
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Side -eq 'local' -and $_.Stage -eq 'Diagnostic capture' -and $_.Result -like '*33554432*'}).Count -ne 1){throw 'Deferred evidence summary missing'}
Write-Output 'PASS: deferred diagnostic mode included without changing pass criteria'

$text=[IO.File]::ReadAllText($report).Replace('suite_profile=15','suite_profile=16')
$text+="`nsuite_continuous_match=1`nsuite_match_completed=1`nsuite_evidence_flushed=21`nsuite_evidence_wait_frames=5`nsuite_evidence_io_failures=0"
[IO.File]::WriteAllText($report,$text)
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$result=Get-Content -LiteralPath (Join-Path $dir 'fixture-summary.json') -Raw | ConvertFrom-Json
if(@($result.stages | Where-Object {$_.Stage -eq 'Bounded evidence queue' -and $_.Result -like 'Flushed: 21; wait frames: 5; I/O failures: 0*'}).Count -ne 1){throw 'Full-match queue diagnostic missing'}
if(@($result.stages | Where-Object {$_.Stage -eq 'Match-end cleanup' -and $_.Result -like '*/3*'}).Count){throw 'Full-match incorrectly summarized as three-cycle target'}
Write-Output 'PASS: full-match queue and completion summary retained without a fixed cycle target'

Add-Content -LiteralPath $report -Value "`nsuite_profile=16`nsuite_fight_gate_schema=2`nsuite_fight_gate_enabled=1`nsuite_fight_native_playback=1`nsuite_fight_round_result=0`nsuite_fight_playback_wait_frames=12`nsuite_fight_result_wait_frames=3"
& (Join-Path $PSScriptRoot 'Summarize-TestSuite.ps1') -Report $report | Out-Null
$playbackSummary=[IO.File]::ReadAllText(($report -replace '\.txt$','-summary.json'))
if($playbackSummary -notmatch 'playback waits 12, round-result waits 3'){throw 'Native playback diagnostics missing from summary'}
Write-Host 'PASS: native playback and round-result gate diagnostics preserved in summary'
