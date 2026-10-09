$ErrorActionPreference='Stop'
try {
    $gameProcess=@(Get-Process SoulcaliburVI -ErrorAction Stop)
    if($gameProcess.Count -ne 1){throw 'Expected one SC6 process.'}
    $directory=Join-Path (Split-Path $gameProcess[0].Path -Parent) 'Mods\SC6Rollback\captures'
    Set-Content -LiteralPath (Join-Path $directory 'steam-telemetry-stop.arm') -Value 'STOP' -Encoding ascii
    Write-Host 'Requested telemetry stop. Restart SC6 before another test.'
}catch{Write-Host ('ERROR: '+$_.Exception.Message);exit 1}
