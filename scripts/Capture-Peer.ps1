param(
    [switch]$Worker,
    [string]$OutputDirectory,
    [ValidateRange(5,120)][int]$Seconds=90
)
$ErrorActionPreference='Stop'
$PSDefaultParameterValues['Out-File:Encoding']='utf8'
$PSDefaultParameterValues['Set-Content:Encoding']='utf8'
$PSDefaultParameterValues['Add-Content:Encoding']='utf8'
$pktmon=Join-Path $env:SystemRoot 'System32\pktmon.exe'
if($Worker){
    $started=$false;$filtered=$false;$failure=$false;$filterSnapshot=''
    $filterName='SC6Peer27860'
    $statusLog=Join-Path $OutputDirectory 'capture-status.txt'
    try {
        if(!(Test-Path -LiteralPath $OutputDirectory -PathType Container)){throw 'Output folder missing.'}
        $identity=[Security.Principal.WindowsIdentity]::GetCurrent()
        if(!([Security.Principal.WindowsPrincipal]$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Administrator approval is required.'}
        $state=(& $pktmon status 2>&1 | Out-String)
        if($LASTEXITCODE -ne 0 -or $state -notmatch 'Packet Monitor is not running'){throw 'Packet Monitor is already active or its status cannot be verified. Existing capture left unchanged.'}
        $filters=(& $pktmon filter list 2>&1 | Out-String)
        if($LASTEXITCODE -ne 0 -or $filters -notmatch '(?m)^\s*None\s*$'){throw 'Existing Packet Monitor filters found, or format not recognized. Existing filters left unchanged.'}
        @('Capture scope: UDP port 27860 only; first 64 bytes per packet.',('Start requested: '+(Get-Date -Format o)),('Duration seconds: '+$Seconds)) | Set-Content -LiteralPath $statusLog
        & $pktmon list 2>&1 | Out-File -LiteralPath (Join-Path $OutputDirectory 'components.txt')
        Get-NetIPConfiguration | Select-Object InterfaceAlias,@{Name='IPv4';Expression={$_.IPv4Address.IPAddress}},@{Name='Gateway';Expression={$_.IPv4DefaultGateway.NextHop}} | Format-List | Out-File -LiteralPath (Join-Path $OutputDirectory 'interfaces.txt')
        & $pktmon filter add $filterName -t UDP -p 27860 2>&1 | Out-File -LiteralPath $statusLog -Append
        if($LASTEXITCODE -ne 0){throw 'Could not set UDP filter.'}
        $filtered=$true
        $filterSnapshot=(& $pktmon filter list 2>&1 | Out-String).Trim()
        & $pktmon start --capture --pkt-size 64 --file-size 16 --file-name (Join-Path $OutputDirectory 'capture.etl') 2>&1 | Out-File -LiteralPath $statusLog -Append
        if($LASTEXITCODE -ne 0){throw 'Packet Monitor could not start.'}
        $started=$true
        Set-Content -LiteralPath (Join-Path $OutputDirectory 'ready.txt') -Value (Get-Date -Format o)
        for($tick=0;$tick -lt $Seconds;$tick++){Start-Sleep -Seconds 1}
    } catch {
        $failure=$true
        $_.Exception.Message | Set-Content -LiteralPath (Join-Path $OutputDirectory 'error.txt')
    } finally {
        if($started){
            & $pktmon counters 2>&1 | Out-File -LiteralPath (Join-Path $OutputDirectory 'counters.txt')
            & $pktmon stop 2>&1 | Out-File -LiteralPath $statusLog -Append
            if($LASTEXITCODE -ne 0){$failure=$true;'Packet Monitor stop failed; run pktmon stop in an administrator terminal.' | Add-Content -LiteralPath (Join-Path $OutputDirectory 'error.txt')}
        }
        if($filtered){
            $currentFilters=(& $pktmon filter list 2>&1 | Out-String).Trim()
            if($currentFilters -eq $filterSnapshot){
                & $pktmon filter remove 2>&1 | Out-File -LiteralPath $statusLog -Append
                if($LASTEXITCODE -ne 0){$failure=$true;'Capture filter cleanup failed.' | Add-Content -LiteralPath (Join-Path $OutputDirectory 'error.txt')}
            }else{
                $failure=$true
                'Filters changed during capture; left them unchanged to preserve other work.' | Add-Content -LiteralPath (Join-Path $OutputDirectory 'error.txt')
            }
        }
        if($started){
            & $pktmon etl2txt (Join-Path $OutputDirectory 'capture.etl') --out (Join-Path $OutputDirectory 'capture.txt') --verbose 2>&1 | Out-File -LiteralPath $statusLog -Append
            if($LASTEXITCODE -ne 0){$failure=$true;'Text conversion failed; original ETL retained.' | Add-Content -LiteralPath (Join-Path $OutputDirectory 'error.txt')}
        }
        Set-Content -LiteralPath (Join-Path $OutputDirectory 'done.txt') -Value (Get-Date -Format o)
    }
    if($failure){exit 1};exit 0
}
try {
    if(!(Test-Path -LiteralPath $pktmon)){throw 'Windows Packet Monitor was not found on this PC.'}
    Write-Host 'Keep Host and Join at their final Enter prompts. This captures UDP 27860 only.'
    Write-Host 'Approve the administrator prompt. Do not close this window until it says finished.'
    $OutputDirectory=Join-Path $PSScriptRoot ('peer-captures\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),'-Worker','-OutputDirectory',('"'+$OutputDirectory+'"'),'-Seconds',"$Seconds")
    $child=Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -PassThru
    $null=$child.Handle
    $announced=$false
    while(!$child.HasExited){
        if(!$announced -and (Test-Path -LiteralPath (Join-Path $OutputDirectory 'ready.txt'))){
            Write-Host "CAPTURE READY: both players press Enter in their network launchers NOW. Recording for $Seconds seconds." -ForegroundColor Green
            $announced=$true
        }
        Start-Sleep -Milliseconds 250
    }
    $child.WaitForExit();$result=$child.ExitCode;$child.Dispose()
    $report=$OutputDirectory+'.zip'
    $reportFiles=@('capture-status.txt','components.txt','interfaces.txt','counters.txt','capture.etl','capture.txt','ready.txt','done.txt','error.txt') | ForEach-Object {Join-Path $OutputDirectory $_} | Where-Object {Test-Path -LiteralPath $_}
    if($reportFiles.Count){Compress-Archive -LiteralPath $reportFiles -DestinationPath $report -Force;Write-Host "Send this report ZIP: $report"}
    if($result -ne 0){
        if(Test-Path -LiteralPath (Join-Path $OutputDirectory 'error.txt')){Get-Content -LiteralPath (Join-Path $OutputDirectory 'error.txt') | Write-Host}
        throw 'Capture helper reported an error. Include the report ZIP.'
    }
    Write-Host 'Capture finished and its filter removed. Also send the Join client.txt from this attempt.' -ForegroundColor Green
} catch {Write-Host ('ERROR: '+$_.Exception.Message) -ForegroundColor Red;exit 1}
