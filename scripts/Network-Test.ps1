param(
    [ValidateSet('Host','Join')][string]$Mode,
    [string]$Peer,
    [string]$KeyFile,
    [ValidateRange(1,65535)][int]$RelayPort=27860,
    [ValidateRange(1,65535)][int]$ClientPort=27861,
    [switch]$SkipFirewall,
    [switch]$NonInteractive,
    [switch]$Observe
)
$ErrorActionPreference='Stop'
$relayProcess=$null
$clientProcess=$null
$firewallCreated=$false
$ruleName='SC6Probe-'+[Guid]::NewGuid().ToString('N')
$failure=$false
function Configure-Firewall([string]$Action) {
    $helper=Join-Path $PSScriptRoot 'Network-Firewall.ps1'
    $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$helper+'"'),'-Action',$Action,'-RuleName',$ruleName,'-Port',"$RelayPort")
    $elevated=Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -Wait -PassThru
    if($elevated.ExitCode -ne 0){throw "Firewall $Action failed. Check Windows administrator access."}
}
function Read-IPv4([string]$Address) {
    $parsed=$null
    if(![Net.IPAddress]::TryParse($Address,[ref]$parsed) -or $parsed.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork -or $Address -eq '0.0.0.0' -or $Address -eq '255.255.255.255') {
        throw 'Enter a numeric IPv4 address, such as the host router public IPv4.'
    }
    return $parsed.ToString()
}
try {
    Write-Host 'SC6 NETWORK DIAGNOSTIC - SC6 stays closed; no gameplay is tested.'
    $exe=Join-Path $PSScriptRoot 'SC6NetworkProbe.exe'
    if(!(Test-Path -LiteralPath $exe)){throw 'Extract the complete ZIP before running Network-Test.cmd.'}
    if(!$Mode){
        if($NonInteractive){throw 'Noninteractive mode requires -Mode.'}
        $choice=Read-Host 'Type H to host on your partner PC, or J to join'
        if($choice -match '^(h|host)$'){$Mode='Host'}elseif($choice -match '^(j|join)$'){$Mode='Join'}else{throw 'Choose H or J.'}
    }
    $observeArgs=@();$relayExtra=@()
    if($Observe){
        Write-Host 'RECORDING ONLY: no game input injection or restoration. Match mode is NOT detected automatically.' -ForegroundColor Yellow
        if(!$NonInteractive){
            $consent=Read-Host 'Both players must agree to observation in offline Training or a private match. Never ranked. Type OBSERVE to confirm'
            if($consent -cne 'OBSERVE'){throw 'Observation was not confirmed.'}
        }
        $games=@(Get-Process SoulcaliburVI -ErrorAction SilentlyContinue)
        if($games.Count -ne 1){throw 'Launch exactly one SC6 process with the observer DLL installed first.'}
        $gameExecutable=$games[0].Path
        if((Get-FileHash -LiteralPath $gameExecutable -Algorithm SHA256).Hash -ne 'F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553'){throw 'Unsupported SC6 executable build.'}
        $captureDirectory=Join-Path (Split-Path $gameExecutable -Parent) 'Mods\SC6Rollback\captures'
        if(!(Test-Path -LiteralPath $captureDirectory)){throw 'Observer capture directory missing; install the observer and launch SC6.'}
        $observeArgs=@('--observe-pid',"$($games[0].Id)",'--capture-dir',('"'+$captureDirectory+'"'))
        $relayExtra=@('--observation')
    }
    $runFolder=Join-Path $PSScriptRoot ('logs\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
    New-Item -ItemType Directory -Path $runFolder -Force | Out-Null
    if(!$KeyFile){
        if($NonInteractive){throw 'Noninteractive mode requires -KeyFile.'}
        $KeyFile=Join-Path $PSScriptRoot 'session.key'
        if($Mode -eq 'Host'){
            $keyBytes=New-Object byte[] 32
            $rng=[Security.Cryptography.RandomNumberGenerator]::Create()
            try{$rng.GetBytes($keyBytes);$code=[BitConverter]::ToString($keyBytes).Replace('-','').ToLowerInvariant()}finally{$rng.Dispose();[Array]::Clear($keyBytes,0,32)}
            [IO.File]::WriteAllText($KeyFile,$code,[Text.Encoding]::ASCII)
            Write-Host "`nSend this session code privately to your partner (not in logs):" -ForegroundColor Yellow
            Write-Host $code
        } else {
            $code=(Read-Host 'Paste the session code your partner sent').Trim()
            if($code -notmatch '^[0-9a-fA-F]{64}$'){throw 'Session code must be exactly 64 hexadecimal characters.'}
            [IO.File]::WriteAllText($KeyFile,$code,[Text.Encoding]::ASCII)
        }
    }
    $KeyFile=(Resolve-Path -LiteralPath $KeyFile).Path
    if(([IO.File]::ReadAllText($KeyFile)).Trim() -notmatch '^[0-9a-fA-F]{64}$'){throw 'Invalid session key file.'}
    if($Mode -eq 'Host'){
        if($ClientPort -eq $RelayPort){throw 'Client and relay ports must differ on the host PC.'}
        Write-Host "`nRouter requirement: forward UDP $RelayPort to this PC. The launcher cannot configure your router."
        Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object {$_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*'} | ForEach-Object {Write-Host ('Local address: '+$_.IPAddress+' ('+$_.InterfaceAlias+')')}
        if(!$NonInteractive){
            Write-Host 'Share your router public IPv4 with your partner too. It is shown on the router Internet/WAN page.'
            $confirmed=Read-Host 'Is UDP forwarding configured? Type Y to continue'
            if($confirmed -notmatch '^(y|yes)$'){throw 'Set up the host router forwarding, then run this launcher again.'}
        }
        if(!$SkipFirewall){
            Write-Host 'Approve Windows UAC to add a temporary app-specific UDP firewall rule. Another UAC prompt removes it when done.'
            Configure-Firewall 'Add';$firewallCreated=$true
        }
        $relayOut=Join-Path $runFolder 'relay.txt';$relayErr=Join-Path $runFolder 'relay-errors.txt'
        $relayProcess=Start-Process -FilePath $exe -ArgumentList (@('--relay-server','--port',"$RelayPort",'--key-file',('"'+$KeyFile+'"'),'--timeout-seconds','600')+$relayExtra) -WindowStyle Hidden -RedirectStandardOutput $relayOut -RedirectStandardError $relayErr -PassThru
        $ready=$false
        for($attempt=0;$attempt -lt 50;$attempt++){
            if($relayProcess.HasExited){throw ('Relay failed: '+(Get-Content -LiteralPath $relayErr -Raw))}
            if((Test-Path -LiteralPath $relayOut) -and (Select-String -LiteralPath $relayOut -Pattern 'RELAY READY' -Quiet)){$ready=$true;break}
            Start-Sleep -Milliseconds 100
        }
        if(!$ready){throw 'Relay did not become ready.'}
        $Peer='127.0.0.1';$role='1'
        if(!$NonInteractive){[void](Read-Host 'Relay ready. Tell your partner to prepare Join, then press Enter together to start')}
    } else {
        $role='0'
        if(!$Peer){if($NonInteractive){throw 'Join requires -Peer.'};$Peer=Read-Host 'Enter your partner router public IPv4'}
        $Peer=Read-IPv4 $Peer
        if(!$NonInteractive){[void](Read-Host 'Press Enter when your partner is ready to start (within 60 seconds of each other)')}
    }
    $clientOut=Join-Path $runFolder 'client.txt';$clientErr=Join-Path $runFolder 'client-errors.txt'
    if($Observe){Write-Host 'Switch to SC6 now and leave it unpaused. Capturing 120 real input records; no restoration.'}
    else{Write-Host 'Running automatically. No game inputs are needed. Waiting up to 60 seconds...'}
    $captureStart=Get-Date
    $clientProcess=Start-Process -FilePath $exe -ArgumentList (@('--role',$role,'--peer',$Peer,'--port',"$ClientPort",'--peer-port',"$RelayPort",'--key-file',('"'+$KeyFile+'"'),'--timeout-seconds','60')+$observeArgs) -WindowStyle Hidden -RedirectStandardOutput $clientOut -RedirectStandardError $clientErr -PassThru
    # Retain the process handle before waiting; Windows PowerShell otherwise
    # can lose ExitCode for processes created through Start-Process.
    $null=$clientProcess.Handle
    if(!$clientProcess.WaitForExit(70000)){throw 'Client exceeded its time limit.'}
    Get-Content -LiteralPath $clientOut | Write-Host
    if($clientProcess.ExitCode -ne 0){Get-Content -LiteralPath $clientErr | Write-Host;throw 'Network test failed. Send the log folder for diagnosis.'}
    if(!(Select-String -LiteralPath $clientOut -Pattern '^PASS .*peer_match=1' -Quiet)){throw 'Client exited without a verified PASS.'}
    Write-Host 'PASS on this PC. Both PCs must pass. Send both log folders.' -ForegroundColor Green
    if($Observe){
        Get-ChildItem -LiteralPath $captureDirectory -Filter 'network-*.csv' -File | Where-Object {$_.LastWriteTime -ge $captureStart} | Copy-Item -Destination $runFolder
        Write-Host 'This verifies record delivery only. Player mapping and game-frame alignment still need analysis.'
    }
    # Let the remote client finish before stopping this machine's relay.
    if($relayProcess){Start-Sleep -Seconds 5}
} catch {
    $failure=$true
    Write-Host ('ERROR: '+$_.Exception.Message) -ForegroundColor Red
} finally {
    foreach($child in @($clientProcess,$relayProcess)){
        if($null -ne $child){if(!$child.HasExited){$child.Kill();$child.WaitForExit()};$child.Dispose()}
    }
    if($firewallCreated){
        try {Configure-Firewall 'Remove'} catch {
            $failure=$true
            Write-Host "Could not remove temporary firewall rule $ruleName. Remove it in Windows Defender Firewall > Advanced settings > Inbound Rules."
        }
    }
    if($runFolder){Write-Host "Logs: $runFolder"}
}
if($failure){exit 1}
exit 0
