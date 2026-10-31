[CmdletBinding()]
param(
    [string]$Config='', [string]$Firmware='',
    [Parameter(HelpMessage='Optional exact J-Link serial. If omitted, select only a single connected USB probe; multiple probes require an explicit serial.')][string]$JLinkSerial='',
    [ValidateSet('om6626')][string]$Chip='om6626',
    [switch]$DryRun, [switch]$Yes,
    [Parameter(HelpMessage='Skip ordinary full readback, but still verify both new Recovery pages and Settings. Result is NOT VERIFIED; default verifies the full 1 MiB.')][switch]$SkipReadbackVerify,
    [string]$Python='python', [string]$JLinkExe='',
    [Parameter(HelpMessage='Transfer speed, default 1 MHz as in the historical loader workflow. Initial attachment remains at or below 100 kHz.')][ValidateScript({$_ -ge 1000 -and $_ -le 1000000 -and $_ % 1000 -eq 0})][int]$FrequencyHz=1000000,
    [Parameter(HelpMessage='Retry initial target detection only. Never retry capture, erase, programming, verification or an unknown native timeout.')][switch]$RetryDetect,
    [Parameter(HelpMessage='Overall initial detection window in seconds when RetryDetect is selected. No scripted reset or BOOT operation; J-Link may use its native connect fallback.')][ValidateRange(1,3600)][int]$DetectTimeoutSeconds=120,
    [Parameter(HelpMessage='Initial contact timeout per native attempt, default 12 seconds (previously 8). After a CPUID read, use the operation timeout, still capped by the remaining detection window.')][ValidateRange(3,120)][int]$AttemptTimeoutSeconds=12,
    [ValidateRange(30,1800)][int]$OperationTimeoutSeconds=360,
    [Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments=$true)][object[]]$RemainingArguments
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../..'))
. (Join-Path $PSScriptRoot 'terminal-log.ps1')
. (Join-Path $root 'tools/jlink-tools.ps1')
$terminalState=Get-InkTagTerminalColour
$operation='';$writeStarted=$false;$ramCaptureStarted=$false;$maintenanceStarted=$false;$lock=$null;$locked=$false;$presentation=@{}
$registrationDirectory='';$registrationFile='';$registrationSHA256='';$registrationValidated=$false
$targetAccessStarted=$false
$connectKHz=[Math]::Min(100,[int]($FrequencyHz/1000))
$helper=Join-Path $root 'tools/hardware/onmicro_full_prepare.py'

function Invoke-ControlledPython([string[]]$Arguments) {
    # WinPS5.1 wraps native stderr as ErrorRecord. Collect the complete native
    # diagnostic and its natural exit; do not turn the first traceback line
    # into a terminating PowerShell exception or reuse a previous exit code.
    $ErrorActionPreference='Continue'
    $PSNativeCommandUseErrorActionPreference=$false
    $lines=@(& $pythonExe @Arguments 2>&1 | ForEach-Object {[string]$_})
    return [pscustomobject]@{ExitCode=$LASTEXITCODE;Lines=$lines}
}

function Read-ControlledText([string]$Path) {
    if(!(Test-Path -LiteralPath $Path)){return ''}
    $stream=[IO.File]::Open($Path,'Open','Read','ReadWrite')
    $reader=[IO.StreamReader]::new($stream)
    try{return $reader.ReadToEnd()}finally{$reader.Dispose()}
}

function Start-ControlledProgrammingProcess([string]$FilePath,[string]$WorkingDirectory,[string]$Arguments) {
    # Start-Process's redirected-output handler is line buffered on Windows.
    # Commander percentages are backspace updates without a newline: read the
    # underlying pipes instead, like the GR stream engine. No target recipe change.
    $info=[Diagnostics.ProcessStartInfo]::new()
    $info.FileName=$FilePath;$info.WorkingDirectory=$WorkingDirectory;$info.Arguments=$Arguments
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    $child=[Diagnostics.Process]::new();$child.StartInfo=$info
    try {if(!$child.Start()){throw 'Programming process did not start'};return $child}
    catch {$child.Dispose();throw}
}

function Invoke-ControlledNative([string]$Stem,[string[]]$Commands,[string]$Device='CORTEX-M4',[switch]$Programming,[double]$TimeoutSeconds=0,[switch]$ProbeList,[switch]$Detection,[switch]$Metadata) {
    $limit=if($TimeoutSeconds -gt 0){[Math]::Min($OperationTimeoutSeconds,$TimeoutSeconds)}else{$OperationTimeoutSeconds}
    if($ProbeList -and ($Programming -or ($Commands -join "`n") -ne 'ShowEmuList USB')){throw 'Probe enumeration only accepts ShowEmuList USB'}
    if($Metadata -and ($Programming -or $ProbeList -or $Detection -or $Commands.Count -ne 1 -or
        $Commands[0] -cne ('ExpDevList "'+(Join-Path $operation 'registered-devices.txt')+'"'))){throw 'Metadata only accepts the fixed device-list export; no probe commands'}
    if($Programming){
        if(!$registrationValidated -or !(Test-Path -LiteralPath $registrationFile) -or
            (Get-FileHash -LiteralPath $registrationFile -Algorithm SHA256).Hash -ne $registrationSHA256){throw 'Validated device registration changed; no write'}
        if($Device -notin $loaderInfo.devices){throw 'Unregistered programming device; no write'}
    }
    if($Detection){
        if($Programming -or $ProbeList -or $Device -ne 'CORTEX-M4' -or
            ($Commands -join "`n") -cne (((Get-ControlledConnection)+@('mem32 0x40000000 2','regs')) -join "`n")){
            throw 'Detection only accepts the fixed Cortex-M4 connect/halt/register-read recipe'
        }
    }
    $script=Join-Path $operation ($Stem+'.jlink')
    # Disable close-time restart before any RAM load or later failing command.
    $commandLines=if($ProbeList -or $Metadata){$Commands+@('exit')}else{@('exec SetRestartOnClose = 0')+$Commands+@('qc')}
    [IO.File]::WriteAllLines($script,$commandLines,[Text.Encoding]::ASCII)
    # Programming aliases must be available at DLL startup, before auto-connect.
    # Detection/maintenance continue to use the generic non-programming core.
    $initial=if($Programming){$Device}else{'CORTEX-M4'}
    # Only the fixed initial connect/read recipe may continue after a failed
    # connect, as in the original tool. Captures/writes/verify stay fail-fast.
    $exitOnError=if($Detection){'0'}else{'1'}
    $argv=if($ProbeList -or $Metadata){@('-NoGui','1','-AutoConnect','0','-ExitOnError','1','-CommanderScript',$script)}else{
        @('-NoGui','1','-SelectEmuBySN',$JLinkSerial,'-Device',$initial,'-If','SWD','-Speed',([string]$connectKHz),'-AutoConnect','1','-ExitOnError',$exitOnError,'-CommanderScript',$script)
    }
    $line=($argv | ForEach-Object {if($_ -match '["\r\n]'){throw 'Unsafe native argument'};'"'+$_+'"'}) -join ' '
    $stdout=Join-Path $operation ($Stem+'.stdout.log');$stderr=Join-Path $operation ($Stem+'.stderr.log')
    $watch=[Diagnostics.Stopwatch]::StartNew();$natural=$false;$offset=0;$contactSeen=$false
    $copies=@();$sinks=@();$process=$null;$stdoutFrames=@()
    try {
        if($Programming){
            foreach($key in @('progress','percent','action','heartbeat','buffer')){$presentation.Remove($key)}
            $presentation['elapsed-seconds']=0
            # Buffer size 1 disables FileStream buffering; CopyToAsync drains
            # both pipes concurrently and preserves the exact original bytes.
            $sinks+= [IO.FileStream]::new($stdout,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite,1,$true)
            $sinks+= [IO.FileStream]::new($stderr,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite,1,$true)
            $process=Start-ControlledProgrammingProcess $JLinkExe (Split-Path $JLinkExe -Parent) $line
            $copies+= $process.StandardOutput.BaseStream.CopyToAsync($sinks[0])
            $copies+= $process.StandardError.BaseStream.CopyToAsync($sinks[1])
        }else{
            $process=Start-Process -FilePath $JLinkExe -WorkingDirectory (Split-Path $JLinkExe -Parent) -ArgumentList $line -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        }
        $null=$process.Handle
        while(!$process.HasExited -or @($copies | Where-Object {!$_.IsCompleted}).Count){
            foreach($copy in $copies){if($copy.IsFaulted){$null=$copy.GetAwaiter().GetResult()}}
            if($Programming -or $Detection){
                $text=Read-ControlledText $stdout
                if($Detection -and $text -match '(?im)^E000ED00\s*=\s*[0-9A-F]{8}'){$contactSeen=$true}
                if($Programming -and $text.Length -gt $offset){
                    $stdoutFrames+=@{elapsedMilliseconds=$watch.Elapsed.TotalMilliseconds;offsetCharacters=$offset;characters=$text.Length-$offset;finalDrain=$false}
                    $message=[Management.Automation.HostInformationMessage]::new();$message.Message=$text.Substring($offset);$message.NoNewLine=$true
                    Show-OmFullOutput -InputObject ([Management.Automation.InformationRecord]::new($message,'OM-controlled')) -State $presentation
                    $offset=$text.Length
                }
            }
            if($Programming){Show-OmFullHeartbeat $presentation $watch.Elapsed.TotalSeconds}
            $activeLimit=if($Detection -and !$contactSeen){[Math]::Min($AttemptTimeoutSeconds,$limit)}else{$limit}
            if($watch.Elapsed.TotalSeconds -ge $activeLimit){throw "Native timeout at $Stem; unknown outcome, no replay"}
            Start-Sleep -Milliseconds 200;$process.Refresh()
        }
        $process.WaitForExit();$natural=$true
        # .NET Framework exposes VoidTaskResult through PowerShell's dynamic
        # dispatch. Never leak those objects into the native stdout return.
        foreach($copy in $copies){$null=$copy.GetAwaiter().GetResult()}
        # WinPS Get-FileHash opens with read-only sharing; release the write
        # handles before recording the natural exit and hashes.
        foreach($sink in $sinks){$sink.Dispose()}
        $text=Read-ControlledText $stdout
        if($Detection -and $text -match '(?im)^E000ED00\s*=\s*[0-9A-F]{8}'){$contactSeen=$true}
        if($Programming -and $text.Length -gt $offset){
            $stdoutFrames+=@{elapsedMilliseconds=$watch.Elapsed.TotalMilliseconds;offsetCharacters=$offset;characters=$text.Length-$offset;finalDrain=$true}
            $message=[Management.Automation.HostInformationMessage]::new();$message.Message=$text.Substring($offset);$message.NoNewLine=$true
            Show-OmFullOutput -InputObject ([Management.Automation.InformationRecord]::new($message,'OM-controlled')) -State $presentation
        }
        if($Programming){Show-OmFullOutput -State $presentation -Final}
        $text+="`n"+(Read-ControlledText $stderr)
        # Arrival timing only: never derive a percentage or transfer speed from
        # time. This separates native buffering from terminal redraw coalescing.
        $nativeReceipt=@{naturalExit=$true;exitCode=$process.ExitCode;elapsedSeconds=$watch.Elapsed.TotalSeconds;contactSeen=$contactSeen;stdoutSHA256=(Get-FileHash $stdout -Algorithm SHA256).Hash;stderrSHA256=(Get-FileHash $stderr -Algorithm SHA256).Hash}
        if($Programming){$nativeReceipt.stdoutDeliveryFrames=$stdoutFrames}
        $nativeReceipt |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $operation ($Stem+'.native.json')) -Encoding UTF8
        $checked=$text
        if($Metadata -and $text -match '(?im)(Connecting to J-Link|Connecting to target|VTref\s*=)'){throw 'Unexpected probe access during metadata export; stop'}
        if($Detection){
            $firstRead=[regex]::Match($text,'(?im)^E000ED00\s*=\s*[0-9A-F]{8}')
            if($firstRead.Success){
                # Preserve original output/exit/SHA. Only explicit no-contact
                # lines BEFORE the first real register read can be recovered;
                # a read/SDK/probe error or later loss is never suppressed.
                $prefix=$text.Substring(0,$firstRead.Index)
                $prefix=[regex]::Replace($prefix,'(?im)^\s*(?:ERROR:\s*)?(?:Cannot connect to target|Could not connect to target|Failed to connect to target)\.?\s*\r?$','')
                $checked=$prefix+$text.Substring($firstRead.Index)
            }
        }
        if($process.ExitCode -ne 0 -or $checked -match '(?im)(cannot connect|could not connect|failed|error while|unknown command|illegal register name|syntax error|could not be halted)' -or
            ($Detection -and $checked -match '(?im)(^\s*(?:\*+\s*)?ERROR\s*:|No J-Link|No emulator|licen[cs](?:e|ing)[^\r\n]*(?:error|expired|invalid|denied|missing|required|not valid|not found)|no (?:valid )?licen[cs]e|access denied|timeout|timed out)')){
            throw "Native failure at $Stem; no retry. Preserve raw logs."
        }
        return $text
    } finally {
        if($null -ne $process -and !$process.HasExited){
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            if($Programming){$null=$process.WaitForExit(5000)}
        }
        foreach($copy in $copies){try{$null=$copy.Wait(5000)}catch{}}
        foreach($sink in $sinks){$sink.Dispose()}
        if($Programming){Complete-InkTagTerminalProgress}
        if(!$natural){@{naturalExit=$false;exitCode=$null;status='unknown';contactSeen=$contactSeen;elapsedSeconds=$watch.Elapsed.TotalSeconds;automaticReplay=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $operation ($Stem+'.native.json')) -Encoding UTF8}
    }
}

function Register-ControlledLoaders {
    # Commander 7.88j has no -JLinkDevicesXMLPath startup flag. Its supported
    # discovery directory is read on process startup. Publish only a unique
    # short-lived XML pointing at this operation's hash-bound ELF copies; never
    # overwrite the installed vendor pack or reuse another operation's alias.
    if($loaderInfo.namespace -notmatch '^[a-f0-9]{32}$'){throw 'Missing unique registration namespace'}
    $expected=@('INKTAG_DEV_OM_NOMBR','INKTAG_DEV_OM_MBR','INKTAG_DEV_OM_LAST') | ForEach-Object {$_+'_'+$loaderInfo.namespace}
    if(($loaderInfo.devices -join '|') -cne ($expected -join '|')){throw 'Unexpected controlled loader aliases; no target access'}
    $parent=Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'SEGGER/JLinkDevices'
    $candidate=Join-Path $parent ('InkTagOM-'+$loaderInfo.namespace)
    if(Test-Path -LiteralPath $candidate){throw 'Registration directory already exists; no reuse'}
    $null=New-Item -ItemType Directory -Path $candidate
    $script:registrationDirectory=$candidate
    $script:registrationFile=Join-Path $registrationDirectory 'JLinkDevices.xml'
    $source=Join-Path $loaderInfo.directory 'JLinkDevices.xml'
    $script:registrationSHA256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    if($registrationSHA256 -ne $loaderInfo.files.$source){throw 'Registration source changed'}
    Copy-Item -LiteralPath $source -Destination $registrationFile
    if((Get-FileHash -LiteralPath $registrationFile -Algorithm SHA256).Hash -ne $registrationSHA256){throw 'Registration copy differs'}
    $null=Invoke-ControlledNative 'loader-metadata' @('ExpDevList "'+(Join-Path $operation 'registered-devices.txt')+'"') -Metadata -TimeoutSeconds 15
    $metadata=Read-ControlledText (Join-Path $operation 'registered-devices.txt')
    foreach($name in $loaderInfo.devices){
        $pattern='(?m)^"Onmicro", "'+[regex]::Escape($name)+'", "Cortex-M4", \{0x00400000, 0x00100000\}, \{0x20000000, 0x00010000\}\r?$'
        if([regex]::Matches($metadata,$pattern).Count -ne 1){throw "Commander did not register $name; no target access"}
    }
    $script:registrationValidated=$true
    @{validated=$true;probeOpened=$false;registrationSHA256=$registrationSHA256;devices=$loaderInfo.devices;metadataSHA256=(Get-FileHash (Join-Path $operation 'registered-devices.txt') -Algorithm SHA256).Hash} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $operation 'loader-registration.json') -Encoding UTF8
}

function Remove-ControlledRegistration {
    if(!$registrationDirectory){return}
    # Remove only the two exact paths created by this invocation, never recurse.
    if($registrationFile -and (Test-Path -LiteralPath $registrationFile)){Remove-Item -LiteralPath $registrationFile -Force}
    if(Test-Path -LiteralPath $registrationDirectory){[IO.Directory]::Delete($registrationDirectory,$false)}
}

function Resolve-ControlledProbe([string]$RequestedSerial) {
    $method='explicit'
    if($RequestedSerial){
        if($RequestedSerial -notmatch '^[0-9]{4,16}$'){throw 'Invalid -JLinkSerial; expected an exact numeric serial'}
        $selected=$RequestedSerial
    } else {
        # USB enumeration only: never guess from the startup S/N banner, select
        # the first of multiple probes, discover remote probes or attach a CPU.
        $text=Invoke-ControlledNative 'probe-list' @('ShowEmuList USB') -ProbeList -TimeoutSeconds 8
        $rows=[regex]::Matches($text,'(?im)^\s*J-Link\[\d+\]:\s*Connection:\s*USB,\s*Serial number:\s*([0-9]{4,16})(?=\s|,|$)')
        if($rows.Count -eq 0){throw 'No unique USB J-Link found; connect a probe or specify -JLinkSerial. No target accessed'}
        if($rows.Count -ne 1){
            $serials=($rows | ForEach-Object {$_.Groups[1].Value}) -join ', '
            throw "Multiple USB J-Link probes found ($serials); specify -JLinkSerial. No target accessed"
        }
        $selected=$rows[0].Groups[1].Value;$method='unique-usb'
    }
    @{serial=$selected;method=$method;targetAccessed=$false} | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $operation 'probe-selection.json') -Encoding UTF8
    Write-InkTagTerminalMessage "Selected J-Link: $selected ($method)."
    return $selected
}

function Get-ControlledConnection([string]$Device='CORTEX-M4') {
    return @("device $Device",'si SWD',"speed $connectKHz",'connect','halt','mem32 0xE000ED00 1','mem32 0xE000EDF0 1')
}

function Test-ControlledDetectionRetryAllowed([string]$Stem) {
    # Retry only proven target absence after a natural native exit. A readable
    # target, probe error, partial/unknown timeout or later stage is not absence.
    try {
        $receipt=Get-Content -Raw -LiteralPath (Join-Path $operation ($Stem+'.native.json')) | ConvertFrom-Json
        if(!$receipt.naturalExit -or $null -eq $receipt.exitCode){return $false}
        $text=(Read-ControlledText (Join-Path $operation ($Stem+'.stdout.log')))+"`n"+(Read-ControlledText (Join-Path $operation ($Stem+'.stderr.log')))
        return ($text -match '(?i)(cannot connect to target|could not connect to target|failed to connect to target|failed to read SW-DP|SW-DP.*(failed|not found))' -and
            $text -notmatch '(?im)(E000ED00\s*=|40000000\s*=|^PC\s*=|Connected successfully|CPUID\s*[:=]|Cortex-M[0-9].*identified|No J-Link|Cannot connect to J-Link|Could not connect to J-Link|Failed to (connect to|open) (J-Link|probe|emulator)|unknown command|syntax error|licen[cs](?:e|ing)[^\r\n]*(?:error|expired|invalid|denied|missing|required|not valid|not found)|(?:error|missing|invalid|denied|failed)[^\r\n]*licen[cs]e|no (?:valid )?licen[cs]e|access denied|could not be halted|error while|failed prepare script|timeout|timed out)')
    } catch {return $false}
}

function Get-ControlledDetection {
    if($writeStarted){throw 'Detection retry is forbidden after write start'}
    $watch=[Diagnostics.Stopwatch]::StartNew();$attempt=0
    while($true){
        $remaining=$DetectTimeoutSeconds-$watch.Elapsed.TotalSeconds
        if($RetryDetect -and $remaining -le 0){throw 'Initial detection window expired; no Flash write started'}
        $attempt++;$stem='detect-{0:D3}' -f $attempt
        $limit=$OperationTimeoutSeconds
        if($RetryDetect){$limit=[Math]::Min($limit,$remaining)}
        Write-InkTagTerminalMessage ("Detecting OM6626 SWD, attempt $attempt (contact timeout ${AttemptTimeoutSeconds}s). Use the InkTag boot SWD window; stock or older builds without shared-pad protection may need BOOT low at power-on. No scripted reset; J-Link native connect fallback may apply.")
        try {
            $detected=Invoke-ControlledNative $stem ((Get-ControlledConnection)+@('mem32 0x40000000 2','regs')) -TimeoutSeconds $limit -Detection
            return $detected
        } catch {
            if(!$RetryDetect -or $writeStarted -or !(Test-ControlledDetectionRetryAllowed $stem)){throw}
            $remaining=$DetectTimeoutSeconds-$watch.Elapsed.TotalSeconds
            if($remaining -le 0){throw 'Initial detection window expired; no Flash write started'}
            Write-InkTagTerminalMessage ('Target not detected; retrying detection only ({0:N1}s remaining). Original attempt evidence retained.' -f $remaining) -Level WARNING
            Start-Sleep -Milliseconds ([int][Math]::Min(1000,[Math]::Ceiling($remaining*1000)))
        }
    }
}

function Get-ControlledBootState([string]$Detected) {
    $id=[regex]::Match($Detected,'(?im)^40000000\s*=\s*([0-9A-F]{8})\s+([0-9A-F]{8})')
    if(!$id.Success -or ([Convert]::ToUInt32($id.Groups[1].Value,16) -band 0xF00) -ne 0x200 -or ([Convert]::ToUInt32($id.Groups[2].Value,16) -band 0xffff) -ne 0x6626){throw 'Exact OM6626 identity not observed'}
    $halt=[regex]::Match($Detected,'(?im)^E000EDF0\s*=\s*([0-9A-F]{8})')
    if(!$halt.Success -or ([Convert]::ToUInt32($halt.Groups[1].Value,16) -band 0x20000) -eq 0){throw 'Target halt not demonstrated; no RAM capture or Flash write started'}
    $pc=[regex]::Match($Detected,'(?im)^PC\s*=\s*([0-9A-F]{8})')
    if(!$pc.Success){throw 'Target PC not observed; no RAM capture or Flash write started'}
    $address=[Convert]::ToUInt32($pc.Groups[1].Value,16)
    if($address -ge 0x00100000 -and $address -lt 0x00104000){return 'rom-isp'}
    return 'swd-attached'
}

function Enter-ControlledMaintenance {
    # Full only, AFTER the exact SYS identity/halt gate. The pinned vendor
    # loader's Init also stops WDT with this key (PrgCode 0x2EC4..0x2ED4).
    # Our earlier RAM reader bypassed Init, leaving a running App/ROM watchdog
    # able to reset the core during the download or between reader processes.
    # Do not change BOOT_SEL, reset flags, Flash status/protection or firmware.
    $commands=@(Get-ControlledConnection)
    $commands+=@('mem32 0x40000000 2','regs','mem32 0x400E0094 1',
        'w4 0x400E00E8 0x00006666','sleep 20','mem32 0x400E00E8 1',
        'mem32 0x400E0094 1','sleep 20','mem32 0x400E0094 1','mem32 0xE000EDF0 1')
    $text=Invoke-ControlledNative 'full-maintenance' $commands
    $null=Get-ControlledBootState $text
    $halted=[regex]::Matches($text,'(?im)^E000EDF0\s*=\s*([0-9A-F]{8})')
    if($halted.Count -ne 2 -or @($halted | Where-Object {([Convert]::ToUInt32($_.Groups[1].Value,16) -band 0x20000) -eq 0}).Count){
        throw 'Full maintenance did not remain halted; no RAM download or Flash write'
    }
    $key=[regex]::Matches($text,'(?im)^400E00E8\s*=\s*([0-9A-F]{8})')
    $status=[regex]::Matches($text,'(?im)^400E0094\s*=\s*([0-9A-F]{8})')
    if($key.Count -ne 1 -or $status.Count -ne 3){throw 'Incomplete Full watchdog maintenance readback; no RAM download or Flash write'}
    $effective=[Convert]::ToUInt32($key[0].Groups[1].Value,16)
    $first=[Convert]::ToUInt32($status[1].Groups[1].Value,16)
    $last=[Convert]::ToUInt32($status[2].Groups[1].Value,16)
    # SDK PMU_WDT_KR_CFG.WDT_KR is the synchronized effective key (bits31:16),
    # not merely the requested low half. LD_WDT_KR_STATUS must be clear and the
    # 128 Hz timer must remain stable over >2 ticks, even with the core halted.
    if(($effective -shr 16) -ne 0x6666 -or (($first -bor $last) -band 1) -ne 0 -or
        ($first -band 0xfffff000L) -ne ($last -band 0xfffff000L)){
        throw 'Full watchdog stop not demonstrated; no RAM download or Flash write'
    }
    @{scope='Full-maintenance-only';watchdogKey=$effective;statusBefore=$status[0].Groups[1].Value;
        statusAfter=@($first,$last);firmwareWatchdogPolicyChanged=$false;flashWritten=$false;automaticReplay=$false} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $operation 'full-maintenance.json') -Encoding UTF8
    Write-InkTagTerminalMessage ('OM6626 Full maintenance ready: watchdog stopped and checked; attach {0} kHz, transfer {1} kHz. No Flash write started.' -f $connectKHz,[int]($FrequencyHz/1000)) -Level SUCCESS
}

function Clear-ControlledHistoryMailbox($Description) {
    # After all Flash algorithms/readback have returned, with target halted.
    # Clear only the actual44B health mailbox in the existing v1 reserved ABI.
    if($Description.address -ne 0x20001fc0 -or $Description.bytes -ne 44){
        throw 'Unqualified retained history window; no further operation'
    }
    $commands=@(Get-ControlledConnection)
    $commands+=@('mem32 0x40000000 2','regs','mem32 0xE000EDF0 1')
    for($at=0x20001fc0;$at -lt 0x20001fec;$at+=4){
        $commands+=('w4 0x{0:X8} 0x00000000' -f $at)
    }
    # Commander parses numeric counts as hexadecimal: 0xB is eleven words.
    $commands+=@('mem32 0x20001FC0 0xB','mem32 0xE000EDF0 1')
    $text=Invoke-ControlledNative 'clear-retained-history' $commands
    $null=Get-ControlledBootState $text
    $halts=[regex]::Matches($text,'(?im)^E000EDF0\s*=\s*([0-9A-F]{8})')
    # Connection prefix, pre-write gate and final readback each read DHCSR.
    if($halts.Count -ne 3 -or @($halts | Where-Object {([Convert]::ToUInt32($_.Groups[1].Value,16) -band 0x20000) -eq 0}).Count){
        throw 'Retained history cleanup halt/readback not demonstrated; do not resume'
    }
    $words=@{}
    foreach($line in [regex]::Matches($text,'(?im)^([0-9A-F]{8})[ \t]*=[ \t]*([0-9A-F]{8}(?:[ \t]+[0-9A-F]{8})*)[ \t]*\r?$')){
        $start=[Convert]::ToUInt32($line.Groups[1].Value,16);$index=0
        foreach($word in ($line.Groups[2].Value.Trim() -split '\s+')){
            $at=$start+4*$index;$index++
            if($at -ge 0x20001fc0 -and $at -lt 0x20001fec){
                if($words.ContainsKey($at)){throw 'Duplicate retained history readback; no success'}
                $words[$at]=[Convert]::ToUInt32($word,16)
            }
        }
    }
    if($words.Count -ne 11 -or @($words.Values | Where-Object {$_ -ne 0}).Count){
        throw 'Retained history cleanup readback differs; no success or replay'
    }
    @{address=0x20001fc0;bytes=44;zeroReadbackVerified=$true;targetHalted=$true;
      automaticReplay=$false;automaticReset=$false;postBootHistoryVerified=$false;
      bootCauseClearance='complete-power-cycle-required'} | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $operation 'retained-history-cleanup.json') -Encoding UTF8
}

function Read-ControlledRange([string]$Stem,[uint32]$Start,[uint32]$End,[string]$Destination,[switch]$WholeRead) {
    if($Start -lt 0x00400000 -or $End -gt 0x004FFFFF -or $End -lt $Start){throw 'Read range outside main Flash'}
    if(Test-Path -LiteralPath $Destination){throw 'Read destination exists; no replay'}
    foreach($item in $readInputs.GetEnumerator()){
        if((Get-FileHash -LiteralPath $item.Key -Algorithm SHA256).Hash -ne $item.Value){throw 'J-Flash read inputs changed; no device access'}
    }
    $count=[int64]$End-$Start+1
    $flog=Join-Path $operation ($Stem+'.jflash.log');$jlog=Join-Path $operation ($Stem+'.jlink.log')
    $stdout=Join-Path $operation ($Stem+'.stdout.log');$stderr=Join-Path $operation ($Stem+'.stderr.log')
    # Restore the former fixed J-Flash read/save recipe. Never pass erase,
    # program, auto or start-app actions to this read-only process.
    $argv=@(('-usb'+$JLinkSerial),'-hide',('-jlinkdevicesxmlpath'+$readDeviceXml),('-jflashlog'+$flog),('-jlinklog'+$jlog),
        ('-openprj'+$readProject),('-readrange0x{0:X8},0x{1:X8}' -f $Start,$End),
        ('-saveas{0},0x{1:X8},0x{2:X8}' -f $Destination,$Start,$End),'-exit')
    $line=($argv | ForEach-Object {if($_ -match '["\r\n]'){throw 'Unsafe J-Flash read argument'};'"'+$_+'"'}) -join ' '
    $process=$null;$natural=$false;$watch=[Diagnostics.Stopwatch]::StartNew();$state=@{'trace-enabled'=$true}
    $callbackObserved=$false;$explicitGoObserved=$false
    Write-InkTagTerminalMessage ("$Stem : reading $count bytes with J-Flash.")
    try {
        # The callback basename resolves in the private project directory.
        # Keep it ASCII without narrowing supported Chinese/space bundle paths.
        $process=Start-Process -FilePath $jflashExe -WorkingDirectory $operation -ArgumentList $line -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $null=$process.Handle
        while(!$process.HasExited){
            if($WholeRead){
                Show-OmBackupLog -Paths @($flog,$jlog,$stdout,$stderr) -State $state -Stage 'Complete Flash readback'
                Show-OmBackupHeartbeat -State $state -Stage 'Complete Flash readback' -ElapsedSeconds $watch.Elapsed.TotalSeconds
            }
            if($watch.Elapsed.TotalSeconds -ge $OperationTimeoutSeconds){throw "J-Flash timeout at $Stem; no retry"}
            Start-Sleep -Milliseconds 200;$process.Refresh()
        }
        $process.WaitForExit();$natural=$true
        if($WholeRead){Show-OmBackupLog -Paths @($flog,$jlog,$stdout,$stderr) -State $state -Stage 'Complete Flash readback' -Final}
        $log=Read-ControlledText $flog
        # 7.88j emits a harmless empty CurrentFile warning before a read-only
        # -readrange. Keep it raw; exclude only that exact warning pair.
        $begin=[regex]::Match($log,'(?m)^Reading target memory \(0x[0-9A-F]+ - 0x[0-9A-F]+\)')
        $dllLog=Read-ControlledText $jlog
        $checked=$log+"`n"+(Read-ControlledText $stdout)+"`n"+(Read-ControlledText $stderr)
        $callbackObserved=($dllLog+"`n"+$checked) -cmatch 'OM_FULL_READ_CLOSE_HALT_OK'
        $explicitGoObserved=$dllLog -match '(?im)JLINK(?:ARM)?_Go\s*\('
        $checked=[regex]::Replace($checked,'(?m)^No data file specified!\r?\nFailed to open data file \[[^\r\n]*\]\.\.\.\r?\n','')
        $fatal='(?im)^\s*(?:-\s*)?(?:\*+\s*)?(?:Error\b|Failed\b|Cannot\b|Could not\b|Unknown command\b|Syntax error\b|No J-Link\b|No emulator\b|Timeout\b|Timed out\b|Target memory (?:read|save) failed\b)|^J-Flash V[^:\r\n]+:\s*(?:Error\b|Failed\b|Cannot\b|Could not\b)'
        if($process.ExitCode -ne 0 -or $log -notmatch 'J-Flash V7\.88j' -or
            !$begin.Success -or $checked -match $fatal -or
            !$callbackObserved -or $explicitGoObserved -or
            $log -notmatch [regex]::Escape(('Reading target memory (0x{0:X8} - 0x{1:X8})' -f $Start,$End)) -or
            $log -notmatch [regex]::Escape("Target memory read successfully. ($count bytes, 1 range)") -or
            $log -notmatch [regex]::Escape("Data file saved successfully ($count bytes, 1 range)") -or
            !(Test-Path -LiteralPath $Destination -PathType Leaf) -or (Get-Item -LiteralPath $Destination).Length -ne $count){
            throw "J-Flash read/save failed at $Stem; no retry"
        }
        Write-InkTagTerminalMessage ("$Stem : read and saved $count bytes; native read receipt verified.")
    } finally {
        if($null -ne $process -and !$process.HasExited){$process.Kill();if(!$process.WaitForExit(2000)){throw 'J-Flash reader did not stop; no further operation'}}
        $receipt=@{transport='jflash-readrange';start=$Start;endInclusive=$End;bytes=$count;arguments=$argv;naturalExit=$natural;
            closePolicyCallbackObserved=$callbackObserved;explicitGoObserved=$explicitGoObserved;postDisconnectHaltObserved=$false;
            exitCode=$(if($natural){$process.ExitCode}else{$null});elapsedSeconds=$watch.Elapsed.TotalSeconds;automaticReplay=$false}
        foreach($path in @($stdout,$stderr,$flog,$jlog)){
            if(Test-Path -LiteralPath $path -PathType Leaf){$receipt[(Split-Path -Leaf $path)+'SHA256']=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
        }
        $receipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $operation ($Stem+'.native.json')) -Encoding UTF8
    }
}

function New-ControlledReadProject {
    param([string]$ToolsDirectory=$PSScriptRoot)
    # Keep the maintained no-action template unchanged. Only this Full
    # invocation gets a byte-bound callback; backup/probe are not changed.
    $readTemplate=[IO.File]::ReadAllText((Join-Path $ToolsDirectory 'om6626-readonly-v788j.jflash'))
    if([regex]::Matches($readTemplate,'(?m)^  Speed[01] = 100\r?$').Count -ne 2 -or
        $readTemplate -notmatch 'ChipName = "Onmicro OM662XX_NOMBR"' -or
        $readTemplate -match '(?m)^  (?:NumInitSteps|NumExitSteps|UseScriptFile|AutoPerforms\w+) = [1-9]'){
        throw 'Maintained J-Flash read-only project changed; no target access'
    }
    foreach($field in @('NumInitSteps','NumExitSteps','UseScriptFile','AutoPerformsDisconnect','AutoPerformsErase','AutoPerformsProgram','AutoPerformsStartApp','AutoPerformsVerify','EnableTargetPower')){
        if([regex]::Matches($readTemplate,('(?m)^  '+$field+' = [^\r\n]*\r?$')).Count -ne 1 -or
            $readTemplate -notmatch ('(?m)^  '+$field+' = 0\r?$')){throw 'Read-only project enables an unsupported action; no target access'}
    }
    if([regex]::Matches($readTemplate,'(?m)^  ScriptFile = ""\r?$').Count -ne 1 -or
        [regex]::Matches($readTemplate,'(?m)^  ScriptFile = ').Count -ne 1){throw 'Unexpected J-Flash script binding; no target access'}
    $callbackSource=Join-Path $ToolsDirectory 'internal/full-read-halt.JLinkScript'
    $callback=Join-Path $operation 'full-read-halt.JLinkScript'
    if(Test-Path -LiteralPath $callback){throw 'Full read callback already exists; no reuse'}
    $callbackBytes=[IO.File]::ReadAllBytes($callbackSource)
    [IO.File]::WriteAllBytes($callback,$callbackBytes)
    $readTemplate=[regex]::Replace($readTemplate,'(?m)^  Speed0 = 100(?=\r?$)',('  Speed0 = '+$connectKHz))
    $readTemplate=[regex]::Replace($readTemplate,'(?m)^  Speed1 = 100(?=\r?$)',('  Speed1 = '+[int]($FrequencyHz/1000)))
    $readTemplate=[regex]::Replace($readTemplate,'(?m)^  UseScriptFile = 0(?=\r?$)','  UseScriptFile = 1')
    # Project directory and native cwd are both this private operation folder.
    $readTemplate=[regex]::Replace($readTemplate,'(?m)^  ScriptFile = ""(?=\r?$)','  ScriptFile = "full-read-halt.JLinkScript"')
    [IO.File]::WriteAllText($readProject,$readTemplate,[Text.Encoding]::ASCII)
    return @{$readProject=(Get-FileHash -LiteralPath $readProject -Algorithm SHA256).Hash;
             $callback=(Get-FileHash -LiteralPath $callback -Algorithm SHA256).Hash}
}

function Test-ControlledIdentityAttachRetryAllowed([string]$NativeStem) {
    # Only the very first identity attachment, after Full maintenance. A
    # completed native failure must prove that NO RAM write/run was reached.
    if($NativeStem -cne 'source-capture-identity-before'){return $false}
    $receiptPath=Join-Path $operation ($NativeStem+'.native.json')
    if(!(Test-Path -LiteralPath $receiptPath)){return $false}
    try {
        $receipt=Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
        if(!$receipt.naturalExit -or $receipt.exitCode -ne 1){return $false}
        $outPath=Join-Path $operation ($NativeStem+'.stdout.log');$errPath=Join-Path $operation ($NativeStem+'.stderr.log')
        if((Get-FileHash -LiteralPath $outPath -Algorithm SHA256).Hash -ne $receipt.stdoutSHA256 -or
            (Get-FileHash -LiteralPath $errPath -Algorithm SHA256).Hash -ne $receipt.stderrSHA256){return $false}
        $text=Read-ControlledText $outPath;$errorText=Read-ControlledText $errPath
        if($errorText.Trim() -or $text -notmatch '(?m)^CPU could not be halted\r?$' -or
            $text -notmatch '(?m)^Script processing completed\.\r?$' -or
            $text -match '(?i)(Connect fallback|reset via|unknown command|illegal register|syntax error|licen[cs][^\r\n]*(?:error|expired|denied|missing)|access denied|timeout|timed out)'){return $false}
        $otherErrors=[regex]::Replace($text,'(?m)^Could not read memory\.\r?$','')
        if($otherErrors -match '(?im)^\s*(?:\*+\s*)?(?:ERROR\s*:|Error while|Failed|Cannot|Could not|Unknown)'){return $false}
        # Require the exact read-only connection prefix, not merely absence of
        # a success marker. Even the first loadbin echo forbids replay.
        $seen=@([regex]::Matches($text,'(?m)^J-Link>([^\r\n]*)') | ForEach-Object {$_.Groups[1].Value.Trim()})
        $prefix=@('exec SetRestartOnClose = 0','device CORTEX-M4','si SWD',"speed $connectKHz",'connect','halt','mem32 0xE000ED00 1')
        if($seen.Count -notin @(6,7)){return $false}
        return (($seen -join "`n") -ceq ($prefix[0..($seen.Count-1)] -join "`n"))
    }catch{return $false}
}

function Read-ControlledIdentity([string]$Directory,[string]$Stem,[string]$Suffix) {
    $code=Join-Path $Directory 'identity-loader-RAM.bin';$trampoline=Join-Path $Directory 'identity-trampoline-RAM.bin';$request=Join-Path $Directory 'identity-request.bin'
    $commands=@(Get-ControlledConnection)
    # Use the same connect-slow/transfer-fast split as the old vendor writer.
    $commands+='speed '+[int]($FrequencyHz/1000)
    $commands+=@('mem32 0x400E00E8 1','mem32 0x400E0094 1')
    $commands+=@("loadbin `"$code`", 0x20000000, noreset","loadbin `"$trampoline`", 0x20012000, noreset",
        "loadbin `"$request`", 0x20013000, noreset",'wreg "R13 (SP)" 0x20013D00','wreg "R15 (PC)" 0x20012001',
        'g','sleep 100','halt',("savebin `"{0}`", 0x20013000, 0x40" -f (Join-Path $Directory ('identity-'+$Suffix+'.bin'))),
        'mem32 0xE000EDF0 1','mem32 0x400E00E8 1','mem32 0x400E0094 1')
    $nativeStem=$Stem+'-identity-'+$Suffix
    for($attachAttempt=0;$attachAttempt -lt 2;$attachAttempt++){
        $attemptStem=if($attachAttempt -eq 0){$nativeStem}else{$nativeStem+'-attach-retry-001'}
        try {$text=Invoke-ControlledNative $attemptStem $commands;break}
        catch {
            if($attachAttempt -ne 0 -or !(Test-ControlledIdentityAttachRetryAllowed $nativeStem)){throw}
            Write-InkTagTerminalMessage 'Initial identity attachment lost before any RAM write/run; retrying attachment once. First raw failure retained; no scripted reset or Flash replay.' -Level WARNING
            Start-Sleep -Milliseconds 200
        }
    }
    $halted=[regex]::Matches($text,'(?im)^E000EDF0\s*=\s*([0-9A-F]{8})')
    if($halted.Count -lt 2 -or @($halted | Where-Object {([Convert]::ToUInt32($_.Groups[1].Value,16) -band 0x20000) -eq 0}).Count){throw 'Captured target halt was not demonstrated'}
    $keys=[regex]::Matches($text,'(?im)^400E00E8\s*=\s*([0-9A-F]{8})')
    $status=[regex]::Matches($text,'(?im)^400E0094\s*=\s*([0-9A-F]{8})')
    if($text -match '(?i)implicit reset|reset via|reset fallback' -or $keys.Count -ne 2 -or $status.Count -ne 2 -or
        @($keys | Where-Object {([Convert]::ToUInt32($_.Groups[1].Value,16) -shr 16) -ne 0x6666}).Count -or
        @($status | Where-Object {([Convert]::ToUInt32($_.Groups[1].Value,16) -band 1) -ne 0}).Count -or
        $status[0].Groups[1].Value -ne $status[1].Groups[1].Value){
        throw 'Full maintenance context changed during identity read; no Flash write or replay'
    }
}

function Read-ControlledCapture([string]$Directory,[string]$Stem) {
    New-Item -ItemType Directory -Path $Directory | Out-Null
    $staged=Invoke-ControlledPython @((Join-Path $root 'tools/hardware/onmicro_flash_identity.py'),'stage','--directory',$Directory,'--loader',$rawLoader)
    if($staged.ExitCode -ne 0){throw ('Pinned RAM identity staging refused: '+($staged.Lines -join "`n"))}
    Read-ControlledIdentity $Directory $Stem 'before'
    foreach($span in @(@(0,0x4000,'protected'),@(0x80000,0x2000,'runtime-mbr'),@(0xf9000,0x7000,'factory-records'))){
        $offset=[int]$span[0];$length=[int]$span[1];$name=[string]$span[2]
        $image=Join-Path $Directory ($name+'.bin')
        Read-ControlledRange ($Stem+'-'+$name) (0x400000+$offset) (0x400000+$offset+$length-1) $image
        $bytes=[IO.File]::ReadAllBytes($image)
        if($bytes.Length -ne $length){throw 'Short J-Flash capture; no blank inference'}
        foreach($part in $description.chunks){
            if($part.offset -ge $offset -and $part.offset+$part.length -le $offset+$length){
                $data=[byte[]]::new([int]$part.length);[Array]::Copy($bytes,([int]$part.offset-$offset),$data,0,$data.Length)
                [IO.File]::WriteAllBytes((Join-Path $Directory $part.file),$data)
            }
        }
    }
    # J-Flash owns/restores its RAM workspace, so restage only the small fixed
    # identity reader after those reads. No old success/result is reused.
    Read-ControlledIdentity $Directory $Stem 'after'
}

try {
    . (Join-Path $root 'tools/powershell-cli.ps1')
    Set-InkTagTerminalEnglish
    $guard=Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help -RemainingArguments $RemainingArguments -Summary 'OM Complete Flash: Firmware + Config; optional JLinkSerial (auto-select only one USB probe). Scope=mcu/main-flash. Preserves freshly read valid native same-unit Pro; optional old state defaults, unverifiable old Pro becomes Free with warning; clears main-Flash settings/password/QR/Find/fonts/images/crash-health history; external Flash is not accessed or erased, external images retained. Default y/N; -Yes only confirms. DryRun never accesses hardware. Identity/calibration and package signature/target/capacity checks remain. Corrected InkTag builds reserve a normal-boot SWD window; stock or older builds may require BOOT low at power-on.'
    if($null -ne $guard){exit $guard}
    if(!$Config -or !$Firmware){throw '-Config and -Firmware are required'}
    $pythonExe=(Get-Command $Python -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $described=Invoke-ControlledPython @($helper,'describe','--config',$Config,'--base',$Firmware)
    $descriptionText=$described.Lines
    if($described.ExitCode -ne 0){throw ('Complete Flash local validation refused: '+($descriptionText -join "`n"))}
    $description=($descriptionText -join "`n") | ConvertFrom-Json
    Write-InkTagTerminalMessage ('Complete Flash scope=mcu/main-flash: preserves freshly read valid native same-unit Pro; optional old state defaults, unverifiable old Pro becomes Free with warning; clears main-Flash settings, password/QR, Find, user fonts/images and crash/health history. External Flash is not accessed or erased; external image data is retained. Immutable identity, calibration, protected boot/CPFT and vendor data are retained. Selected board: '+$description.board+'. No backup or BLE operation is performed.') -Level WARNING
    Write-InkTagTerminalMessage 'Full maintenance halts the target and temporarily stops its watchdog before RAM acquisition. On success or failure, fully power-cycle manually; do not resume the previous App.' -Level WARNING
    if($SkipReadbackVerify){Write-InkTagTerminalMessage 'Full readback will be skipped: NOT VERIFIED. Verification of both new Recovery pages and Settings remains mandatory.' -Level WARNING}
    if($DryRun){Write-InkTagTerminalMessage 'DryRun passed. Package signatures and Config validated; no device accessed, no Flash written. Physical capacity and Factory preservation are checked only during execution.' -Level INFO;exit 0}
    if(!$Yes -and (Read-Host 'Continue complete flash? [y/N]') -notmatch '^(?i:y|yes)$'){Write-InkTagTerminalMessage 'Cancelled. No device accessed; no Flash written.';exit 0}
    if($env:OS -ne 'Windows_NT'){throw 'Linux OM programming backend is not qualified; no device accessed'}
    if($JLinkSerial -and $JLinkSerial -notmatch '^[0-9]{4,16}$'){throw 'Invalid -JLinkSerial; expected an exact numeric serial'}
    Write-InkTagTerminalMessage 'Preparing local inputs. Wait for the power-on prompt. Stock or older InkTag builds without NFC/SWD shared-pad protection may require BOOT low at power-on. No scripted reset or write retry; J-Link native connect fallback may apply.' -Level WARNING
    if($RetryDetect){Write-InkTagTerminalMessage "Initial detection retry enabled for $DetectTimeoutSeconds seconds only; captures and Flash writes are never replayed."}
    else{Write-InkTagTerminalMessage 'Initial detection retry disabled; use -RetryDetect to enable a bounded detection window.'}
    $JLinkExe=Resolve-InkTagJLinkTool -ToolName JLink.exe -ExplicitPath $JLinkExe -RepoRoot $root
    Assert-InkTagJLink788j -JLinkExe $JLinkExe
    $jflashExe=Resolve-InkTagJLinkTool -ToolName JFlash.exe -ExplicitPath (Join-Path (Split-Path -Parent $JLinkExe) 'JFlash.exe') -RepoRoot $root
    $pack=Join-Path (Get-InkTagJLinkDeviceRoot) 'InkTag/OM6626'
    $rawLoader=Join-Path $pack 'Devices/Onmicro/OM662XX_nombr.elf'
    foreach($pair in @(@($rawLoader,'9A53C01B9363371E8EFF44A7A9DE80E2942E96025AEDE3ADE8053875AB6AD640'),@((Join-Path $pack 'Devices/Onmicro/OM662XX.elf'),'8523643FC138B84601C850571499068D36DA954B60E7A3AA0BE68386AD8E13AF'))){
        if((Get-FileHash -LiteralPath $pair[0] -Algorithm SHA256).Hash -ne $pair[1]){throw 'Maintained vendor loader differs; no device accessed'}
    }
    $operation=Join-Path $root ('.local/om-full/'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $operation | Out-Null
    @{requestedJLinkSerial=$JLinkSerial;retryDetect=[bool]$RetryDetect;detectTimeoutSeconds=$DetectTimeoutSeconds;attemptTimeoutSeconds=$AttemptTimeoutSeconds;frequencyHz=$FrequencyHz;operationTimeoutSeconds=$OperationTimeoutSeconds;skipReadbackVerify=[bool]$SkipReadbackVerify} | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $operation 'operator-options.json') -Encoding UTF8
    $configCopy=Join-Path $operation ('board'+[IO.Path]::GetExtension($Config));$baseCopy=Join-Path $operation 'app-full.zip'
    Copy-Item -LiteralPath $Config -Destination $configCopy;Copy-Item -LiteralPath $Firmware -Destination $baseCopy
    $admitted=Invoke-ControlledPython @($helper,'describe','--config',$configCopy,'--base',$baseCopy)
    $admission=$admitted.Lines
    if($admitted.ExitCode -ne 0){throw 'Snapshotted inputs refused before device access'}
    $description=($admission -join "`n") | ConvertFrom-Json
    $loaders=Invoke-ControlledPython @((Join-Path $root 'tools/hardware/onmicro_controlled_loader.py'),'--pack',$pack,'--out',(Join-Path $operation 'loaders'),'--namespace',([Guid]::NewGuid().ToString('N')))
    if($loaders.ExitCode -ne 0){throw 'Fixed operation-local loader registration refused before device access'}
    $loaderInfo=($loaders.Lines -join "`n") | ConvertFrom-Json
    Register-ControlledLoaders
    $readDeviceXml=Join-Path $pack 'JLinkDevices.xml';$readProject=Join-Path $operation 'readonly.jflash'
    $readInputs=New-ControlledReadProject
    $readInputs[$readDeviceXml]=([string]$loaderInfo.sourceXMLSHA256).ToUpperInvariant()
    $readInputs[$rawLoader]=([string]$loaderInfo.originalNoMBRSHA256).ToUpperInvariant()
    $readInputs | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $operation 'read-inputs.json') -Encoding UTF8
    $JLinkSerial=Resolve-ControlledProbe $JLinkSerial
    $lock=[Threading.Mutex]::new($false,('Local\InkTagOMControlled-'+$JLinkSerial));$locked=$lock.WaitOne(0)
    if(!$locked){throw 'Selected probe is busy; no target access started'}
    Write-InkTagTerminalStage 'COMPLETE FLASH / EXACT PROBE AND SWD TARGET'
    Write-InkTagTerminalMessage 'Detection ready. Power on InkTag now to use its 30-second SWD window. Stock or older builds without NFC/SWD shared-pad protection may require BOOT low at power-on.' -Level WARNING
    $targetAccessStarted=$true
    $detected=Get-ControlledDetection
    $bootState=Get-ControlledBootState $detected
    if($bootState -eq 'rom-isp'){Write-InkTagTerminalMessage 'OM6626 halted in ROM ISP. Release BOOT if held; do not reset.' -Level WARNING}
    else{Write-InkTagTerminalMessage 'OM6626 SWD attached outside ROM ISP. BOOT low is not required; do not reset.'}
    # This is not a probe/read-only diagnostic: Full owns a halted maintenance
    # context until manual cold power cycle, including failures before erase.
    $maintenanceStarted=$true
    Enter-ControlledMaintenance
    $ramCaptureStarted=$true
    $capture=Join-Path $operation 'capture';Read-ControlledCapture $capture 'source-capture'
    Write-InkTagTerminalStage 'COMPLETE FLASH / NATIVE INITIALIZATION'
    $prepared=Join-Path $operation 'prepared'
    $preparation=Invoke-ControlledPython @($helper,'prepare','--config',$configCopy,'--base',$baseCopy,'--capture-dir',$capture,'--out',$prepared)
    $result=$preparation.Lines
    $result | Out-File -LiteralPath (Join-Path $operation 'preparation.log') -Encoding utf8
    if($preparation.ExitCode -ne 0){throw ('Factory preservation/initialization refused before erase: '+($result -join "`n"))}
    $info=($result -join "`n") | ConvertFrom-Json
    foreach($warning in @($info.warnings)){Write-InkTagTerminalMessage ([string]$warning) -Level WARNING}
    $confirm=Join-Path $operation 'before-write';Read-ControlledCapture $confirm 'fresh-source-confirmation'
    foreach($part in $description.chunks){if((Get-FileHash (Join-Path $capture $part.file) -Algorithm SHA256).Hash -ne (Get-FileHash (Join-Path $confirm $part.file) -Algorithm SHA256).Hash){throw 'Source changed before erase; no write'}}
    $a=[IO.File]::ReadAllBytes((Join-Path $capture 'identity-before.bin'));$b=[IO.File]::ReadAllBytes((Join-Path $confirm 'identity-after.bin'))
    if([Convert]::ToBase64String($a,0,48) -ne [Convert]::ToBase64String($b,0,48)){throw 'Unit changed before erase'}
    # Validate the fresh reader's nonce/repeat/length rules too, without another preparation.
    $fresh=Invoke-ControlledPython @('-c','import sys;sys.path.insert(0,sys.argv[1]);from onmicro_full_prepare import capture_from_directory;capture_from_directory(sys.argv[2])',(Join-Path $root 'tools/hardware'),$confirm)
    if($fresh.ExitCode -ne 0){throw 'Fresh identity/coverage confirmation refused'}
    $latch=[IO.File]::Open((Join-Path $operation 'WRITE-STARTED-NO-REPLAY.json'),'CreateNew','Write')
    try{$bytes=[Text.Encoding]::UTF8.GetBytes((@{planSHA256=$info.planSha256;status='unknown';automaticReplay=$false}|ConvertTo-Json));$latch.Write($bytes,0,$bytes.Length);$latch.Flush($true)}finally{$latch.Dispose()}
    $writeStarted=$true
    Write-InkTagTerminalMessage 'Target is halted. Keep BOOT released; do not reset. Programming starts in 3 seconds.' -Level WARNING
    Start-Sleep -Seconds 3
    $names=@('factory-state','factory-code','factory-device-info');$devices=$loaderInfo.devices
    for($i=0;$i -lt 3;$i++){
        $name=$names[$i];$device=$devices[$i];$image=[string]$info.images.$name
        if((Get-FileHash $image -Algorithm SHA256).Hash -ne $info.imageSha256.$name){throw 'Prepared image changed; no replay'}
        foreach($item in $loaderInfo.files.PSObject.Properties){if((Get-FileHash -LiteralPath $item.Name -Algorithm SHA256).Hash -ne $item.Value){throw 'Operation-local loader/registration changed; no replay'}}
        $commands=@(Get-ControlledConnection $device);$commands+='speed '+[int]($FrequencyHz/1000)
        if($i -eq 1){$commands+='erase 0x00404000 0x00477FFF'}
        $commands+='loadfile "'+$image+'"';$presentation['stage']="Complete Flash $($i+1)/3 - $name"
        Write-InkTagTerminalStage $presentation['stage']
        $written=Invoke-ControlledNative $name $commands $device -Programming
        if($written -notmatch '(?im)^J-Link: Flash download:|flash contents already match'){throw 'Missing programmer completion; no success or retry'}
    }
    if($SkipReadbackVerify){
        Write-InkTagTerminalStage 'COMPLETE FLASH / VERIFY NEW RECOVERY AND SETTINGS'
        $nativeCheck=Join-Path $operation 'native-readback';New-Item -ItemType Directory -Path $nativeCheck | Out-Null
        $base=[int]$info.settings_base
        Read-ControlledRange 'native-recovery-readback' 0x00482000 0x00483FFF (Join-Path $nativeCheck 'init-082000.bin')
        for($at=$base;$at -lt 0x100000;$at+=8192){
            $length=[Math]::Min(8192,0x100000-$at)
            Read-ControlledRange ('native-settings-{0:x6}' -f $at) (0x400000+$at) (0x400000+$at+$length-1) (Join-Path $nativeCheck ('init-{0:x6}.bin' -f $at))
        }
        $verified=Invoke-ControlledPython @($helper,'verify-initialization','--config',$configCopy,'--base',$baseCopy,'--capture-dir',$nativeCheck,'--out',$info.expected,'--expected-sha256',$info.expectedSha256)
        if($verified.ExitCode -ne 0){throw ('Mandatory native initialization verification failed: '+($verified.Lines -join "`n"))}
        Clear-ControlledHistoryMailbox $info.retainedHistoryMailbox
        @{status='write-completed-not-verified';scope='mcu/main-flash';externalFlashAccessed=$false;externalFlashModified=$false;externalImageDataPreserved=$info.externalImageDataPreserved;readbackSkipped=$true;wholeReadbackVerified=$false;nativeInitializationVerified=$true;automaticReplay=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $operation 'WRITE-NOT-VERIFIED.json') -Encoding utf8
        Write-InkTagTerminalMessage "Main-Flash programming completed; full readback skipped: NOT VERIFIED. Both new Recovery pages and Settings checked. External Flash untouched; external images retained. Bluetooth: $($info.expectedAppBluetoothName); Recovery: $($info.expectedRecoveryBluetoothName)." -Level WARNING
        Write-InkTagTerminalMessage 'Release BOOT if held, then disconnect and reconnect power to start InkTag. The native calendar is requested on first startup. A complete power cycle is required to clear old reset causes.'
        exit 0
    }
    Write-InkTagTerminalStage 'COMPLETE FLASH / INDEPENDENT WHOLE FLASH READBACK'
    $readback=Join-Path $operation 'readback';New-Item -ItemType Directory -Path $readback | Out-Null
    $actual=Join-Path $operation 'readback.bin'
    Read-ControlledRange 'readback' 0x00400000 0x004FFFFF $actual -WholeRead
    if((Get-FileHash $actual -Algorithm SHA256).Hash -ne $info.expectedSha256){throw 'Whole readback differs; preserve failure, no automatic retry/rollback'}
    Clear-ControlledHistoryMailbox $info.retainedHistoryMailbox
    @{status='write-verified';scope='mcu/main-flash';expectedAppBluetoothName=$info.expectedAppBluetoothName;planSHA256=$info.planSha256;resultSHA256=$info.expectedSha256;externalFlashAccessed=$false;externalFlashModified=$false;externalImageDataPreserved=$info.externalImageDataPreserved;releaseAccepted=$false;bluetoothObserved=$false;displayObserved=$false;automaticReplay=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $operation 'WRITE-VERIFIED.json') -Encoding utf8
    Write-InkTagTerminalMessage "Main-Flash write and independent readback verified. External Flash untouched; external images retained. Bluetooth: $($info.expectedAppBluetoothName); Recovery: $($info.expectedRecoveryBluetoothName)." -Level SUCCESS
    Write-InkTagTerminalMessage 'Release BOOT if held, then disconnect and reconnect power to start InkTag. The native calendar is requested on first startup. A complete power cycle is required to clear old reset causes.'
} catch {
    Write-InkTagTerminalMessage $_.Exception.Message -Level ERROR
    if($writeStarted){Write-InkTagTerminalMessage 'A write was attempted; outcome may be partial. Keep evidence. No automatic replay, resume or rollback.' -Level WARNING}
    elseif($ramCaptureStarted){Write-InkTagTerminalMessage 'If the target attached, it may remain halted with overwritten RAM. Power-cycle manually; do not resume the old App. No Flash write was started.' -Level WARNING}
    elseif($maintenanceStarted){Write-InkTagTerminalMessage 'Full maintenance may have stopped the watchdog and halted the target. Fully power-cycle manually; do not resume the old App. No RAM download or Flash write was started.' -Level WARNING}
    elseif($targetAccessStarted){Write-InkTagTerminalMessage 'No RAM capture loader or Flash write was started. J-Link connection may have halted or reset the target; power-cycle manually before running the App.' -Level WARNING}
    else{Write-InkTagTerminalMessage 'Stopped before target access. No target connection, RAM download or Flash write was started.' -Level WARNING}
    exit 1
}
finally {
    try {
        try {Remove-ControlledRegistration} catch {Write-InkTagTerminalMessage ('Temporary loader registration cleanup failed: '+$_.Exception.Message) -Level WARNING}
        if($operation){Write-InkTagTerminalMessage "Operation evidence: $operation"}
        Complete-InkTagTerminalProgress
    } finally {
        try {if($locked){$lock.ReleaseMutex()};if($lock){$lock.Dispose()}}
        finally {Restore-InkTagTerminalColour $terminalState}
    }
}
