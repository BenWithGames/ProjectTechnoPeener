param([Parameter(Mandatory=$true)][switch]$TrainingConfirmed,
      [string]$GameDirectory='E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64')
$ErrorActionPreference='Stop'
if(!$TrainingConfirmed){throw 'Offline Training confirmation required.'}
$games=@(Get-Process SoulcaliburVI -ErrorAction Stop)
if($games.Count -ne 1){throw 'Expected one SC6 process.'}
$game=$games[0]
if((Split-Path $game.Path -Parent) -ne $GameDirectory){throw 'Unexpected game directory.'}
if((Get-FileHash -LiteralPath $game.Path).Hash -ne 'F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553'){throw 'Unsupported game.'}
$project=Split-Path $PSScriptRoot -Parent
$root=Join-Path $GameDirectory 'Mods\SC6Rollback'
if((Get-FileHash -LiteralPath "$root\dlls\main.dll").Hash -ne (Get-FileHash -LiteralPath "$project\package\Mods\SC6Rollback\dlls\main.dll").Hash){throw 'Installed DLL differs from staged build.'}
$captures=Join-Path $root 'captures'
if(@(Get-ChildItem -LiteralPath $captures -Filter '*.arm').Count){throw 'Another diagnostic is pending.'}
if(Test-Path "$captures\steam-inputs-$($game.Id).txt"){throw 'Restart after a network capture.'}
foreach($report in @(Get-ChildItem -LiteralPath $captures -Filter "native-adjacent-idle-$($game.Id)-*.txt")){
    $metadata=Get-Content -LiteralPath $report.FullName
    if($metadata -notcontains 'scope=offline_field_trace_only' -or $metadata -notcontains 'fault=0' -or $metadata -notcontains 'stop=limit' -or $metadata -notcontains 'samples=600'){throw 'Restart after an incomplete or non-read-only experiment.'}
}
$stream=[IO.File]::Open((Join-Path $captures 'offline-full-restore.arm'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{
    $bytes=[Text.Encoding]::ASCII.GetBytes("OFFLINE_FIELD_TRACE_ONLY $($game.Id)`n")
    $stream.Write($bytes,0,$bytes.Length)
}finally{$stream.Dispose()}
Write-Host 'Armed 600-frame read-only field trace; no restoration or extra updates. Keep Training unpaused and controls untouched. Completed read-only captures can be repeated in this session.'
