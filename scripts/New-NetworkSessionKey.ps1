param([Parameter(Mandatory)][string]$Path)
$ErrorActionPreference='Stop'
$keyBytes=New-Object byte[] 32
$rng=[System.Security.Cryptography.RandomNumberGenerator]::Create()
try {
    $rng.GetBytes($keyBytes)
    $encoded=[Text.Encoding]::ASCII.GetBytes(([BitConverter]::ToString($keyBytes).Replace('-','').ToLowerInvariant()))
    $file=[IO.File]::Open($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try {$file.Write($encoded,0,$encoded.Length)} finally {$file.Dispose()}
} finally {$rng.Dispose();[Array]::Clear($keyBytes,0,$keyBytes.Length)}
Write-Output 'Session key created. Share this file privately with your test partner; do not commit it or include it in logs.'
