param([Parameter(Mandatory=$true)][string]$Report)
$ErrorActionPreference='Stop'
$file=[IO.File]::Open($Report,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
try {$reader=New-Object IO.StreamReader($file);try{$text=$reader.ReadToEnd()}finally{$reader.Dispose()}}finally{$file.Dispose()}
$values=@{}
foreach($line in ($text -split '\r?\n')){if($line -match '^([^=]+)=(.*)$'){$values[$Matches[1]]=$Matches[2]}}
$names=@('Transport','Resources and cleanup','Frame pairs','Boundary agreement','Hold and release','Four snapshots','Three-frame reference replay')
if($values.suite_profile -eq '5'){$names[6]='Three corrected frames';$names+= 'Changed prediction corrected'}
$rows=@()
foreach($side in @('local','peer')){
    for($i=0;$i -lt $names.Count;$i++){
        $bit=1 -shl $i;$result='Not completed'
        if($side -eq 'peer' -and $values.suite_peer_received -ne '1'){$result='Report unavailable'}
        elseif(([int]$values["suite_${side}_fail"] -band $bit) -ne 0){$result='Failed'}
        elseif(([int]$values["suite_${side}_pass"] -band $bit) -ne 0){$result='Passed'}
        elseif(([int]$values["suite_${side}_skip"] -band $bit) -ne 0){$result='Skipped (not selected or prerequisite failed)'}
        $rows += [pscustomobject]@{Side=$side;Stage=$names[$i];Result=$result}
    }
}
$rows += [pscustomobject]@{Side='both';Stage='Continuous rollback';Result='Unavailable in this runner'}
$summary=[ordered]@{status=$values.status;reason=$values.suite_reason;restartRequired=($values.suite_restart_required -eq '1' -or $values.suite_reference_restart_required -eq '1' -or $values.suite_correction_restart_required -eq '1');roundChanges=$values.suite_round_changes;rematchCandidates=$values.suite_rematch_candidates;stages=$rows;rawReport=$Report}
$base=Join-Path ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Report))) ([IO.Path]::GetFileNameWithoutExtension($Report)+'-summary')
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ($base+'.json') -Encoding UTF8
$output="Status: $($summary.status)`r`nReason: $($summary.reason)`r`nRestart required by failure: $($summary.restartRequired)`r`n"+($rows | Format-Table -AutoSize | Out-String)+"Round changes: $($summary.roundChanges); rematch candidates: $($summary.rematchCandidates)`r`nZero events means not observed, not passed. Restart SC6 before another suite.`r`nRaw report: $Report"
$output | Set-Content -LiteralPath ($base+'.txt') -Encoding UTF8
Write-Host $output
Write-Host "Combined summary: $base.txt"
