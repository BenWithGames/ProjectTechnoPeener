$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'SC6Astral-Updater.ps1') -LibraryOnly
Add-Type -AssemblyName System.IO.Compression.FileSystem
$testRoot=Join-Path ([IO.Path]::GetTempPath()) ('SC6UpdaterTest-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$rsa=[Security.Cryptography.RSACryptoServiceProvider]::new(2048);$rsa.PersistKeyInCsp=$false
$script:UpdatePublicKey=$rsa.ToXmlString($false)
$script:checks=0
function Assert($Condition,[string]$Message){if(!$Condition){throw $Message};$script:checks++}
function Reject([scriptblock]$Action,[string]$Message){$rejected=$false;try {& $Action | Out-Null}catch{$rejected=$true};Assert $rejected $Message}
function Sign($Manifest){$bytes=[Text.Encoding]::UTF8.GetBytes(($Manifest | ConvertTo-Json -Compress -Depth 5));return (@{schema=1;payload=[Convert]::ToBase64String($bytes);signature=[Convert]::ToBase64String($rsa.SignData($bytes,'SHA256'))} | ConvertTo-Json -Compress)}
function Zip-Stage([string]$Path){$zip=Join-Path $testRoot ([Guid]::NewGuid().ToString('N')+'.zip');[IO.Compression.ZipFile]::CreateFromDirectory($Path,$zip);return $zip}
function Meta([string]$Zip){return @{build='20261023000002';bytes=(Get-Item -LiteralPath $Zip).Length;sha256=(Get-FileHash -LiteralPath $Zip).Hash}}
function Extract([string]$Zip,$Manifest){Expand-VerifiedUpdate $Zip $Manifest (Join-Path $testRoot ([Guid]::NewGuid().ToString('N')))}
try {
    $now=[DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
    $m=@{schema=1;build='20261023000002';issued=$now;expires=$now+1000;url='https://sc6astral-downloads.ben-tmarkham.workers.dev/SC6Rollback-Update-test.zip';bytes=123;sha256=('A'*64)}
    $json=Sign $m
    Assert ((Get-SignedUpdate $json '20261023000001').build -eq $m.build) 'Valid signature rejected'
    Assert ((Get-SignedUpdate $json $m.build).build -eq $m.build) 'Current version rejected'
    Reject {Get-SignedUpdate $json '20261023000003'} 'Downgrade accepted'
    $bad=$json | ConvertFrom-Json;$raw=[Convert]::FromBase64String($bad.payload);$raw[0]=$raw[0] -bxor 1;$bad.payload=[Convert]::ToBase64String($raw)
    Reject {Get-SignedUpdate ($bad | ConvertTo-Json) '20261023000001'} 'Tampered signature accepted'
    $other=[Security.Cryptography.RSACryptoServiceProvider]::new(2048);$other.PersistKeyInCsp=$false
    $bad=$json | ConvertFrom-Json;$bad.signature=[Convert]::ToBase64String($other.SignData([Convert]::FromBase64String($bad.payload),'SHA256'));$other.Dispose()
    Reject {Get-SignedUpdate ($bad | ConvertTo-Json) '20261023000001'} 'Wrong publisher accepted'
    foreach($url in @('http://sc6astral-downloads.ben-tmarkham.workers.dev/SC6Rollback-Update-test.zip','https://evil.example/SC6Rollback-Update-test.zip','https://sc6astral-downloads.ben-tmarkham.workers.dev/../bad.zip','https://sc6astral-downloads.ben-tmarkham.workers.dev/SC6Rollback-Update-test.zip?x=1')){
        $changed=$m.Clone();$changed.url=$url;Reject {Get-SignedUpdate (Sign $changed) '20261023000001'} 'Unsafe URL accepted'
    }
    $changed=$m.Clone();$changed.expires=$now-1;Reject {Get-SignedUpdate (Sign $changed) '20261023000001'} 'Expired manifest accepted'
    $changed=$m.Clone();$changed.issued=$now+600;Reject {Get-SignedUpdate (Sign $changed) '20261023000001'} 'Future manifest accepted'
    $changed=$m.Clone();$changed.bytes=67108865;Reject {Get-SignedUpdate (Sign $changed) '20261023000001'} 'Oversize accepted'
    $changed=$m.Clone();$changed.sha256='invalid';Reject {Get-SignedUpdate (Sign $changed) '20261023000001'} 'Invalid hash accepted'

    $win64=Join-Path $testRoot 'Win64';$mod=Join-Path $win64 'Mods\SC6Rollback';$stage=Join-Path $testRoot 'payload'
    New-Item -ItemType Directory -Force -Path (Join-Path $mod 'dlls'),(Join-Path $mod 'logs\pass'),$stage | Out-Null
    $runtime=@{}
    foreach($name in @('SoulcaliburVI.exe','UE4SS.dll','dwmapi.dll')){[IO.File]::WriteAllText((Join-Path $win64 $name),'fake-runtime');$runtime[$name]=(Get-FileHash -LiteralPath (Join-Path $win64 $name)).Hash}
    foreach($name in $script:UpdateFiles){[IO.File]::WriteAllText((Get-UpdateTarget $mod $name),('old-'+$name));[IO.File]::WriteAllText((Join-Path $stage $name),('new-'+$name))}
    @{schema=1;build='20261023000002';runtime=$runtime} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'UpdateVersion.json')
    [IO.File]::WriteAllText((Join-Path $mod 'settings.ini'),'custom');[IO.File]::WriteAllText((Join-Path $mod 'logs\pass\match.zip'),'keep-log')
    $zip=Zip-Stage $stage;$meta=Meta $zip
    Extract $zip $meta;Assert $true 'Valid archive'
    $badMeta=$meta.Clone();$badMeta.sha256='0'*64;Reject {Extract $zip $badMeta} 'Hash mismatch accepted'
    $badMeta=$meta.Clone();$badMeta.bytes++;Reject {Extract $zip $badMeta} 'Size mismatch accepted'
    $badMeta=$meta.Clone();$badMeta.build='20261023000003';Reject {Extract $zip $badMeta} 'Wrong build accepted'
    foreach($entryName in @('../escape.txt','settings.ini','main.dll','dlls/main.dll','C:/escape.txt')){
        $badZip=Join-Path $testRoot ([Guid]::NewGuid().ToString('N')+'.zip');Copy-Item -LiteralPath $zip -Destination $badZip
        $a=[IO.Compression.ZipFile]::Open($badZip,[IO.Compression.ZipArchiveMode]::Update);$e=$a.CreateEntry($entryName);$s=[IO.StreamWriter]::new($e.Open());$s.Write('bad');$s.Dispose();$a.Dispose()
        Reject {Extract $badZip (Meta $badZip)} 'ZIP allowlist/duplicate bypass'
    }
    $missing=Join-Path $testRoot 'missing.zip';$a=[IO.Compression.ZipFile]::Open($missing,[IO.Compression.ZipArchiveMode]::Create);$a.Dispose()
    Reject {Extract $missing (Meta $missing)} 'Incomplete ZIP accepted'
    # Unit sandbox only: never invoke actual game or touch the real installation.
    function Assert-GameClosed {}
    Install-VerifiedUpdate $mod $stage (Join-Path $testRoot 'backup-success')
    foreach($name in $script:UpdateFiles){Assert ((Get-FileHash -LiteralPath (Get-UpdateTarget $mod $name)).Hash -eq (Get-FileHash -LiteralPath (Join-Path $stage $name)).Hash) 'Installed file differs'}
    Assert ([IO.File]::ReadAllText((Join-Path $mod 'settings.ini')) -eq 'custom') 'Settings changed'
    Assert ([IO.File]::ReadAllText((Join-Path $mod 'logs\pass\match.zip')) -eq 'keep-log') 'Logs changed'
    foreach($name in $script:UpdateFiles){[IO.File]::WriteAllText((Get-UpdateTarget $mod $name),('old-'+$name))}
    $script:calls=0
    function Assert-GameClosed {$script:calls++;if($script:calls -eq 4){throw 'Injected failure after partial replacement'}}
    Reject {Install-VerifiedUpdate $mod $stage (Join-Path $testRoot 'backup-failure')} 'Injected failure ignored'
    foreach($name in $script:UpdateFiles){Assert ([IO.File]::ReadAllText((Get-UpdateTarget $mod $name)) -eq ('old-'+$name)) 'Rollback lost original file'}
    function Assert-GameClosed {throw 'Game running'}
    Reject {Install-VerifiedUpdate $mod $stage (Join-Path $testRoot 'backup-running')} 'Installed during game'
    Assert (!(Test-Path -LiteralPath (Join-Path $testRoot 'backup-running'))) 'Wrote backup while game running'
    function Assert-GameClosed {}
    [IO.File]::WriteAllText((Join-Path $win64 'UE4SS.dll'),'wrong runtime')
    Reject {Install-VerifiedUpdate $mod $stage (Join-Path $testRoot 'backup-runtime')} 'Mismatched runtime accepted'
    Reject {Remove-UpdateStage $testRoot $mod} 'Cleanup escaped staging directory'
    $cleanup=Join-Path $testRoot ('stage-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $cleanup | Out-Null
    Copy-Item -LiteralPath (Join-Path $stage 'main.dll') -Destination $cleanup
    Remove-UpdateStage $testRoot $cleanup;Assert (!(Test-Path -LiteralPath $cleanup)) 'Stage not cleaned'
    Reject {Get-UpdateTarget $mod '..\settings.ini'} 'Target path traversal accepted'
    $recovery=Join-Path $testRoot 'backup-success'
    $journal=Get-Content -LiteralPath (Join-Path $recovery 'transaction.json') -Raw | ConvertFrom-Json
    [IO.File]::WriteAllText((Join-Path $mod 'dlls\main.dll'),'interrupted-write')
    Restore-UpdateBackup $mod $recovery $journal
    Assert ([IO.File]::ReadAllText((Join-Path $mod 'dlls\main.dll')) -eq 'old-main.dll') 'Interrupted transaction recovery failed'
    Write-Host "Signed updater: $script:checks checks passed (signature, trust, expiry, downgrade, archive boundaries, runtime, preservation, rollback, game-exit gate)."
} finally {$rsa.Dispose()}
# Keep test sandbox for inspection; it contains only synthetic files, never a private signing key.
