param([string]$Config=(Join-Path $PSScriptRoot 'TestSuite.json'),[string]$FriendCode,[ValidateRange(0,2)][int]$PlayerSlot=0,[switch]$Consent)
$ErrorActionPreference='Stop'
try {
    $plan=Get-Content -LiteralPath $Config -Raw | ConvertFrom-Json
    if($plan.profile -eq 'LivePrediction'){
        if(!$FriendCode){$FriendCode=$plan.friendCode}
        if(!$PlayerSlot -and $plan.playerSlot){$PlayerSlot=[int]$plan.playerSlot}
        if($plan.consent -eq $true){$Consent=$true}
        & (Join-Path $PSScriptRoot 'Steam-Inputs.ps1') -Continuous -FriendCode $FriendCode -PlayerSlot $PlayerSlot -Consent:$Consent
        exit $LASTEXITCODE
    }
    if($plan.profile -notin @('Boundary','Hold','Snapshots','ReferenceReplay','PredictionCorrection','RepeatedCorrection','AttackCorrection','RepeatedAttackCorrection','CombatCorrection','CombatCorrectionBaseline','TargetedRetention','RetainedObjectReplay','OwnedRecordingReplay','RepeatedOwnedReplay','MultiRoundReplay','FullMatchReplay') -or ($plan.observeSeconds -isnot [int] -and $plan.observeSeconds -isnot [long]) -or $plan.observeSeconds -lt 0 -or $plan.observeSeconds -gt 180){throw 'Use profile Boundary, Hold, Snapshots, ReferenceReplay, PredictionCorrection, RepeatedCorrection or AttackCorrection and integer observeSeconds from 0 to 180.'}
    if(!$FriendCode){$FriendCode=$plan.friendCode}
    if(!$PlayerSlot -and $plan.playerSlot){$PlayerSlot=[int]$plan.playerSlot}
    if($plan.consent -eq $true){$Consent=$true}
    & (Join-Path $PSScriptRoot 'Steam-Inputs.ps1') -TestSuite -SuiteProfile $plan.profile -ObserveSeconds $plan.observeSeconds -FriendCode $FriendCode -PlayerSlot $PlayerSlot -Consent:$Consent
} catch { Write-Host ('ERROR: '+$_.Exception.Message) -ForegroundColor Red;exit 1 }
