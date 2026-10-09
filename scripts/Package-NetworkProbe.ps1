$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$build=Join-Path $project 'build-network'
$exe=Join-Path $build 'SC6NetworkProbe.exe'
if (!(Test-Path -LiteralPath $exe)) {throw 'Run Build-NetworkProbe.ps1 first.'}
$destination=Join-Path $build 'diagnostic'
New-Item -ItemType Directory -Force -Path $destination | Out-Null
# Explicit allowlist: never include session keys, logs, game captures or dependencies.
Copy-Item -LiteralPath $exe -Destination $destination -Force
foreach ($name in @('Start-NetworkProbe.ps1','New-NetworkSessionKey.ps1','Start-NetworkRelay.ps1','Network-Test.cmd','Network-Test.ps1','Network-Firewall.ps1','Observe-Inputs.cmd','Install-Observer.cmd','Install-Observer.ps1')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $destination -Force
}
Copy-Item -LiteralPath (Join-Path $project 'docs\NETWORK_DIAGNOSTIC.md') -Destination (Join-Path $destination 'README.md') -Force
Copy-Item -LiteralPath (Join-Path $project 'docs\PARTNER_RELAY_SETUP.md') -Destination $destination -Force
Copy-Item -LiteralPath (Join-Path $project 'docs\INPUT_OBSERVATION.md') -Destination $destination -Force
$payload=Join-Path $destination 'observer-mod'
New-Item -ItemType Directory -Force -Path $payload | Out-Null
$allowed=@('dwmapi.dll','UE4SS.dll','UE4SS-settings.ini','UE4SS-LICENSE.txt','MINHOOK-LICENSE.txt','main.dll')
foreach($name in $allowed){
    $source=if($name -eq 'main.dll'){Join-Path $project 'package\Mods\SC6Rollback\dlls\main.dll'}else{Join-Path $project ('package\'+$name)}
    Copy-Item -LiteralPath $source -Destination (Join-Path $payload $name) -Force
}
if(@(Get-ChildItem -LiteralPath $payload -Force | Where-Object {$_.PSIsContainer -or $_.Name -notin $allowed}).Count){throw 'Unexpected observer payload contents; archive not created.'}
$files=@('SC6NetworkProbe.exe','Start-NetworkProbe.ps1','New-NetworkSessionKey.ps1','Start-NetworkRelay.ps1','Network-Test.cmd','Network-Test.ps1','Network-Firewall.ps1','Observe-Inputs.cmd','Install-Observer.cmd','Install-Observer.ps1','README.md','PARTNER_RELAY_SETUP.md','INPUT_OBSERVATION.md','observer-mod') | ForEach-Object {Join-Path $destination $_}
$zip=Join-Path $build 'SC6NetworkDiagnostic.zip'
Compress-Archive -LiteralPath $files -DestinationPath $zip -Force
Get-FileHash -LiteralPath $zip -Algorithm SHA256
