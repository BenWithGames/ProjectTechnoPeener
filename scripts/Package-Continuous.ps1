param([string]$OutputDirectory=(Join-Path $env:USERPROFILE 'Downloads'))
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$name='SC6Rollback-SourceGuard-'+(Get-Date -Format 'yyyyMMdd-HHmmss')
$destination=Join-Path $project ('build-network\'+$name)
$payload=Join-Path $destination 'observer-mod'
$null=New-Item -ItemType Directory -Path $payload
foreach($file in @('Steam-Continuous.cmd','Steam-Inputs.ps1','Collect-RollbackReport.cmd','Collect-RollbackReport.ps1','Install-Observer.cmd','Install-Observer.ps1')){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $destination
}
$installer=Join-Path $destination 'Install-Observer.ps1'
$text=[IO.File]::ReadAllText($installer).Replace('Installed and verified observer.','Installed and verified experimental continuous build.').Replace('No recording starts until Observe-Inputs.cmd is explicitly run.','Follow README.txt and run Steam-Continuous.cmd in a consenting private session.')
[IO.File]::WriteAllText($installer,$text)
foreach($file in @('dwmapi.dll','UE4SS.dll','UE4SS-settings.ini','UE4SS-LICENSE.txt','MINHOOK-LICENSE.txt','main.dll')){
    $source=if($file -eq 'main.dll'){Join-Path $project 'package\Mods\SC6Rollback\dlls\main.dll'}else{Join-Path $project ('package\'+$file)}
    Copy-Item -LiteralPath $source -Destination $payload
}
@'
SC6Rollback - Experimental general combat restoration

This build uses a character-independent snapshot restore path, including
changing input envelopes, exact live-endpoint checks, native loading and
post-load verification. It does not reclassify unknown moves as tested moves.
All resource, topology, blend, camera and serializer guards remain. A window
outside those guards still stops the test; universal move/stage coverage is
NOT established. Native network waits remain; no latency improvement claimed.

Both PCs:
1. Close SC6, extract this ZIP and run Install-Observer.cmd.
2. Launch SC6 and join the private lobby. NO warmup match.
3. Run Steam-Continuous.cmd, enter the OTHER player's friend code and your
   assigned P1/P2 slot, consent, and press Enter together.
4. Start the private match and play normally. Initial boundary agreement
   precedes activation; it is not necessarily active before FIGHT.
5. This ONE-ROUND experiment stops on fault, round change, or capture limit.
   It may freeze simulation after a stop. Close BOTH games afterward.
6. Send BOTH SC6Rollback-Report-*.zip files from Downloads. If automatic
   collection fails, run Collect-RollbackReport.cmd with the printed report.

Inspect continuous_corrections_completed, continuous_replayed_frames,
continuous_native_restore_invoked and continuous_restore_* diagnostics.
A transport pass or normal gameplay is not a rollback pass.
Local mocked save/load tests passed; live general combat validation is pending.
Private consenting matches only. No automatic upload.
'@ | Set-Content -LiteralPath (Join-Path $destination 'README.txt') -Encoding ascii
$hash=(Get-FileHash -LiteralPath (Join-Path $payload 'main.dll')).Hash
if($hash -ne (Get-FileHash -LiteralPath (Join-Path $project 'package\Mods\SC6Rollback\dlls\main.dll')).Hash){throw 'DLL hash mismatch'}
$null=New-Item -ItemType Directory -Force -Path $OutputDirectory
$zip=Join-Path $OutputDirectory ($name+'.zip')
Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $zip
[pscustomobject]@{Folder=$destination;Zip=$zip;DllSHA256=$hash}
