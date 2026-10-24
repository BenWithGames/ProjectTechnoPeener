$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$test=Join-Path $project ('build-native\archive-test-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $test | Out-Null
$helper=Join-Path $test 'Archive-AutomaticReport.ps1'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Archive-AutomaticReport.ps1') -Destination $helper
foreach($category in @('pass','fails')){
 $name=if($category -eq 'pass'){'match-1-1'}else{'match-1-2'}
 $folder=Join-Path $test ('logs\'+$name)
 New-Item -ItemType Directory -Path (Join-Path $folder 'nested') -Force | Out-Null
 [IO.File]::WriteAllBytes((Join-Path $folder 'nested\bytes.bin'),[byte[]](0..255))
 [IO.File]::WriteAllText((Join-Path $folder 'report.txt'),'status='+$category)
 [IO.File]::WriteAllText((Join-Path $folder 'archive.ready'),$category)
 & $helper -ReportFolder $folder | Out-Null
 if(Test-Path -LiteralPath $folder){throw 'Loose source remains after verified ZIP'}
 if(!(Test-Path -LiteralPath (Join-Path $test ('logs\'+$category+'\match-unpaired-'+$name.Substring(6)+'.zip')))){throw 'Missing ZIP'}
}
$folder=Join-Path $test 'logs\match-1-3'
New-Item -ItemType Directory -Path $folder | Out-Null
[IO.File]::WriteAllText((Join-Path $folder 'report.txt'),'unfinished')
& $helper -ReportFolder $folder -ParentId 2147483647 | Out-Null
if(!(Test-Path -LiteralPath (Join-Path $test 'logs\fails\match-unpaired-1-3.zip'))){throw 'Exited process was not archived as failure'}
$folder=Join-Path $test 'logs\match-1-1'
New-Item -ItemType Directory -Path $folder | Out-Null
[IO.File]::WriteAllText((Join-Path $folder 'report.txt'),'new evidence')
[IO.File]::WriteAllText((Join-Path $folder 'archive.ready'),'pass')
$rejected=$false;try{& $helper -ReportFolder $folder | Out-Null}catch{$rejected=$true}
if(!$rejected -or !(Test-Path -LiteralPath (Join-Path $folder 'report.txt'))){throw 'Collision lost source evidence'}
$rejected=$false;try{& $helper -ReportFolder $test | Out-Null}catch{$rejected=$true}
if(!$rejected){throw 'Outside directory accepted'}
'PASS: pass/fails ZIPs, nested binary hash verification, crash recovery, collision preservation and path confinement'

# Both peers share an ID but retain distinct local suffixes, across pass/fails.
foreach($n in @(4,5)){
 $folder=Join-Path $test ('logs\match-1-'+$n)
 New-Item -ItemType Directory -Path $folder | Out-Null
 [IO.File]::WriteAllText((Join-Path $folder 'match-id.txt'),'00000000000000010000000000000002')
 $category=if($n -eq 4){'pass'}else{'fails'}
 [IO.File]::WriteAllText((Join-Path $folder 'archive.ready'),$category)
 & $helper -ReportFolder $folder | Out-Null
 if(!(Test-Path -LiteralPath (Join-Path $test ('logs\'+$category+'\match-00000000000000010000000000000002-1-'+$n+'.zip')))){throw 'Shared match filename missing'}
}
'PASS: shared match ID on both peer archives'

# A ready/recovered folder can still contain a CSV held by a closing writer.
$folder=Join-Path $test 'logs\match-2-1'
New-Item -ItemType Directory -Path $folder | Out-Null
[IO.File]::WriteAllText((Join-Path $folder 'archive.ready'),'fails')
$csv=Join-Path $folder 'events.csv'
[IO.File]::WriteAllText($csv,'before')
$writer=Start-Job -ArgumentList $csv -ScriptBlock {
 param($path)
 $handle=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Write,[IO.FileShare]::Read)
 try {
  Write-Output 'locked'
  Start-Sleep -Seconds 2
  $bytes=[Text.Encoding]::UTF8.GetBytes('complete evidence')
  $handle.SetLength(0);$handle.Write($bytes,0,$bytes.Length);$handle.Flush()
 } finally {$handle.Dispose()}
}
try {
 $deadline=[DateTime]::UtcNow.AddSeconds(15)
 while(!($writer | Receive-Job -Keep)){if([DateTime]::UtcNow -gt $deadline){throw 'Writer did not start'};Start-Sleep -Milliseconds 50}
 & $helper -ReportFolder $folder | Out-Null
 $archive=[IO.Compression.ZipFile]::OpenRead((Join-Path $test 'logs\fails\match-unpaired-2-1.zip'))
 try {
  $reader=[IO.StreamReader]::new($archive.GetEntry('events.csv').Open())
  try {if($reader.ReadToEnd() -ne 'complete evidence'){throw 'Archived an incomplete writer buffer'}}finally{$reader.Dispose()}
 } finally {$archive.Dispose()}
 if(Test-Path -LiteralPath $folder){throw 'Source not removed after writer completed and ZIP verified'}
} finally {$writer | Stop-Job;$writer | Remove-Job -Force}
'PASS: locked writer retried; final CSV verified before source removal'

# Retain newest ten independently; unrelated files and incomplete archives survive.
foreach($category in @('pass','fails')){
 $directory=Join-Path $test ('logs\'+$category)
 foreach($n in 1..12){
  $path=Join-Path $directory ('match-unpaired-99-'+$n+'.zip')
  [IO.File]::WriteAllText($path,'retention fixture')
  (Get-Item -LiteralPath $path).LastWriteTimeUtc=[DateTime]::UtcNow.AddDays($n)
 }
 foreach($name in @('notes.zip','match-unpaired-99-99.zip.partial')){
  [IO.File]::WriteAllText((Join-Path $directory $name),'preserve')
 }
}
& $helper -RetentionOnly
foreach($category in @('pass','fails')){
 $directory=Join-Path $test ('logs\'+$category)
 $reports=@(Get-ChildItem -LiteralPath $directory -Filter 'match-*.zip')
 if($reports.Count -ne 10){throw 'Retention did not keep ten per category'}
 foreach($n in 3..12){if(!(Test-Path -LiteralPath (Join-Path $directory ('match-unpaired-99-'+$n+'.zip')))){throw 'Newest archive removed'}}
 foreach($name in @('notes.zip','match-unpaired-99-99.zip.partial')){
  if(!(Test-Path -LiteralPath (Join-Path $directory $name))){throw 'Unrelated or unfinished file removed'}
 }
}
'PASS: newest ten per category retained; unrelated and partial files preserved'

