param(
    [Parameter(Mandatory)][ValidateSet('Add','Remove')][string]$Action,
    [Parameter(Mandatory)][ValidatePattern('^SC6Probe-[a-f0-9]{32}$')][string]$RuleName,
    [ValidateRange(1,65535)][int]$Port=27860
)
$ErrorActionPreference='Stop'
try {
    if ($Action -eq 'Add') {
        $program=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot 'SC6NetworkProbe.exe')).Path
        New-NetFirewallRule -Name $RuleName -DisplayName 'SC6 temporary network diagnostic relay' -Direction Inbound -Action Allow -Protocol UDP -LocalPort $Port -Program $program -Profile Any | Out-Null
    } else {
        Get-NetFirewallRule -Name $RuleName -ErrorAction SilentlyContinue | Remove-NetFirewallRule
    }
    exit 0
} catch { Write-Error $_; exit 1 }
