param(
    [Parameter(Mandatory)][ValidateSet(0,1)][int]$Role,
    [Parameter(Mandatory)][string]$Peer,
    [Parameter(Mandatory)][string]$KeyFile,
    [ValidateRange(1,65535)][int]$Port=27860,
    [ValidateRange(1,65535)][int]$PeerPort=27860,
    [ValidateRange(0,500)][int]$DelayMs=0,
    [ValidateRange(0,65535)][int]$DropEvery=0
)
$ErrorActionPreference='Stop'
$probe=Join-Path $PSScriptRoot 'SC6NetworkProbe.exe'
if (!(Test-Path -LiteralPath $probe)) {
    $probe=Join-Path (Split-Path $PSScriptRoot -Parent) 'build-network\SC6NetworkProbe.exe'
}
if (!(Test-Path -LiteralPath $probe)) {throw 'Build the network probe or extract the diagnostic ZIP first.'}
$resolvedKey=(Resolve-Path -LiteralPath $KeyFile).Path
$logFolder=Join-Path (Split-Path $probe -Parent) 'logs'
New-Item -ItemType Directory -Force -Path $logFolder | Out-Null
$log=Join-Path $logFolder ("peer-$Role-"+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.txt')
& $probe --role $Role --peer $Peer --port $Port --peer-port $PeerPort --key-file $resolvedKey --delay-ms $DelayMs --drop-every $DropEvery --timeout-seconds 60 2>&1 | Tee-Object -FilePath $log
$probeExit=$LASTEXITCODE
Write-Output "Diagnostic log: $log"
if($probeExit -ne 0){throw "Network diagnostic failed (exit $probeExit). See the log."}
