$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$dir=Join-Path $root 'build-native\input-latency-script-test'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$base=@'
input_latency_schema=1
input_latency_prediction_enabled=MODE
input_latency_publication_to_consumption_lower_count=10
input_latency_publication_to_consumption_upper_count=10
input_latency_publication_to_consumption_lower_mean_us=1000
input_latency_publication_to_consumption_upper_mean_us=2000
input_latency_missing=2
input_latency_invalid=0
input_latency_native_wait_episode_count=2
input_latency_native_wait_episode_mean_us=3000
input_latency_native_wait_open_censored=1
input_latency_native_wait_bypassed_calls=0
'@
$on=Join-Path $dir 'enabled-continuous.txt';$off=Join-Path $dir 'disabled-continuous.txt'
$base.Replace('MODE','1') | Set-Content -LiteralPath $on -Encoding ascii
$base.Replace('MODE','0').Replace('mean_us=1000','mean_us=4000').Replace('mean_us=2000','mean_us=5000') | Set-Content -LiteralPath $off -Encoding ascii
& (Join-Path $PSScriptRoot 'Compare-InputLatency.ps1') -EnabledReport $on -DisabledReport $off
$rejected=$false
try{& (Join-Path $PSScriptRoot 'Compare-InputLatency.ps1') -EnabledReport $off -DisabledReport $on}catch{$rejected=$true}
if(!$rejected){throw 'Mode swap was not rejected'}
$base.Replace('MODE','1').Replace('invalid=0','invalid=1') | Set-Content -LiteralPath $on -Encoding ascii
$rejected=$false
try{& (Join-Path $PSScriptRoot 'Compare-InputLatency.ps1') -EnabledReport $on -DisabledReport $off}catch{$rejected=$true}
if(!$rejected){throw 'Invalid measurements were not rejected'}
foreach($file in @('Steam-TestSuite.ps1','Steam-Inputs.ps1','Compare-InputLatency.ps1','Package-TestSuite.ps1')){
 $tokens=$null;$errors=$null
 [void][Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $file),[ref]$tokens,[ref]$errors)
 if($errors.Count){throw ($errors | Out-String)}
}
Write-Host 'PASS: latency comparison modes, insufficient evidence and launcher syntax'
