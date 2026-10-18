param([Parameter(Mandatory=$true)][string]$Report)
$ErrorActionPreference='Stop'
$file=[IO.File]::Open($Report,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
try {$reader=New-Object IO.StreamReader($file);try{$text=$reader.ReadToEnd()}finally{$reader.Dispose()}}finally{$file.Dispose()}
$values=@{}
foreach($line in ($text -split '\r?\n')){if($line -match '^([^=]+)=(.*)$'){$values[$Matches[1]]=$Matches[2]}}
$names=@('Transport','Resources and cleanup','Frame pairs','Boundary agreement','Hold and release','Four snapshots','Three-frame reference replay')
if($values.suite_profile -in @('5','6','7','8','9','10')){$names[6]='Three corrected frames';$names+= 'Changed prediction corrected'}
if($values.suite_profile -in @('6','8')){$names[6]='Three cycles: corrected frames';$names[7]='Three cycles: changed predictions corrected'}
if($values.suite_profile -eq '11'){$names[5]='Retention snapshot windows';$names[6]='Pinned removal and allocator cleanup'}
if($values.suite_profile -eq '12'){$names[5]='Owned combat snapshot window';$names[6]='Original-input replay verified';$names+='Retained object revived'}
if($values.suite_profile -eq '13'){$names[5]='Four retained recordings';$names[6]='Three original frames replayed';$names+='Same-session replay and cleanup'}
if($values.suite_profile -in @('15','16')){$names[5]='Retained recordings';$names[6]='Bounded original-input replay';$names+='Replay cycles verified and cleaned up'}
if($values.suite_profile -eq '14'){$names[5]='Retained recording windows';$names[6]='Three cycles: original-input replay';$names+='Three cycles verified and cleaned up'}
$rows=@()
if($values.suite_match_end_clean -eq '1' -and $values.suite_profile -ne '16'){
    $rows += [pscustomobject]@{Side='local';Stage='Match-end cleanup';Result=("Clean match end; {0}/3 cycles verified. Missing cycles are not PASS." -f $values.suite_retained_replay_cycles_completed)}
}

foreach($side in @('local','peer')){
    for($i=0;$i -lt $names.Count;$i++){
        $bit=1 -shl $i;$result='Not completed'
        if($side -eq 'peer' -and $values.suite_peer_received -ne '1'){$result='Report unavailable'}
        elseif(([int]$values["suite_${side}_fail"] -band $bit) -ne 0){$result='Failed'}
        elseif(([int]$values["suite_${side}_pass"] -band $bit) -ne 0){$result='Passed'}
        elseif(([int]$values["suite_${side}_skip"] -band $bit) -ne 0){$result=if($values.suite_profile -in @('11','12','13','14','15','16') -and $i -ge 5){'Not observed (not PASS)'}elseif($values.suite_reason -eq 'no_suitable_window_observed'){'Not observed: no suitable input window'}else{'Skipped (not selected or prerequisite failed)'}}
        $rows += [pscustomobject]@{Side=$side;Stage=$names[$i];Result=$result}
    }
}
$rows += [pscustomobject]@{Side='both';Stage='Continuous rollback';Result='Unavailable in this runner'}
$windEvents=@()
foreach($key in @($values.Keys | Sort-Object)){
    if($key -match '^suite_cycle_(\d+)_topology_added_nodes$'){
        $cycle=[int]$Matches[1];$prefix="suite_cycle_${cycle}_topology_"
        $added=[int]$values[$key];$removed=[int]$values[($prefix+'removed_nodes')]
        if($added -or $removed){
            $windEvents += [pscustomobject]@{cycle=$cycle;added=$added;removed=$removed;
                firstAddedTypeRva=$values[($prefix+'added_vtable_rva')];firstRemovedTypeRva=$values[($prefix+'removed_vtable_rva')];
                beforeCount=$values[($prefix+'before_count')];afterCount=$values[($prefix+'after_count')]}
        }
    }
}
if($values.suite_profile -in @('9','10')){
    $windResult='Lifecycle restoration not connected in this report'
    if($values.suite_wind_native_integrated -eq '1'){
        $windResult='Not exercised'
        if([int]$values.suite_wind_failure -ne 0){$windResult='Failed: native wind validation or cleanup';if($values.suite_wind_external_check -and $values.suite_wind_external_check -ne '0'){$windResult+=" (reference check $($values.suite_wind_external_check), visits $($values.suite_wind_external_visits))"}}
        elseif([int]$values.suite_wind_loads -gt 0 -and [int]$values.suite_wind_comparisons -gt 0){
            $windResult='Restore compared; no object-list change observed'
            if(([int]$values.suite_wind_added+[int]$values.suite_wind_removed) -gt 0){
                $windResult=if($values.suite_wind_captures -eq $values.suite_wind_retirements){'Passed'}else{'Compared; retirement incomplete'}
            }
        }
    }
    $rows += [pscustomobject]@{Side='local';Stage='Wind object lifecycle';Result=$windResult}
    $rows += [pscustomobject]@{Side='peer';Stage='Wind object lifecycle';Result='Requires peer raw report; not implied by transport PASS'}
}
if($values.suite_method_schema -eq '2'){
    for($cycle=1;$cycle -le [int]$values.suite_cycles_active;$cycle++){
        $p="suite_cycle_${cycle}_"
        $reference=if($values[($p+'reference_verified')] -eq '1'){'Passed'}elseif($values[($p+'reference_attempted')] -eq '1'){'Failed'}else{'Not exercised'}
        $prediction=if($values[($p+'verified')] -eq '1'){'Passed'}elseif($values[($p+'no_divergence_skipped')] -eq '1'){'Inconclusive: no prediction difference'}elseif([int]$values[($p+'predicted')] -gt 0){'Failed or incomplete'}else{'Not exercised'}
        $stop=switch($values[($p+'stop_outcome')]){'1' {'Verified endpoint and cleanup'} '2' {'Safe pre-mutation rejection'} '3' {'FAILED: unsafe stop; restart required'} default {'Not exercised'}}
        $rows += [pscustomobject]@{Side='local';Stage="Attempt $cycle original-input replay";Result=$reference}
        $rows += [pscustomobject]@{Side='local';Stage="Attempt $cycle prediction correction";Result=$prediction}
        $rows += [pscustomobject]@{Side='local';Stage="Attempt $cycle stop/recovery";Result=$stop}
        if([int]$values[($p+'deferred_animation_bytes')] -gt 0){
            $rows += [pscustomobject]@{Side='local';Stage="Attempt $cycle derived animation";Result='Intermediate difference retained; endpoint comparison remains exact'}
        }
    }
    $rows += [pscustomobject]@{Side='both';Stage='Repeated corrections';Result=if($values.status -eq 'SUITE_COMPLETE'){'Three bilateral corrections completed'}else{'Not completed'}}
    $rows += [pscustomobject]@{Side='local';Stage='Round/rematch coverage';Result="Observed transitions: $($values.suite_round_changes); rematch candidates: $($values.suite_rematch_candidates). Cross-boundary correction not certified."}
    $rows += [pscustomobject]@{Side='peer';Stage='Reference and recovery details';Result='Requires peer raw report'}
}
if($values.suite_retained_replay_cycle_target -and $values.suite_profile -ne '16'){
    $rows += [pscustomobject]@{Side='local';Stage='Bounded retained replay cycles';Result="$($values.suite_retained_replay_cycles_completed)/$($values.suite_retained_replay_cycle_target) verified and retired"}
    $rows += [pscustomobject]@{Side='peer';Stage='Bounded retained replay cycle details';Result='See peer report for frame ranges and diagnostic traces'}
}
if($values.suite_profile -in @('15','16')){
    $gate='Unavailable in this report'
    if($values.suite_fight_gate_schema -in @('1','2') -and $values.suite_fight_gate_enabled -eq '1'){
        $gate="Native Normal mode required on each snapshot; waited $($values.suite_fight_wait_frames) boundaries; discarded $($values.suite_fight_partial_discards) partial windows"
    }
    if($values.suite_fight_gate_schema -eq '2'){$gate+="; playback waits $($values.suite_fight_playback_wait_frames), round-result waits $($values.suite_fight_result_wait_frames); latest playback=$($values.suite_fight_native_playback), result=$($values.suite_fight_round_result)"}
    $rows += [pscustomobject]@{Side='local';Stage='Fight-ready capture gate';Result=$gate}
    $rows += [pscustomobject]@{Side='peer';Stage='Fight-ready capture gate';Result='See peer raw report; not inferred from transport PASS'}
}
if($values.suite_camera_calc_schema -eq '1'){$rows += [pscustomobject]@{Side='local';Stage='Camera calculation diagnostics';Result="Installed: $($values.suite_camera_calc_installed); events: $($values.suite_camera_calc_events); dropped: $($values.suite_camera_calc_dropped); overwritten: $($values.suite_camera_calc_overwritten). Six stages retained in camera.calculations binary evidence; not a replay pass."}}
if($values.suite_timer_lookup_schema -eq '1'){
    $rows += [pscustomobject]@{Side='local';Stage='Animation timer evidence';Result="Installed: $($values.suite_timer_lookup_installed); events: $($values.suite_timer_lookup_events); invalid: $($values.suite_timer_lookup_invalid); dropped: $($values.suite_timer_lookup_dropped); overwritten: $($values.suite_timer_lookup_overwritten); written: $($values.suite_timer_lookup_written). Evidence only, not a replay pass."}
    $rows += [pscustomobject]@{Side='peer';Stage='Animation timer evidence';Result='Requires peer report and timer-lookup.bin'}
}
if($values.suite_evidence_deferred -eq '1'){
    $rows += [pscustomobject]@{Side='local';Stage='Diagnostic capture';Result="Preallocated immutable evidence; report worker serializes. Arena limit: $($values.suite_evidence_arena_limit) bytes per bundle. Full verification remains enabled."}
}
if($values.suite_emitter_schema -eq '1'){
    $rows += [pscustomobject]@{Side='local';Stage='Wind emitter state';Result="Check: $($values.suite_emitter_check); emitters: $($values.suite_emitter_count); restores: $($values.suite_emitter_restores); comparisons: $($values.suite_emitter_compares). See raw wind.emitters evidence; no peer outcome inferred."}
}
if($values.suite_wind_gate_schema -eq '1'){
    $gateNames=@{'0'='Not attempted';'1'='Entered';'2'='Lock contended';'3'='Stop flag set'}
    $outcome=$gateNames[$values.suite_wind_gate_outcome];if(!$outcome){$outcome='Unknown'}
    $rows += [pscustomobject]@{Side='local';Stage='Effect capture gate';Result="$outcome; attempts: $($values.suite_wind_gate_attempts); contentions: $($values.suite_wind_gate_contentions); stops: $($values.suite_wind_gate_stops); last rejection: $($values.suite_wind_gate_last_rejection); thread/owner: $($values.suite_wind_gate_thread)/$($values.suite_wind_gate_owner); capture key: $($values.suite_wind_gate_frame). Diagnostic only."}
}
if($values.suite_scoped_clear_schema -in @('1','2')){
    $rows += [pscustomobject]@{Side='local';Stage='Scoped resource clears';Result="Seen: $($values.suite_scoped_clear_seen); unrelated ignored: $($values.suite_scoped_clear_ignored); related: $($values.suite_scoped_clear_related); unknown: $($values.suite_scoped_clear_unknown); last reason: $($values.suite_scoped_clear_reason). Evidence only, not a replay pass."}
    $rows += [pscustomobject]@{Side='peer';Stage='Scoped resource clears';Result='Requires peer raw report; not inferred from transport PASS'}
}
if($values.suite_timing_diagnostics_schema -eq '1' -and [double]$values.suite_timing_qpc_frequency -gt 0){
    $frequency=[double]$values.suite_timing_qpc_frequency
    foreach($cycle in 1..4){
        foreach($phase in @('step','capture','archive','restore','replay','evidence','traces','retire','release','discard','round_cleanup','report_io','callback_gap')){
            $prefix="suite_timing_${cycle}_${phase}"
            $count=[long]$values["${prefix}_count"]
            if($count -le 0){continue}
            $total=1000.0*[double]$values["${prefix}_total_ticks"]/$frequency
            $maximum=1000.0*[double]$values["${prefix}_max_ticks"]/$frequency
            $label=if($cycle -eq 4){'Session'}else{"Cycle $cycle"}
            $rows += [pscustomobject]@{Side='local';Stage="$label timing: $phase";Result=('Count {0}; total {1:F3} ms; max {2:F3} ms' -f $count,$total,$maximum)}
        }
    }
    $rows += [pscustomobject]@{Side='local';Stage='Timing interpretation';Result='Wall time, not GPU/display frame time. Step includes its child phases; do not sum nested timings. Callback gaps may include normal gameplay or loading. Peer timings require peer report.'}
}
if($values.suite_profile -eq '16'){
    $rows += [pscustomobject]@{Side='local';Stage='Full-match reference replay';Result="Cycles: $($values.suite_retained_replay_cycles_completed); native match end: $($values.suite_match_completed); clean retirement: $($values.suite_match_end_clean); spacing: $($values.suite_recording_spacing_frames) frames. Not every-frame prediction."}
    $rows += [pscustomobject]@{Side='local';Stage='Bounded evidence queue';Result="Flushed: $($values.suite_evidence_flushed); wait frames: $($values.suite_evidence_wait_frames); I/O failures: $($values.suite_evidence_io_failures). Full per-cycle files retained."}
}
if($values.ContainsKey('suite_retained_replay_selector_enabled')){
    $rows += [pscustomobject]@{Side='local';Stage='Latest selector restoration';Result="Enabled: $($values.suite_retained_replay_selector_enabled); restored: $($values.suite_retained_replay_selector_restored); endpoint verified: $($values.suite_retained_replay_selector_endpoint_verified); phase/reason: $($values.suite_retained_replay_selector_phase)/$($values.suite_retained_replay_selector_reason); side/offset: $($values.suite_retained_replay_selector_side)/$($values.suite_retained_replay_selector_offset). Earlier cycles remain in raw reports and binary evidence."}
}
$summary=[ordered]@{status=$values.status;reason=$values.suite_reason;restartRequired=($values.suite_retained_replay_restart -eq '1' -or $values.suite_restart_required -eq '1' -or $values.suite_reference_restart_required -eq '1' -or $values.suite_correction_restart_required -eq '1');roundChanges=$values.suite_round_changes;rematchCandidates=$values.suite_rematch_candidates;stages=$rows;windLifecycleEvents=$windEvents;rawReport=$Report}
$base=Join-Path ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Report))) ([IO.Path]::GetFileNameWithoutExtension($Report)+'-summary')
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ($base+'.json') -Encoding UTF8
$output="Status: $($summary.status)`r`nReason: $($summary.reason)`r`nRestart required by failure: $($summary.restartRequired)`r`n"+($rows | Format-Table -AutoSize | Out-String)+"Round changes: $($summary.roundChanges); rematch candidates: $($summary.rematchCandidates)`r`nZero events means not observed, not passed. Restart SC6 before another suite.`r`nRaw report: $Report"
$output | Set-Content -LiteralPath ($base+'.txt') -Encoding UTF8
Write-Host $output
Write-Host "Combined summary: $base.txt"
