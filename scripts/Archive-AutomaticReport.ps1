param([string]$ReportFolder,[int]$ParentId=0,[switch]$RetentionOnly)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'logs')).TrimEnd('\')
function Remove-OldMatchArchives {
 foreach($category in @('pass','fails')){
  $directory=Join-Path $root $category
  if(!(Test-Path -LiteralPath $directory)){continue}
  if((Get-Item -LiteralPath $root).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked logs directory rejected'}
  if((Get-Item -LiteralPath $directory).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked archive directory rejected'}
  $archives=@(Get-ChildItem -LiteralPath $directory -File | Where-Object {
   $_.Name -match '^match-(?:[0-9a-f]{32}-|unpaired-)?\d+-\d+\.zip$' -and
   !($_.Attributes -band [IO.FileAttributes]::ReparsePoint)
  } | Sort-Object @{Expression='LastWriteTimeUtc';Descending=$true},@{Expression='Name';Descending=$true})
  foreach($archive in ($archives | Select-Object -Skip 10)){
   if([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($archive.FullName)) -ne $directory){throw 'Retention path rejected'}
   Remove-Item -LiteralPath $archive.FullName -Force
  }
 }
}
if($RetentionOnly){Remove-OldMatchArchives;return}
if(!$ReportFolder){throw 'ReportFolder is required when archiving'}
$source=[IO.Path]::GetFullPath($ReportFolder).TrimEnd('\')
if([IO.Path]::GetDirectoryName($source) -ne $root -or [IO.Path]::GetFileName($source) -notmatch '^match-\d+-\d+$'){throw 'Report path is outside the match logs directory'}
$marker=Join-Path $source 'archive.ready'
try {
 if($ParentId){
  $parent=Get-Process -Id $ParentId -ErrorAction SilentlyContinue
  while(!(Test-Path -LiteralPath $marker)){
   if(!$parent -or $parent.HasExited){
    [IO.File]::WriteAllText((Join-Path $source 'archive-recovery.txt'),'Game exited before finalization. Completion is unverified.')
    [IO.File]::WriteAllText($marker,'fails');break
   }
   Start-Sleep -Milliseconds 500
  }
 }
 $category=[IO.File]::ReadAllText($marker).Trim()
 if($category -notin @('pass','fails')){throw 'Invalid archive classification'}
 $destination=Join-Path $root $category
 New-Item -ItemType Directory -Force -Path $destination | Out-Null
 $name=Split-Path $source -Leaf
 $identity=Join-Path $source 'match-id.txt'
 if(Test-Path -LiteralPath $identity){
  $id=[IO.File]::ReadAllText($identity).Trim()
  if($id -notmatch '^[0-9a-f]{32}$'){throw 'Invalid shared match ID; original report retained'}
  $name='match-'+$id+'-'+$name.Substring(6)
 }else{$name='match-unpaired-'+$name.Substring(6)}
 $zip=Join-Path $destination ($name+'.zip')
 $temporary=$zip+'.partial'
 if((Test-Path -LiteralPath $zip) -or (Test-Path -LiteralPath $temporary)){throw 'Archive already exists; original report retained'}
 $locked=[Collections.Generic.List[IDisposable]]::new()
 $deadline=[DateTime]::UtcNow.AddSeconds(30)
 $lockRetries=0
 try {
  while($true){
   try {
    $items=@(Get-ChildItem -LiteralPath $source -Recurse -Force)
    if((Get-Item -LiteralPath $source).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked report directory rejected'}
    if($items | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}){throw 'Linked report entry rejected'}
    $files=@($items | Where-Object {!$_.PSIsContainer})
    if(!$files.Count){throw 'Empty report'}
    foreach($file in $files){
     $locked.Add([IO.File]::Open($file.FullName,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read))
    }
    break
   } catch {
    foreach($handle in $locked){$handle.Dispose()};$locked.Clear()
    $errorCode=$_.Exception
    while($errorCode.InnerException){$errorCode=$errorCode.InnerException}
    if(($errorCode.HResult -band 65535) -notin @(32,33) -or [DateTime]::UtcNow -ge $deadline){throw}
    ++$lockRetries;Start-Sleep -Milliseconds 250
   }
  }
 [IO.Compression.ZipFile]::CreateFromDirectory($source,$temporary,[IO.Compression.CompressionLevel]::Fastest,$false)
 $archive=[IO.Compression.ZipFile]::OpenRead($temporary)
 try {
  $entries=@($archive.Entries | Where-Object {$_.Name})
  if($entries.Count -ne $files.Count){throw 'Archive file count mismatch'}
  foreach($file in $files){
   $relative=$file.FullName.Substring($source.Length+1).Replace('\','/')
   $entry=$archive.GetEntry($relative)
   if(!$entry){$entry=$archive.GetEntry($relative.Replace('/','\'))}
   if(!$entry -or $entry.Length -ne $file.Length){throw "Archive size mismatch: $relative"}
   $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
   try{$hash=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','')}finally{$stream.Dispose();$sha.Dispose()}
   if($hash -ne (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash){throw "Archive content mismatch: $relative"}
  }
 } finally {$archive.Dispose()}
 } finally {foreach($handle in $locked){$handle.Dispose()}}
 Write-Verbose "Archive source-lock retries: $lockRetries"
 Move-Item -LiteralPath $temporary -Destination $zip
 if([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($source)) -ne $root){throw 'Cleanup path rejected'}
 Remove-Item -LiteralPath $source -Recurse -Force
 Remove-OldMatchArchives
 Write-Output $zip
} catch {
 if(Test-Path -LiteralPath $source){[IO.File]::WriteAllText((Join-Path $source 'archive-error.txt'),$_.Exception.Message)}
 throw
}
