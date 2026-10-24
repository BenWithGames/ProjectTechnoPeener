param([string[]]$Name,[switch]$List,[string]$ReportPath)
$ErrorActionPreference='Stop'
$tests=@((Import-PowerShellDataFile (Join-Path $PSScriptRoot 'NativeTests.psd1')).Tests)
if(@($tests.Name | Sort-Object -Unique).Count -ne $tests.Count){throw 'Duplicate test identity in manifest.'}
foreach($test in $tests){if(!(Test-Path -LiteralPath (Join-Path $PSScriptRoot $test.Script))){throw "Missing test: $($test.Script)"}}
if($Name){
 foreach($requested in $Name){if($requested -notin $tests.Name){throw "Unknown test: $requested. Use -List for valid names."}}
 $tests=@($tests | Where-Object {$_.Name -in $Name})
}
if($List){$tests | ForEach-Object {$_.Name};return}
if(!$ReportPath){$ReportPath=Join-Path (Split-Path $PSScriptRoot -Parent) ('build-native\test-runs\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+$PID+'.json')}
New-Item -ItemType Directory -Force -Path (Split-Path $ReportPath -Parent) | Out-Null
$results=[Collections.Generic.List[object]]::new()
try {
 foreach($test in $tests){
  $timer=[Diagnostics.Stopwatch]::StartNew();$passed=$false;$failure=$null
  try {
   $global:LASTEXITCODE=0
   if($test.Optimized){& (Join-Path $PSScriptRoot $test.Script) -Optimized}else{& (Join-Path $PSScriptRoot $test.Script)}
   if($LASTEXITCODE -ne 0){throw "Test exited with code $LASTEXITCODE"}
   $passed=$true
  } catch {$failure=$_.Exception.Message;throw}
  finally {$timer.Stop();$results.Add([pscustomobject]@{name=$test.Name;passed=$passed;seconds=$timer.Elapsed.TotalSeconds;error=$failure})}
 }
} finally {
 [pscustomobject]@{schema=1;scope=$(if($Name){'targeted'}else{'full'});requested=@($tests.Name);completed=$results.Count;results=@($results.ToArray())} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ReportPath
 Write-Host "Test timing report: $ReportPath"
}
