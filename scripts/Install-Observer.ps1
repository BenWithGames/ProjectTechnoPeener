param([string]$GameFolder)
$ErrorActionPreference='Stop'
try {
    if(Get-Process SoulcaliburVI -ErrorAction SilentlyContinue){throw 'Close SC6 before installing.'}
    if(!$GameFolder){$GameFolder=Read-Host 'Paste the SC6 installation folder from Steam > Manage > Browse local files'}
    $GameFolder=(Resolve-Path -LiteralPath $GameFolder.Trim('"')).Path
    $candidates=@($GameFolder,(Join-Path $GameFolder 'SoulcaliburVI\Binaries\Win64'),(Join-Path $GameFolder 'Binaries\Win64'))
    $target=$candidates | Where-Object {Test-Path -LiteralPath (Join-Path $_ 'SoulcaliburVI.exe')} | Select-Object -First 1
    if(!$target){throw 'Could not find SoulcaliburVI.exe in the Win64 folder.'}
    if((Get-FileHash -LiteralPath (Join-Path $target 'SoulcaliburVI.exe')).Hash -ne 'F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553'){throw 'Unsupported SC6 executable build; no files changed.'}
    $payload=Join-Path $PSScriptRoot 'observer-mod'
    foreach($required in @('dwmapi.dll','UE4SS.dll','UE4SS-settings.ini','UE4SS-LICENSE.txt','MINHOOK-LICENSE.txt','main.dll')){if(!(Test-Path -LiteralPath (Join-Path $payload $required) -PathType Leaf)){throw "Incomplete package: $required missing; no files changed."}}
    foreach($name in @('dwmapi.dll','UE4SS.dll')){
        $existing=Join-Path $target $name
        if((Test-Path -LiteralPath $existing) -and (Get-FileHash -LiteralPath $existing).Hash -ne (Get-FileHash -LiteralPath (Join-Path $payload $name)).Hash){throw "Existing $name differs from the verified loader; no files changed."}
    }
    $mod=Join-Path $target 'Mods\SC6Rollback'
    if((Test-Path -LiteralPath $mod) -and @(Get-ChildItem -LiteralPath $mod -Filter '*.arm' -Recurse -File).Count){throw 'Pending capture arm found; remove or review it before installation.'}
    $settingsPath=Join-Path $target 'UE4SS-settings.ini'
    $settings=if(Test-Path -LiteralPath $settingsPath){[IO.File]::ReadAllText($settingsPath)}else{[IO.File]::ReadAllText((Join-Path $payload 'UE4SS-settings.ini'))}
    if($settings -notmatch '(?m)^\s*bUseUObjectArrayCache\s*='){throw 'Unrecognized UE4SS settings; no files changed.'}
    $settings=$settings -replace '(?m)^(\s*bUseUObjectArrayCache\s*=)[^\r\n]*','$1 false'
    $modsPath=Join-Path $target 'Mods\mods.txt'
    $mods=if(Test-Path -LiteralPath $modsPath){[IO.File]::ReadAllText($modsPath)}else{''}
    if($mods -match '(?m)^\s*SC6Rollback\s*:'){$mods=$mods -replace '(?m)^\s*SC6Rollback\s*:[^\r\n]*','SC6Rollback : 1'}else{$mods+="`r`nSC6Rollback : 1`r`n"}
    $dllFolder=Join-Path $mod 'dlls'
    New-Item -ItemType Directory -Force -Path $dllFolder | Out-Null
    $backup=Join-Path $mod ('install-backups\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    foreach($file in @($settingsPath,$modsPath,(Join-Path $dllFolder 'main.dll'))){if(Test-Path -LiteralPath $file){Copy-Item -LiteralPath $file -Destination (Join-Path $backup ((Split-Path $file -Leaf)+'.disabled'))}}
    foreach($name in @('dwmapi.dll','UE4SS.dll','UE4SS-LICENSE.txt','MINHOOK-LICENSE.txt')){Copy-Item -LiteralPath (Join-Path $payload $name) -Destination (Join-Path $target $name) -Force}
    [IO.File]::WriteAllText($settingsPath,$settings)
    [IO.File]::WriteAllText($modsPath,$mods)
    $sourceDll=Join-Path $payload 'main.dll';$targetDll=Join-Path $dllFolder 'main.dll'
    Copy-Item -LiteralPath $sourceDll -Destination $targetDll -Force
    if((Get-FileHash -LiteralPath $sourceDll).Hash -ne (Get-FileHash -LiteralPath $targetDll).Hash){throw 'Installed DLL verification failed.'}
    Write-Host "Installed and verified observer. Backup: $backup"
    Write-Host 'Launch SC6 through Steam. No recording starts until Observe-Inputs.cmd is explicitly run.'
} catch {Write-Host ('ERROR: '+$_.Exception.Message) -ForegroundColor Red;exit 1}
