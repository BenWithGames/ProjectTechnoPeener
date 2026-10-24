$ErrorActionPreference='Stop'
if(Get-Process SoulcaliburVI -ErrorAction SilentlyContinue){throw 'Lifecycle sandbox needs SC6 closed; it never terminates a real game'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root=Join-Path ([IO.Path]::GetTempPath()) ('SC6UpdaterLifecycle-'+[Guid]::NewGuid().ToString('N'))
$win64=Join-Path $root 'Win64';$mod=Join-Path $win64 'Mods\SC6Rollback';$payload=Join-Path $root 'payload'
New-Item -ItemType Directory -Force -Path (Join-Path $mod 'dlls'),(Join-Path $mod 'logs\pass'),$payload | Out-Null
$source=Join-Path $root 'Game.cs';$release=Join-Path $root 'exit-game'
'using System;using System.IO;using System.Threading;class Game {static void Main(string[] a){while(!File.Exists(a[0]))Thread.Sleep(50);}}' | Set-Content -LiteralPath $source
& "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /target:exe "/out:$win64\SoulcaliburVI.exe" $source
if($LASTEXITCODE){throw 'Lifecycle fixture compilation failed'}
$runtime=@{'SoulcaliburVI.exe'=(Get-FileHash -LiteralPath (Join-Path $win64 'SoulcaliburVI.exe')).Hash}
foreach($name in @('UE4SS.dll','dwmapi.dll')){[IO.File]::WriteAllText((Join-Path $win64 $name),'fixture');$runtime[$name]=(Get-FileHash -LiteralPath (Join-Path $win64 $name)).Hash}
foreach($name in @('main.dll','SC6Astral-Updater.ps1','Archive-AutomaticReport.ps1','connection.mp3')){[IO.File]::WriteAllText((Join-Path $payload $name),('new-'+$name))}
@{schema=1;build='20261023000002';runtime=$runtime} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $payload 'UpdateVersion.json')
$zip=Join-Path $root 'package.zip';[IO.Compression.ZipFile]::CreateFromDirectory($payload,$zip)
$now=[DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
$m=@{schema=1;build='20261023000002';issued=$now;expires=$now+3600;url='https://sc6astral-downloads.ben-tmarkham.workers.dev/SC6Rollback-Update-fixture.zip';bytes=(Get-Item -LiteralPath $zip).Length;sha256=(Get-FileHash -LiteralPath $zip).Hash}
$rsa=[Security.Cryptography.RSACryptoServiceProvider]::new(2048);$rsa.PersistKeyInCsp=$false
try {
    $bytes=[Text.Encoding]::UTF8.GetBytes(($m | ConvertTo-Json -Compress))
    $manifest=Join-Path $root 'manifest.json'
    @{schema=1;payload=[Convert]::ToBase64String($bytes);signature=[Convert]::ToBase64String($rsa.SignData($bytes,'SHA256'))} | ConvertTo-Json | Set-Content -LiteralPath $manifest
    $helper=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'SC6Astral-Updater.ps1'))
    $helper=[regex]::Replace($helper,'(?m)^\$script:UpdatePublicKey=.*$',('$script:UpdatePublicKey='''+$rsa.ToXmlString($false)+''''))
    # Test-only transport fixture. Production URL checks/signature/ZIP/install code stay intact.
    $transport='function Receive-UpdateFile([string]$Url,[string]$Destination,[long]$Limit){if($Url.EndsWith("update.json")){Copy-Item -LiteralPath '''+$manifest.Replace("'","''")+''' -Destination $Destination -Force}else{Copy-Item -LiteralPath '''+$zip.Replace("'","''")+''' -Destination $Destination -Force}}'
    $helper=$helper.Replace('if($LibraryOnly){return}',($transport+"`r`n"+'if($LibraryOnly){return}'))
    $helperPath=Join-Path $root 'helper.ps1';[IO.File]::WriteAllText($helperPath,$helper)
    [IO.File]::WriteAllText((Join-Path $mod 'dlls\main.dll'),'old-dll')
    [IO.File]::WriteAllText((Join-Path $mod 'settings.ini'),'keep-settings')
    [IO.File]::WriteAllText((Join-Path $mod 'logs\pass\match.zip'),'keep-report')
    $game=Start-Process -FilePath (Join-Path $win64 'SoulcaliburVI.exe') -ArgumentList ('"'+$release+'"') -WindowStyle Hidden -PassThru
    $arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$helperPath+'" -ModDirectory "'+$mod+'" -GameProcessId '+$game.Id+' -CurrentBuild 20261023000001'
    $worker=Start-Process -FilePath "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $status=Join-Path $mod 'updates\status.txt';$watch=[Diagnostics.Stopwatch]::StartNew()
    while($watch.Elapsed.TotalSeconds -lt 20){if((Test-Path -LiteralPath $status) -and [IO.File]::ReadAllText($status) -eq 'Update ready - installs after exit'){break};Start-Sleep -Milliseconds 100}
    if(!(Test-Path -LiteralPath $status) -or [IO.File]::ReadAllText($status) -ne 'Update ready - installs after exit'){throw "Did not stage update: $root"}
    Start-Sleep -Milliseconds 500
    if($game.HasExited -or [IO.File]::ReadAllText((Join-Path $mod 'dlls\main.dll')) -ne 'old-dll'){throw 'Files changed while game was alive'}
    [IO.File]::WriteAllText($release,'exit');$game.WaitForExit()
    if(!$worker.WaitForExit(20000)){throw 'Installer did not finish after game exit'}
    if([IO.File]::ReadAllText($status) -ne 'Update installed'){throw "Update did not install: $root"}
    if([IO.File]::ReadAllText((Join-Path $mod 'dlls\main.dll')) -ne 'new-main.dll'){throw 'DLL not installed'}
    if([IO.File]::ReadAllText((Join-Path $mod 'settings.ini')) -ne 'keep-settings' -or [IO.File]::ReadAllText((Join-Path $mod 'logs\pass\match.zip')) -ne 'keep-report'){throw 'Settings/reports modified'}
    if(@(Get-ChildItem -LiteralPath (Join-Path $mod 'updates') -Directory -Filter 'stage-*').Count){throw 'Staging files not cleaned'}
    Write-Host 'PASS: end-to-end signed staging in separate helper, no mutation while game alive, install after real process exit, preserved settings/reports, staging cleanup.'
} finally {
    $rsa.Dispose()
    if($game -and !$game.HasExited){[IO.File]::WriteAllText($release,'exit');$game.WaitForExit()}
    if($worker -and !$worker.HasExited){$worker.WaitForExit(3000) | Out-Null;if(!$worker.HasExited){$worker.Kill()}}
}
