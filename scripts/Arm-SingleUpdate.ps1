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
if(@(Get-ChildItem -LiteralPath $captures -Filter "native-adjacent-idle-$($game.Id)-*.txt").Count -or (Test-Path "$captures\steam-inputs-$($game.Id).txt")){throw 'Restart SC6 before this test.'}
$stream=[IO.File]::Open((Join-Path $captures 'offline-full-restore.arm'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{
    $bytes=[Text.Encoding]::ASCII.GetBytes("OFFLINE_SINGLE_SYNCHRONOUS_REPLAY $($game.Id)`n")
    $stream.Write($bytes,0,$bytes.Length)
}finally{$stream.Dispose()}
Write-Host 'Armed one synchronous offline idle replay. Keep Training unpaused and controls untouched. Restart SC6 after the experiment.'
