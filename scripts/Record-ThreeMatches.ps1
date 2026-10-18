param([ValidateRange(1,2)][int]$PlayerSlot=1)
$ErrorActionPreference='Stop'
try {
 $games=@(Get-Process SoulcaliburVI -ErrorAction Stop)
 if($games.Count -ne 1){throw 'Start exactly one SC6 game.'}
 $game=$games[0];$bin=Split-Path $game.Path -Parent
 if((Get-FileHash $game.Path).Hash -ne 'F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553'){throw 'Unsupported SC6 executable'}
 $capture=Join-Path $bin 'Mods\SC6Rollback\captures'
 if(Test-Path (Join-Path $capture "steam-inputs-$($game.Id).txt")){throw 'Restart SC6 first. Do not run a rollback suite in this recording session.'}
 if(Get-ChildItem $capture -Filter '*.arm'){throw 'Another recording/test arm is pending.'}
 $session=Join-Path $env:USERPROFILE ('Downloads\SC6-ThreeMatches-'+(Get-Date -Format yyyyMMdd-HHmmss))
 $null=New-Item -ItemType Directory -Path $session
 Write-Host 'RECORD ONLY: no restoration, replay, injected inputs or retained objects. Both players run this script.'
 Write-Host 'No warmup or friend code is needed. Stay in a private lobby with your friend. Do not launch any other test script.'
 Write-Host 'For each match: start recording together in the lobby, play the entire match, then stop at results before rematching.'
 $matches=@()
 for($match=1;$match -le 3;$match++){
  $label=Read-Host "Match $match of 3: enter both characters and stage"
  $null=Read-Host 'Both players ready in lobby? Press Enter on both PCs to arm recording'
  $ready=Join-Path $capture 'record-match-ready.txt'
  if(Test-Path $ready){Remove-Item -LiteralPath $ready}
  [IO.File]::WriteAllText((Join-Path $capture 'record-match.arm'),"$($game.Id) $match")
  $deadline=(Get-Date).AddSeconds(20)
  while(!(Test-Path $ready) -and (Get-Date) -lt $deadline){Start-Sleep -Milliseconds 200;if($game.HasExited){throw 'SC6 exited'}}
  if(!(Test-Path $ready)){throw 'Recorder did not arm. Check installed build and restart SC6.'}
  $folder=[IO.File]::ReadAllText($ready).Trim()
  if(!(Test-Path $folder) -or !(Split-Path $folder -Leaf).StartsWith("match-dataset-$($game.Id)-")){throw 'Invalid recorder response'}
  Write-Host "Recording match $match. Enter the match now and play normally. Limit: 54,000 simulation updates."
  $null=Read-Host 'When the match is finished and you are at results, press Enter here to stop recording'
  [IO.File]::WriteAllText((Join-Path $capture 'record-match.stop'),'stop')
  $report=Join-Path $folder 'recording.txt';$deadline=(Get-Date).AddSeconds(60)
  while(!(Test-Path $report) -and (Get-Date) -lt $deadline){Start-Sleep -Milliseconds 250;if($game.HasExited){throw 'SC6 exited before data was saved'}}
  if(!(Test-Path $report)){throw "Save timed out. Preserve $folder"}
  Start-Sleep -Seconds 1
  $complete=Read-Host 'Did you record the entire match from loading through results? Type YES, otherwise NO'
  $metadata=[ordered]@{match=$match;playerSlot=$PlayerSlot;charactersAndStage=$label;operatorConfirmedFullMatch=($complete -ceq 'YES');source=$folder;pid=$game.Id;exeSHA256=(Get-FileHash $game.Path).Hash;dllSHA256=(Get-FileHash (Join-Path $bin 'Mods\SC6Rollback\dlls\main.dll')).Hash}
  $metadata | ConvertTo-Json | Set-Content (Join-Path $folder 'match.json')
  Copy-Item -LiteralPath $folder -Destination (Join-Path $session "match-$match") -Recurse
  $matches+=$metadata
  Get-Content $report | Write-Host
  if($match -lt 3){Write-Host 'Return to the private lobby, choose the next characters/stage, then continue.'}
 }
 $matches | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $session 'session.json')
 'Analysis dataset only. Raw pointers are session-specific; these files cannot be directly loaded for replay. Snapshot windows are bounded and do not retain native objects. No replay correctness is claimed.' | Set-Content (Join-Path $session 'README.txt')
 Compress-Archive -Path (Join-Path $session '*') -DestinationPath ($session+'.zip')
 Write-Host "Send both PCs' ZIPs. Saved: $session.zip"
}catch {Write-Host ('ERROR: '+$_.Exception.Message);exit 1}
