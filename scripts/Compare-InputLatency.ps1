param([Parameter(Mandatory)][string]$EnabledReport,[Parameter(Mandatory)][string]$DisabledReport)
$ErrorActionPreference='Stop'
function Read-LatencyReport([string]$Path,[int]$ExpectedMode) {
    if([IO.Path]::GetExtension($Path) -eq '.zip') {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip=[IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Path).Path)
        try {
            $entries=@($zip.Entries | Where-Object {$_.Name -like '*-continuous.txt'})
            if($entries.Count -ne 1){throw 'Use a ZIP containing exactly one continuous report, or pass the continuous.txt file directly.'}
            $reader=[IO.StreamReader]::new($entries[0].Open())
            try {$text=$reader.ReadToEnd()} finally {$reader.Dispose()}
        } finally {$zip.Dispose()}
    } else {$text=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $Path).Path)}
    $groups=@{}
    foreach($line in ($text -split '\r?\n')) {
        if($line -match '^(retired_event_\d+_)?input_latency_([^=]+)=(.*)$') {
            $group=if($Matches[1]){$Matches[1]}else{'current'}
            if(!$groups.ContainsKey($group)){$groups[$group]=@{}}
            $groups[$group][$Matches[2]]=$Matches[3]
        }
    }
    if(!$groups.Count){throw 'No input-latency measurements: install the comparison build and record both modes.'}
    $count=0L;$missing=0L;$invalid=0L;$low=0.0;$high=0.0;$waitSum=0.0;$waitCount=0L;$censored=0L;$bypassed=0L
    foreach($g in $groups.Values){
        if($g.schema -ne '1' -or $g.prediction_enabled -ne [string]$ExpectedMode){throw 'Mixed or incorrect prediction mode, or unsupported timing schema.'}
        $n=[long]$g.publication_to_consumption_lower_count
        if($n -ne [long]$g.publication_to_consumption_upper_count){throw 'Inconsistent latency sample counts.'}
        $count+=$n;$missing+=[long]$g.missing;$invalid+=[long]$g.invalid
        $low+=$n*[double]::Parse($g.publication_to_consumption_lower_mean_us,[Globalization.CultureInfo]::InvariantCulture)
        $high+=$n*[double]::Parse($g.publication_to_consumption_upper_mean_us,[Globalization.CultureInfo]::InvariantCulture)
        $w=[long]$g.native_wait_episode_count;$waitCount+=$w
        $waitSum+=$w*[double]::Parse($g.native_wait_episode_mean_us,[Globalization.CultureInfo]::InvariantCulture)
        $censored+=[long]$g.native_wait_open_censored;$bypassed+=[long]$g.native_wait_bypassed_calls
    }
    if(!$count -or $invalid){throw "Timing evidence insufficient: matched=$count invalid=$invalid. Missing=$missing. No latency reduction inferred."}
    [pscustomobject]@{Mode=$(if($ExpectedMode){'Enabled'}else{'Disabled'});Samples=$count;Missing=$missing;
        LowerMeanMs=$low/$count/1000;UpperMeanMs=$high/$count/1000;
        WaitEpisodes=$waitCount;WaitMeanMs=$(if($waitCount){$waitSum/$waitCount/1000}else{0});
        CensoredWaits=$censored;BypassedWaitCalls=$bypassed}
}
$enabled=Read-LatencyReport $EnabledReport 1
$disabled=Read-LatencyReport $DisabledReport 0
@($enabled,$disabled) | Format-Table -AutoSize
$minimum=$disabled.LowerMeanMs-$enabled.UpperMeanMs
$maximum=$disabled.UpperMeanMs-$enabled.LowerMeanMs
Write-Host ('Observed disabled-minus-enabled mean difference: {0:F3} to {1:F3} ms.' -f $minimum,$maximum)
Write-Host 'Native publication-to-input-formation bounds only, not button-to-screen latency. Positive means enabled was faster in these runs. Use the same PC, connection, characters and stage; repeat in reverse order. Different matches are not a controlled proof of causation. Missing samples and unfinished waits are excluded. Both modes retain instrumentation and snapshot overhead.'
