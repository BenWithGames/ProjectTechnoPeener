param([string]$Config=(Join-Path $PSScriptRoot 'TestSuite.json'))
$ErrorActionPreference='Stop'
try {
    $plan=Get-Content -LiteralPath $Config -Raw | ConvertFrom-Json
    if($plan.profile -notin @('Boundary','Hold','Snapshots','ReferenceReplay','PredictionCorrection') -or ($plan.observeSeconds -isnot [int] -and $plan.observeSeconds -isnot [long]) -or $plan.observeSeconds -lt 0 -or $plan.observeSeconds -gt 180){throw 'Use profile Boundary, Hold, Snapshots, ReferenceReplay or PredictionCorrection and integer observeSeconds from 0 to 180.'}
    & (Join-Path $PSScriptRoot 'Steam-Inputs.ps1') -TestSuite -SuiteProfile $plan.profile -ObserveSeconds $plan.observeSeconds
} catch { Write-Host ('ERROR: '+$_.Exception.Message) -ForegroundColor Red;exit 1 }
