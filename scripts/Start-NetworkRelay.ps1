param(
    [Parameter(Mandatory)][string]$KeyFile,
    [ValidateRange(1,65535)][int]$Port=27860,
    [ValidateRange(3,600)][int]$Seconds=600
)
$ErrorActionPreference='Stop'
$probe=Join-Path $PSScriptRoot 'SC6NetworkProbe.exe'
if (!(Test-Path -LiteralPath $probe)) {
    $probe=Join-Path (Split-Path $PSScriptRoot -Parent) 'build-network\SC6NetworkProbe.exe'
}
if (!(Test-Path -LiteralPath $probe)) {throw 'Extract the diagnostic ZIP first.'}
$resolvedKey=(Resolve-Path -LiteralPath $KeyFile).Path
& $probe --relay-server --port $Port --key-file $resolvedKey --timeout-seconds $Seconds
if($LASTEXITCODE -ne 0){throw 'Relay failed.'}
