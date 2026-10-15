param([Parameter(Mandatory=$true)][string]$Report)
$ErrorActionPreference='Stop'
$file=[IO.File]::Open($Report,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
try {$reader=New-Object IO.StreamReader($file);try{$text=$reader.ReadToEnd()}finally{$reader.Dispose()}}finally{$file.Dispose()}
$values=@{}
foreach($line in ($text -split '\r?\n')){if($line -match '^([^=]+)=(.*)$'){$values[$Matches[1]]=$Matches[2]}}
$names=@('Transport','Resources and cleanup','Frame pairs','Boundary agreement','Hold and release','Four snapshots','Three-frame reference replay')
if($values.suite_profile -in @('5','6','7','8','9','10')){$names[6]='Three corrected frames';$names+= 'Changed prediction corrected'}
if($values.suite_profile -in @('6','8')){$names[6]='Three cycles: corrected frames';$names[7]='Three cycles: changed predictions corrected'}
$rows=@()
foreach($side in @('local','peer')){
    for($i=0;$i -lt $names.Count;$i++){
        $bit=1 -shl $i;$result='Not completed'
        if($side -eq 'peer' -and $values.suite_peer_received -ne '1'){$result='Report unavailable'}
        elseif(([int]$values["suite_${side}_fail"] -band $bit) -ne 0){$result='Failed'}
        elseif(([int]$values["suite_${side}_pass"] -band $bit) -ne 0){$result='Passed'}
        elseif(([int]$values["suite_${side}_skip"] -band $bit) -ne 0){$result=if($values.suite_reason -eq 'no_suitable_window_observed'){'Not observed: no suitable input window'}else{'Skipped (not selected or prerequisite failed)'}}
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
$summary=[ordered]@{status=$values.status;reason=$values.suite_reason;restartRequired=($values.suite_restart_required -eq '1' -or $values.suite_reference_restart_required -eq '1' -or $values.suite_correction_restart_required -eq '1');roundChanges=$values.suite_round_changes;rematchCandidates=$values.suite_rematch_candidates;stages=$rows;windLifecycleEvents=$windEvents;rawReport=$Report}
$base=Join-Path ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Report))) ([IO.Path]::GetFileNameWithoutExtension($Report)+'-summary')
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ($base+'.json') -Encoding UTF8
$output="Status: $($summary.status)`r`nReason: $($summary.reason)`r`nRestart required by failure: $($summary.restartRequired)`r`n"+($rows | Format-Table -AutoSize | Out-String)+"Round changes: $($summary.roundChanges); rematch candidates: $($summary.rematchCandidates)`r`nZero events means not observed, not passed. Restart SC6 before another suite.`r`nRaw report: $Report"
$output | Set-Content -LiteralPath ($base+'.txt') -Encoding UTF8
Write-Host $output
Write-Host "Combined summary: $base.txt"
