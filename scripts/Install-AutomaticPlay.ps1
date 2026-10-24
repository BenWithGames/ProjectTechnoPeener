param([string]$GameFolder,[switch]$ChoosePath,[switch]$VerifyOnly)
$ErrorActionPreference='Stop'
try {
    if($ChoosePath){
        Write-Host 'SC6Rollback installer'
        Write-Host ('Current game path: '+$(if($GameFolder){$GameFolder}else{'not selected'}))
        Write-Host 'Find yours in Steam > Soulcalibur VI > Manage > Browse local files.'
        $selected=Read-Host 'Paste your SC6 installation folder, or press Enter to use the current path'
        if($selected.Trim()){$GameFolder=$selected.Trim().Trim('"')}
    }

    if(!$VerifyOnly -and (Get-Process SoulcaliburVI -ErrorAction SilentlyContinue)){throw 'Close SC6 before installing.'}
    if(!$GameFolder){
        $libraries=@()
        $steam=(Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
        if($steam){
            $libraries+=$steam
            foreach($vdf in @((Join-Path $steam 'steamapps\libraryfolders.vdf'),(Join-Path $steam 'config\libraryfolders.vdf'))){
                if(Test-Path -LiteralPath $vdf){
                    foreach($line in Get-Content -LiteralPath $vdf){
                        if($line -match '"path"\s+"([^"]+)"'){$libraries+=$Matches[1].Replace('\\','\')}
                    }
                }
            }
        }
        $libraries+=@('C:\Program Files (x86)\Steam','D:\SteamLibrary','E:\SteamLibrary','F:\SteamLibrary')
        $installs=@($libraries | Sort-Object -Unique | ForEach-Object {Join-Path $_ 'steamapps\common\SoulcaliburVI'} | Where-Object {Test-Path -LiteralPath (Join-Path $_ 'SoulcaliburVI\Binaries\Win64\SoulcaliburVI.exe')})
        if($installs.Count -ne 1){throw 'Could not uniquely identify SC6. Run Install.cmd -GameFolder "your SC6 installation folder".'}
        $GameFolder=$installs[0]
    }
    Write-Host ('Installing to game folder: '+$GameFolder) -ForegroundColor Cyan
    $GameFolder=(Resolve-Path -LiteralPath $GameFolder.Trim('"')).Path
    $candidates=@($GameFolder,(Join-Path $GameFolder 'SoulcaliburVI\Binaries\Win64'),(Join-Path $GameFolder 'Binaries\Win64'))
    $target=$candidates | Where-Object {Test-Path -LiteralPath (Join-Path $_ 'SoulcaliburVI.exe')} | Select-Object -First 1
    if(!$target){throw 'Could not find SoulcaliburVI.exe in the Win64 folder.'}
    if((Get-FileHash -LiteralPath (Join-Path $target 'SoulcaliburVI.exe')).Hash -ne 'F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553'){throw 'Unsupported SC6 executable build; no files changed.'}
    $payload=Join-Path $PSScriptRoot 'observer-mod'
    $runtimeRequirements=Join-Path $PSScriptRoot 'RuntimeRequirements.json'
    $updateOnly=Test-Path -LiteralPath $runtimeRequirements
    if($updateOnly){
        $requirements=Get-Content -LiteralPath $runtimeRequirements -Raw | ConvertFrom-Json
        if($requirements.schema -ne 1){throw 'Unsupported update manifest; no files changed.'}
        foreach($pair in @(@('dwmapi.dll','dwmapi'),@('UE4SS.dll','ue4ss'))){
            $existing=Join-Path $target $pair[0];$expected=$requirements.($pair[1])
            if($expected -notmatch '^[0-9A-Fa-f]{64}$' -or !(Test-Path -LiteralPath $existing) -or (Get-FileHash -LiteralPath $existing).Hash -ne $expected){throw 'Update requires the matching full runtime installation; no files changed. Use the full package.'}
        }
    }
    $requiredFiles=@('UE4SS-settings.ini','UE4SS-LICENSE.txt','MINHOOK-LICENSE.txt','main.dll','connection.mp3','Archive-AutomaticReport.ps1','SC6Astral-Updater.ps1','UpdateVersion.json')
    if(!$updateOnly){$requiredFiles+=@('dwmapi.dll','UE4SS.dll')}
    foreach($required in $requiredFiles){if(!(Test-Path -LiteralPath (Join-Path $payload $required) -PathType Leaf)){throw "Incomplete package: $required missing; no files changed."}}
    foreach($name in @('dwmapi.dll','UE4SS.dll')){
        if($updateOnly){continue}
        $existing=Join-Path $target $name
        if((Test-Path -LiteralPath $existing) -and (Get-FileHash -LiteralPath $existing).Hash -ne (Get-FileHash -LiteralPath (Join-Path $payload $name)).Hash){throw "Existing $name differs from the verified loader; no files changed."}
    }
    $mod=Join-Path $target 'Mods\SC6Rollback'

    $settingsPath=Join-Path $target 'UE4SS-settings.ini'
    $settings=if(Test-Path -LiteralPath $settingsPath){[IO.File]::ReadAllText($settingsPath)}else{[IO.File]::ReadAllText((Join-Path $payload 'UE4SS-settings.ini'))}
    if($settings -notmatch '(?m)^\s*bUseUObjectArrayCache\s*='){throw 'Unrecognized UE4SS settings; no files changed.'}
    $settings=$settings -replace '(?m)^(\s*bUseUObjectArrayCache\s*=)[^\r\n]*','$1 false'
    $modsPath=Join-Path $target 'Mods\mods.txt'
    $mods=if(Test-Path -LiteralPath $modsPath){[IO.File]::ReadAllText($modsPath)}else{''}
    if($mods -match '(?m)^\s*SC6Rollback\s*:'){$mods=$mods -replace '(?m)^\s*SC6Rollback\s*:[^\r\n]*','SC6Rollback : 1'}else{$mods+="`r`nSC6Rollback : 1`r`n"}
    if($VerifyOnly){Write-Host 'Package and target validation passed; no files changed.';return}
    $dllFolder=Join-Path $mod 'dlls'
    New-Item -ItemType Directory -Force -Path $dllFolder | Out-Null
    $backup=Join-Path $mod ('install-backups\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    foreach($pending in @(Get-ChildItem -LiteralPath $mod -Filter '*.arm' -Recurse -File)){
        Move-Item -LiteralPath $pending.FullName -Destination (Join-Path $backup ([Guid]::NewGuid().ToString('N')+'-'+$pending.Name+'.disabled'))
    }
    foreach($file in @($settingsPath,$modsPath,(Join-Path $dllFolder 'main.dll'))){if(Test-Path -LiteralPath $file){Copy-Item -LiteralPath $file -Destination (Join-Path $backup ((Split-Path $file -Leaf)+'.disabled'))}}
    foreach($name in @('dwmapi.dll','UE4SS.dll','UE4SS-LICENSE.txt','MINHOOK-LICENSE.txt')){if($updateOnly -and $name -in @('dwmapi.dll','UE4SS.dll')){continue};Copy-Item -LiteralPath (Join-Path $payload $name) -Destination (Join-Path $target $name) -Force}
    [IO.File]::WriteAllText($settingsPath,$settings)
    [IO.File]::WriteAllText($modsPath,$mods)
    $sourceDll=Join-Path $payload 'main.dll';$targetDll=Join-Path $dllFolder 'main.dll'
    Copy-Item -LiteralPath $sourceDll -Destination $targetDll -Force
    if((Get-FileHash -LiteralPath $sourceDll).Hash -ne (Get-FileHash -LiteralPath $targetDll).Hash){throw 'Installed DLL verification failed.'}
    Copy-Item -LiteralPath (Join-Path $payload 'connection.mp3') -Destination (Join-Path $mod 'connection.mp3') -Force
    Copy-Item -LiteralPath (Join-Path $payload 'Archive-AutomaticReport.ps1') -Destination $mod -Force
    Copy-Item -LiteralPath (Join-Path $payload 'SC6Astral-Updater.ps1'),(Join-Path $payload 'UpdateVersion.json') -Destination $mod -Force
    foreach($category in @('pass','fails')){New-Item -ItemType Directory -Force -Path (Join-Path $mod ('logs\'+$category)) | Out-Null}
    New-Item -ItemType Directory -Force -Path (Join-Path $mod 'logs') | Out-Null
    [IO.File]::WriteAllText((Join-Path $mod 'automatic-play.enabled'),'automatic-play-v1')
    Write-Host "Installed and verified automatic-play alpha. Backup: $backup"
    Write-Host 'Launch SC6 through Steam. Automatic play is enabled. Reports are saved in Mods\SC6Rollback\logs. No test script is needed.'
} catch {Write-Host ('ERROR: '+$_.Exception.Message) -ForegroundColor Red;exit 1}



