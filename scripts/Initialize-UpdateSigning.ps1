param([string]$KeyDirectory=(Join-Path $env:LOCALAPPDATA 'SC6AstralPublisher'))
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Security
New-Item -ItemType Directory -Force -Path $KeyDirectory | Out-Null
$keyPath=Join-Path $KeyDirectory 'signing-key.dpapi'
$rsa=[Security.Cryptography.RSACryptoServiceProvider]::new(3072)
$rsa.PersistKeyInCsp=$false
try {
    if(Test-Path -LiteralPath $keyPath){
        $private=[Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes($keyPath),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
        $rsa.FromXmlString([Text.Encoding]::UTF8.GetString($private))
    } else {
        $private=[Text.Encoding]::UTF8.GetBytes($rsa.ToXmlString($true))
        $protected=[Security.Cryptography.ProtectedData]::Protect($private,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
        [IO.File]::WriteAllBytes($keyPath,$protected)
    }
    $public=$rsa.ToXmlString($false)
    $updater=Join-Path $PSScriptRoot 'SC6Astral-Updater.ps1'
    $text=[IO.File]::ReadAllText($updater)
    if($text.Contains('PUBLIC_KEY_PLACEHOLDER')){[IO.File]::WriteAllText($updater,$text.Replace('PUBLIC_KEY_PLACEHOLDER',$public))}
    elseif(!$text.Contains($public)){throw 'Existing updater trusts a different key; do not silently rotate keys'}
    Write-Host "Signing key protected for this Windows user at $keyPath. Never distribute it."
} finally {if($private){[Array]::Clear($private,0,$private.Length)};$rsa.Dispose()}
