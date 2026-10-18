param([Parameter(Mandatory=$true)][string]$CaptureDirectory,
      [long]$MaxBytes=4GB,[long]$MinimumFreeBytes=2GB)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath($CaptureDirectory).TrimEnd('\','/')
if(!(Test-Path -LiteralPath $root)){New-Item -ItemType Directory -Path $root | Out-Null}
if((Get-Item -LiteralPath $root).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Capture cleanup refuses a reparse-point directory.'}
$files=@(Get-ChildItem -LiteralPath $root -File)
$protected=@{}
# Preserve the newest three sessions, including failures, regardless of status.
$reports=@($files | Where-Object {$_.Name -match '^steam-inputs-\d+\.txt$'} | Sort-Object LastWriteTime -Descending)
foreach($r in ($reports | Select-Object -First 3)){$protected[$r.BaseName]=$true}
$pins=Join-Path $root 'retention-pins.txt'
if(Test-Path -LiteralPath $pins){foreach($line in (Get-Content -LiteralPath $pins)){if($line -match '^steam-inputs-\d+$'){$protected[$line]=$true}}}
$total=[long](($files | Measure-Object Length -Sum).Sum)
$removed=0
foreach($f in ($files | Sort-Object LastWriteTime)){
    if($total -le $MaxBytes){break}
    if($f.Name -notmatch '^(steam-inputs-\d+)-.*\.bin$' -or $protected.ContainsKey($Matches[1])){continue}
    if([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($f.FullName)) -ne $root){throw 'Capture path escaped directory.'}
    Remove-Item -LiteralPath $f.FullName
    $total-=$f.Length;$removed++
}
if($total -gt $MaxBytes){throw 'Capture budget exceeded by protected/reference files. Archive those captures before starting another test.'}
$drive=New-Object IO.DriveInfo ([IO.Path]::GetPathRoot($root))
if($drive.AvailableFreeSpace -lt $MinimumFreeBytes){throw 'At least 2 GiB free is required before arming. Free space or archive captures first.'}
Write-Host ('Capture storage: {0:N2} GiB; removed {1} old binaries; newest three sessions and pinned references retained.' -f ($total/1GB),$removed)
