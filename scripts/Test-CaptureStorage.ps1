$ErrorActionPreference='Stop'
$root=Join-Path ([IO.Path]::GetTempPath()) ('sc6-storage-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
try {
    foreach($i in 1..5){
        $report=Join-Path $root "steam-inputs-$i.txt"
        [IO.File]::WriteAllText($report,'status=failed')
        (Get-Item $report).LastWriteTime=(Get-Date).AddDays(-10+$i)
        [IO.File]::WriteAllBytes((Join-Path $root "steam-inputs-$i-retained-evidence.bin"),(New-Object byte[] 1024))
    }
    [IO.File]::WriteAllText((Join-Path $root 'retention-pins.txt'),'steam-inputs-2')
    [IO.File]::WriteAllText((Join-Path $root 'unrelated.bin'),'keep')
    & (Join-Path $PSScriptRoot 'Limit-CaptureStorage.ps1') -CaptureDirectory $root -MaxBytes 4500 -MinimumFreeBytes 0
    if(Test-Path (Join-Path $root 'steam-inputs-1-retained-evidence.bin')){throw 'Old binary was not removed'}
    foreach($i in 2..5){if(!(Test-Path (Join-Path $root "steam-inputs-$i-retained-evidence.bin"))){throw 'Protected binary removed'}}
    if(!(Test-Path (Join-Path $root 'unrelated.bin'))){throw 'Unrelated file removed'}
    $blocked=$false
    try {& (Join-Path $PSScriptRoot 'Limit-CaptureStorage.ps1') -CaptureDirectory $root -MaxBytes 1 -MinimumFreeBytes 0} catch {$blocked=$true}
    if(!$blocked){throw 'Protected over-budget captures must block startup'}
    $blocked=$false
    try {& (Join-Path $PSScriptRoot 'Limit-CaptureStorage.ps1') -CaptureDirectory $root -MinimumFreeBytes ([long]::MaxValue)} catch {$blocked=$true}
    if(!$blocked){throw 'Low disk must block startup'}
    'PASS: capture budget, latest failures, pins, unrelated files and disk reserve'
} finally {
    $resolved=[IO.Path]::GetFullPath($root)
    if([IO.Path]::GetDirectoryName($resolved).TrimEnd('\') -ne ([IO.Path]::GetFullPath([IO.Path]::GetTempPath())).TrimEnd('\') -or [IO.Path]::GetFileName($resolved) -notmatch '^sc6-storage-[a-f0-9]{32}$'){throw 'Unsafe test cleanup path'}
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
