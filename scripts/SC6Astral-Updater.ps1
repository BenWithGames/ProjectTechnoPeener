param([string]$ModDirectory,[int]$GameProcessId,[string]$CurrentBuild,[switch]$LibraryOnly)
$ErrorActionPreference='Stop'
# Public verification key only. The signing key never ships with the mod.
$script:UpdatePublicKey='<RSAKeyValue><Modulus>wb3pGY+KCMfNPEDz92AubbKMAO9nddDpZD5TUuCiXP1PP6XU580SObwrpiwV8yuBpgyVp9YeuuRkBQUJHpJyPojN5CAFksSAgG99BsK3+jRz7aI5kfJbNrTJdItfSlSYjKf644aCnA3yJxFNnkooZJ4h8AjggWA3jPx/8haTMa7g0+/3gNshGyFGzl/rB6PC9sC9bL8TKcUMS2WHXLWMGQRurA0FPd69YRDGXSwYGjUHk1VOl7zrnVzOUVs0S54roYw/P5TjYnLDz3e807xDPyWb3sW1qgaLdBO0luedO3WgfZ9FZGLt2lrK7o50pffw+tVbEy7SanW0aM4DQ9o8WTFmNeGRevMOZAnibtvoY0WOurGSLtZJdy83ty0G2EPgPzKSDKZjhLoz3SoykYqvAKVfEVCTTkF3LX/vDz7m1Gry/8yg/l7AGn/g2boz9tzuERZDzt1t9z0IfolEnRs7sJZYfTnS5/Zlg8wqwtS3rmjgy61HWFhMO4QoA/vuC6kV</Modulus><Exponent>AQAB</Exponent></RSAKeyValue>'
$script:ManifestUrl='https://benwithgames.github.io/sc6astral-updates/update.json'
$script:DownloadPrefix='https://sc6astral-downloads.ben-tmarkham.workers.dev/'
$script:UpdateFiles=@('SC6Astral-Updater.ps1','Archive-AutomaticReport.ps1','connection.mp3','main.dll','UpdateVersion.json')

function Get-UpdateHash([string]$LiteralPath,[string]$Algorithm='SHA256') {
    if($Algorithm -ne 'SHA256'){throw 'Unsupported hash algorithm'}
    $stream=[IO.File]::OpenRead($LiteralPath);$sha=[Security.Cryptography.SHA256]::Create()
    try {return [pscustomobject]@{Hash=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','')}}
    finally {$sha.Dispose();$stream.Dispose()}
}

function Get-SignedUpdate([string]$Json,[string]$Installed,[long]$Now=[DateTimeOffset]::UtcNow.ToUnixTimeSeconds()) {
    if($Json.Length -gt 32768){throw 'Manifest exceeds limit'}
    $envelope=$Json | ConvertFrom-Json
    if($envelope.schema -ne 1){throw 'Unsupported signature envelope'}
    $payload=[Convert]::FromBase64String([string]$envelope.payload)
    $signature=[Convert]::FromBase64String([string]$envelope.signature)
    $rsa=[Security.Cryptography.RSACryptoServiceProvider]::new()
    $rsa.PersistKeyInCsp=$false
    try {
        $rsa.FromXmlString($script:UpdatePublicKey)
        if(!$rsa.VerifyData($payload,'SHA256',$signature)){throw 'Update signature rejected'}
    } finally {$rsa.Dispose()}
    $m=[Text.UTF8Encoding]::new($false,$true).GetString($payload) | ConvertFrom-Json
    if($m.schema -ne 1 -or [string]$m.build -notmatch '^20\d{12}$' -or $Installed -notmatch '^20\d{12}$'){throw 'Invalid build identity'}
    if([long]$m.issued -gt $Now+300 -or [long]$m.expires -le $Now -or [long]$m.expires -le [long]$m.issued){throw 'Expired or future update manifest'}
    if([long]$m.build -lt [long]$Installed){throw 'Older update refused'}
    if([string]$m.sha256 -notmatch '^[A-Fa-f0-9]{64}$' -or [long]$m.bytes -lt 1 -or [long]$m.bytes -gt 67108864){throw 'Invalid package metadata'}
    if([string]$m.url -cnotmatch '^https://sc6astral-downloads\.ben-tmarkham\.workers\.dev/SC6Rollback-Update-[A-Za-z0-9._-]+\.zip$'){throw 'Untrusted download location'}
    return $m
}

function Receive-UpdateFile([string]$Url,[string]$Destination,[long]$Limit) {
    # No redirects, credentials, cookies, unlimited retries, or whole-package buffering.
    [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
    $request=[Net.HttpWebRequest]::Create($Url)
    $request.AllowAutoRedirect=$false;$request.Timeout=15000;$request.ReadWriteTimeout=15000
    $request.UserAgent='SC6Astral-Updater/1';$request.AutomaticDecompression=[Net.DecompressionMethods]::None
    $response=$null;$inputStream=$null;$output=$null
    try {
        $response=$request.GetResponse()
        if([int]$response.StatusCode -ne 200 -or $response.ContentLength -gt $Limit){throw 'Invalid download response'}
        $inputStream=$response.GetResponseStream()
        $output=[IO.File]::Open($Destination,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::None)
        $buffer=New-Object byte[] 65536;$total=0L;$watch=[Diagnostics.Stopwatch]::StartNew()
        while(($count=$inputStream.Read($buffer,0,$buffer.Length)) -gt 0){
            $total+=$count
            if($total -gt $Limit -or $watch.Elapsed.TotalSeconds -gt 120){throw 'Download exceeded size/time limit'}
            $output.Write($buffer,0,$count)
        }
    } finally {if($output){$output.Dispose()};if($inputStream){$inputStream.Dispose()};if($response){$response.Dispose()}}
}

function Assert-PlainDirectory([string]$Path) {
    $item=Get-Item -LiteralPath $Path -Force
    while($item){
        if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Update paths cannot contain junctions or links'}
        $item=$item.Parent
    }
}

function Expand-VerifiedUpdate([string]$Zip,[object]$Manifest,[string]$Stage) {
    if((Get-Item -LiteralPath $Zip).Length -ne [long]$Manifest.bytes -or (Get-UpdateHash -LiteralPath $Zip -Algorithm SHA256).Hash -ne $Manifest.sha256){throw 'Package size/hash rejected'}
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive=[IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        $seen=@{};$total=0L
        foreach($entry in $archive.Entries){
            if($entry.FullName -cnotin $script:UpdateFiles -or $seen.ContainsKey($entry.FullName)){throw 'Unexpected or duplicate ZIP entry'}
            if(($entry.ExternalAttributes -band 0x400) -ne 0 -or (($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000){throw 'ZIP links rejected'}
            $seen[$entry.FullName]=$true;$total+=$entry.Length
            if($entry.Length -le 0 -or $total -gt 67108864){throw 'Expanded package exceeds limit'}
        }
        if($seen.Count -ne $script:UpdateFiles.Count){throw 'Incomplete update package'}
        New-Item -ItemType Directory -Path $Stage | Out-Null
        foreach($entry in $archive.Entries){[IO.Compression.ZipFileExtensions]::ExtractToFile($entry,(Join-Path $Stage $entry.FullName),$false)}
    } finally {$archive.Dispose()}
    $version=Get-Content -LiteralPath (Join-Path $Stage 'UpdateVersion.json') -Raw | ConvertFrom-Json
    if($version.schema -ne 1 -or [string]$version.build -cne [string]$Manifest.build){throw 'Package build identity mismatch'}
}

function Get-UpdateTarget([string]$Root,[string]$Name) {
    if($Name -cnotin $script:UpdateFiles){throw 'Invalid update target'}
    if($Name -eq 'main.dll'){return Join-Path $Root 'dlls\main.dll'}
    return Join-Path $Root $Name
}

function Assert-GameClosed {
    if(Get-Process SoulcaliburVI -ErrorAction SilentlyContinue){throw 'SC6 is still running; update deferred'}
}

function Restore-UpdateBackup([string]$Root,[string]$Backup,[object]$Journal) {
    Assert-GameClosed
    foreach($item in $Journal.files){
        $target=Get-UpdateTarget $Root $item.name
        Assert-PlainDirectory (Split-Path $target -Parent)
        if(Test-Path -LiteralPath $target){if((Get-Item -LiteralPath $target -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked update target rejected'}}
        if($item.existed){
            $source=Join-Path $Backup $item.name
            if((Get-UpdateHash -LiteralPath $source).Hash -ne $item.sha256){throw 'Backup integrity failed'}
            [IO.File]::Copy($source,$target,$true)
        } elseif(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target -Force}
    }
}

function Install-VerifiedUpdate([string]$Root,[string]$Stage,[string]$Backup) {
    Assert-GameClosed
    Assert-PlainDirectory $Root
    $win64=Split-Path (Split-Path $Root -Parent) -Parent
    $version=Get-Content -LiteralPath (Join-Path $Stage 'UpdateVersion.json') -Raw | ConvertFrom-Json
    foreach($name in @('SoulcaliburVI.exe','UE4SS.dll','dwmapi.dll')){
        $expected=$version.runtime.$name
        if($expected -notmatch '^[0-9A-Fa-f]{64}$' -or (Get-UpdateHash -LiteralPath (Join-Path $win64 $name)).Hash -ne $expected){throw 'Installed game/runtime differs; use a full installer'}
    }
    New-Item -ItemType Directory -Path $Backup | Out-Null
    $items=@()
    foreach($name in $script:UpdateFiles){
        $target=Get-UpdateTarget $Root $name
        Assert-PlainDirectory (Split-Path $target -Parent)
        $exists=Test-Path -LiteralPath $target
        $hash=$null
        if($exists){
            if((Get-Item -LiteralPath $target -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked update target rejected'}
            Copy-Item -LiteralPath $target -Destination (Join-Path $Backup $name)
            $hash=(Get-UpdateHash -LiteralPath (Join-Path $Backup $name)).Hash
        }
        $items+=@{name=$name;existed=$exists;sha256=$hash}
    }
    $journal=@{schema=1;files=$items}
    $journal | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Backup 'transaction.json') -Encoding UTF8
    try {
        foreach($name in $script:UpdateFiles){
            Assert-GameClosed
            $target=Get-UpdateTarget $Root $name;$source=Join-Path $Stage $name
            $temporary=$target+'.updating'
            if(Test-Path -LiteralPath $temporary){throw 'Stale update temporary file; manual repair required'}
            [IO.File]::Copy($source,$temporary,$false)
            try {
                if(Test-Path -LiteralPath $target){[IO.File]::Replace($temporary,$target,[NullString]::Value)}else{[IO.File]::Move($temporary,$target)}
            } finally {if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary -Force}}
            if((Get-UpdateHash -LiteralPath $target).Hash -ne (Get-UpdateHash -LiteralPath $source).Hash){throw 'Installed file verification failed'}
        }
        [IO.File]::WriteAllText((Join-Path $Backup 'complete'),'verified')
    } catch {
        Restore-UpdateBackup $Root $Backup $journal
        [IO.File]::WriteAllText((Join-Path $Backup 'complete'),'rolled-back')
        throw
    }
}

function Set-UpdateStatus([string]$Text) {
    [IO.File]::WriteAllText((Join-Path $script:UpdateDirectory 'status.txt'),$Text,[Text.Encoding]::ASCII)
}

function Remove-UpdateStage([string]$Root,[string]$Stage) {
    $rootPath=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    $stagePath=[IO.Path]::GetFullPath($Stage)
    if((Split-Path $stagePath -Parent) -ne $rootPath -or (Split-Path $stagePath -Leaf) -notmatch '^stage-[a-f0-9]{32}$'){throw 'Invalid staging cleanup path'}
    if(!(Test-Path -LiteralPath $stagePath)){return}
    Assert-PlainDirectory $stagePath
    # Never recurse through a supplied path or remove unexpected user data.
    foreach($file in Get-ChildItem -LiteralPath $stagePath -Force){
        if($file.PSIsContainer -or $file.Name -cnotin $script:UpdateFiles -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Unexpected staging contents'}
    }
    foreach($file in Get-ChildItem -LiteralPath $stagePath -Force){Remove-Item -LiteralPath $file.FullName -Force}
    Remove-Item -LiteralPath $stagePath
}

if($LibraryOnly){return}
try {
    $ModDirectory=(Resolve-Path -LiteralPath $ModDirectory).Path
    Assert-PlainDirectory $ModDirectory
    if($CurrentBuild -notmatch '^20\d{12}$'){throw 'Invalid installed build'}
    $game=Get-Process -Id $GameProcessId -ErrorAction Stop
    if($game.ProcessName -ne 'SoulcaliburVI'){throw 'Wrong parent process'}
    # Force acquisition of a process handle: PID reuse must not extend the wait.
    $null=$game.Handle
    $script:UpdateDirectory=Join-Path $ModDirectory 'updates'
    New-Item -ItemType Directory -Force -Path $script:UpdateDirectory | Out-Null
    Assert-PlainDirectory $script:UpdateDirectory
    try {$lock=[IO.File]::Open((Join-Path $script:UpdateDirectory 'updater.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}catch{return} # Another helper owns this installation; do not overwrite its status.
    foreach($oldStage in Get-ChildItem -LiteralPath $script:UpdateDirectory -Directory -Filter 'stage-*'){Remove-UpdateStage $script:UpdateDirectory $oldStage.FullName}
    $pending=@(Get-ChildItem -LiteralPath $script:UpdateDirectory -Directory -Filter 'backup-*' | Where-Object {(Test-Path -LiteralPath (Join-Path $_.FullName 'transaction.json')) -and !(Test-Path -LiteralPath (Join-Path $_.FullName 'complete'))})
    if($pending.Count){
        Set-UpdateStatus 'Repair pending - close SC6'
        $game.WaitForExit()
        foreach($backup in $pending){
            Restore-UpdateBackup $ModDirectory $backup.FullName (Get-Content -LiteralPath (Join-Path $backup.FullName 'transaction.json') -Raw | ConvertFrom-Json)
            [IO.File]::WriteAllText((Join-Path $backup.FullName 'complete'),'recovered')
        }
        Set-UpdateStatus 'Previous update recovered';return
    }
    $next=[DateTime]::MinValue;$last=[DateTime]::MinValue
    while(!$game.HasExited){
        $requestPath=Join-Path $script:UpdateDirectory 'check.request'
        $manual=Test-Path -LiteralPath $requestPath
        if([DateTime]::UtcNow -ge $next -or ($manual -and ([DateTime]::UtcNow-$last).TotalSeconds -ge 30)){
            # Consume only when accepted. The overlay's cooldown starts at the click,
            # slightly before this polling helper begins its own cooldown.
            # An early request remains queued instead of silently losing the click.
            if($manual){Remove-Item -LiteralPath $requestPath -Force}
            $last=[DateTime]::UtcNow;$next=$last.AddMinutes(15)
            $stage=$null;$freshStage=$null
            try {
                if(Test-Path -LiteralPath (Join-Path $ModDirectory 'updates.disabled')){Set-UpdateStatus 'Updates disabled';continue}
                Set-UpdateStatus 'Checking for updates...'
                $manifestFile=Join-Path $script:UpdateDirectory 'manifest.json'
                Receive-UpdateFile $script:ManifestUrl $manifestFile 32768
                $manifest=Get-SignedUpdate ([IO.File]::ReadAllText($manifestFile)) $CurrentBuild
                if([long]$manifest.build -eq [long]$CurrentBuild){Set-UpdateStatus 'On the latest build'}else{
                    Set-UpdateStatus 'Downloading verified update...'
                    $zip=Join-Path $script:UpdateDirectory 'download.zip'
                    Receive-UpdateFile $manifest.url $zip ([long]$manifest.bytes)
                    $stage=Join-Path $script:UpdateDirectory ('stage-'+[Guid]::NewGuid().ToString('N'))
                    Expand-VerifiedUpdate $zip $manifest $stage
                    Set-UpdateStatus 'Update ready - installs after exit'
                    $game.WaitForExit()
                    # Verify signed metadata, package and extracted bytes again after waiting.
                    $manifest=Get-SignedUpdate ([IO.File]::ReadAllText($manifestFile)) $CurrentBuild
                    $freshStage=Join-Path $script:UpdateDirectory ('stage-'+[Guid]::NewGuid().ToString('N'))
                    Expand-VerifiedUpdate $zip $manifest $freshStage
                    Install-VerifiedUpdate $ModDirectory $freshStage (Join-Path $script:UpdateDirectory ('backup-'+[DateTime]::UtcNow.ToString('yyyyMMddHHmmss')+'-'+[Guid]::NewGuid().ToString('N')))
                    Set-UpdateStatus 'Update installed';break
                }
            } catch {
                if($_.Exception.Message -eq 'Older update refused'){
                    Set-UpdateStatus 'On the latest build'
                    continue # Keep downgrade rejection; an older published build is not a check failure.
                }
                Set-UpdateStatus 'Update unavailable - retry later'
                # Bounded local diagnostics; never upload anything.
                [IO.File]::WriteAllText((Join-Path $script:UpdateDirectory 'last-error.txt'),$_.Exception.Message)
            } finally {
                foreach($temporaryStage in @($stage,$freshStage)){if($temporaryStage){Remove-UpdateStage $script:UpdateDirectory $temporaryStage}}
                $download=Join-Path $script:UpdateDirectory 'download.zip'
                if(Test-Path -LiteralPath $download){Remove-Item -LiteralPath $download -Force}
            }
        }
        Start-Sleep -Milliseconds 1000
    }
} catch {
    if($script:UpdateDirectory){try {Set-UpdateStatus 'Updater unavailable';[IO.File]::WriteAllText((Join-Path $script:UpdateDirectory 'last-error.txt'),$_.Exception.Message)}catch{}}
} finally {if($lock){$lock.Dispose()}}
