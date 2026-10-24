param([string]$OutputDirectory=(Join-Path $env:USERPROFILE 'Downloads'),[string]$KeyDirectory=(Join-Path $env:LOCALAPPDATA 'SC6AstralPublisher'))
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Security
Add-Type -AssemblyName System.IO.Compression.FileSystem
$project=Split-Path $PSScriptRoot -Parent
$version=Get-Content -LiteralPath (Join-Path $project 'package\Mods\SC6Rollback\UpdateVersion.json') -Raw | ConvertFrom-Json
$build=[string]$version.build
if($build -notmatch '^20\d{12}$'){throw 'Build version missing'}
if($version.dllSHA256 -ne (Get-FileHash -LiteralPath (Join-Path $project 'package\Mods\SC6Rollback\dlls\main.dll')).Hash){throw 'Version metadata does not match compiled DLL'}
$name='SC6Rollback-Update-'+$build.Substring(0,8)+'-'+$build.Substring(8)+'.zip'
$release=Join-Path $OutputDirectory ('SC6Astral-Publish-'+$build)
New-Item -ItemType Directory -Path $release | Out-Null
$stage=Join-Path $release 'payload'
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath (Join-Path $project 'package\Mods\SC6Rollback\dlls\main.dll') -Destination (Join-Path $stage 'main.dll')
Copy-Item -LiteralPath (Join-Path $project 'package\Mods\SC6Rollback\UpdateVersion.json') -Destination $stage
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'SC6Astral-Updater.ps1'),(Join-Path $PSScriptRoot 'Archive-AutomaticReport.ps1'),(Join-Path $project 'assets\connection.mp3') -Destination $stage
$zip=Join-Path $release $name
[IO.Compression.ZipFile]::CreateFromDirectory($stage,$zip)
$now=[DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
$manifest=[ordered]@{schema=1;build=$build;issued=$now;expires=$now+7776000;url=('https://sc6astral-downloads.ben-tmarkham.workers.dev/'+$name);bytes=(Get-Item -LiteralPath $zip).Length;sha256=(Get-FileHash -LiteralPath $zip).Hash}
$payload=[Text.Encoding]::UTF8.GetBytes(($manifest | ConvertTo-Json -Compress))
$rsa=[Security.Cryptography.RSACryptoServiceProvider]::new();$rsa.PersistKeyInCsp=$false
try {
    $private=[Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes((Join-Path $KeyDirectory 'signing-key.dpapi')),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
    $rsa.FromXmlString([Text.Encoding]::UTF8.GetString($private))
    if(!([IO.File]::ReadAllText((Join-Path $PSScriptRoot 'SC6Astral-Updater.ps1'))).Contains($rsa.ToXmlString($false))){throw 'Signing key does not match installed trust anchor'}
    $envelope=[ordered]@{schema=1;payload=[Convert]::ToBase64String($payload);signature=[Convert]::ToBase64String($rsa.SignData($payload,'SHA256'))}
    [IO.File]::WriteAllText((Join-Path $release 'update.json'),($envelope | ConvertTo-Json),[Text.UTF8Encoding]::new($false))
} finally {if($private){[Array]::Clear($private,0,$private.Length)};$rsa.Dispose()}
@"
1. Upload $name to the private sc6astral-updates R2 bucket.
2. Check the Worker download and SHA256: $($manifest.sha256)
3. Publish update.json at the ROOT of BenWithGames/sc6astral-updates on main LAST.
4. GitHub Pages must serve https://benwithgames.github.io/sc6astral-updates/update.json
This manifest expires after 90 days. Regenerate/sign it before expiry (do not edit signed bytes).
The payload directory is build staging, not a manual installer. Friends initially install the separate AutomaticPlay-Alpha package.
Keep the signing key backed up securely; it is DPAPI protected and bound to this Windows account. It is not in these files.
"@ | Set-Content -LiteralPath (Join-Path $release 'PUBLISH.txt')
Write-Output $release
