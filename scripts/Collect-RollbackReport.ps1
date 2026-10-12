param([string]$ReportPath,[string]$OutputDirectory)
$ErrorActionPreference='Stop'
if(!$ReportPath){$ReportPath=Read-Host 'Paste the report .txt path printed by the test'}
$report=Get-Item -LiteralPath $ReportPath.Trim('"')
if($report.PSIsContainer -or $report.Name -notmatch '^steam-inputs-\d+\.txt$'){
    throw 'Choose the steam-inputs-PID.txt report printed by the test.'
}
if(!$OutputDirectory){$OutputDirectory=Join-Path $env:USERPROFILE 'Downloads'}
$null=New-Item -ItemType Directory -Force -Path $OutputDirectory
$id=[guid]::NewGuid().ToString('N').Substring(0,8)
$stage=Join-Path ([IO.Path]::GetTempPath()) ('SC6Rollback-Report-'+$id)
$null=New-Item -ItemType Directory -Path $stage
$zip=Join-Path $OutputDirectory ('SC6Rollback-Report-'+$report.BaseName+'-'+$id+'.zip')
$copied=@()
try {
    $files=Get-ChildItem -LiteralPath $report.DirectoryName -File | Where-Object {
        $_.Name -eq $report.Name -or
        ($_.Name.StartsWith($report.BaseName+'-',[StringComparison]::OrdinalIgnoreCase) -and $_.Extension -in '.txt','.csv','.json')
    }
    foreach($file in $files){
        # Reports may still be open by the game. Collect a shared-read snapshot,
        # not an exclusive ReadAllText; record that this is not an atomic bundle.
        $source=[IO.File]::Open($file.FullName,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
        try {
            $target=[IO.File]::Create((Join-Path $stage $file.Name))
            try {$source.CopyTo($target)} finally {$target.Dispose()}
        } finally {$source.Dispose()}
        $copied+=$file.Name
    }
    @('SC6Rollback diagnostic report bundle',('Collected UTC: '+[DateTime]::UtcNow.ToString('o')),
      'Local copy only; nothing was uploaded.',
      'Files were copied separately. A still-running capture may be incomplete.',
      'Transport PASS alone does not establish rollback success.',
      'Included files:')+$copied | Set-Content -LiteralPath (Join-Path $stage 'README.txt') -Encoding utf8
    Compress-Archive -LiteralPath @((Get-ChildItem -LiteralPath $stage -File).FullName) -DestinationPath $zip
    Write-Output $zip
} finally {
    # Only remove the exact GUID staging directory created above.
    $tempRoot=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
    $resolved=[IO.Path]::GetFullPath($stage)
    if($resolved.StartsWith($tempRoot,[StringComparison]::OrdinalIgnoreCase) -and
       [IO.Path]::GetFileName($resolved) -eq ('SC6Rollback-Report-'+$id)){
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
