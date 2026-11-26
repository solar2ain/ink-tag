# Maintained OM native process/attach/readback functions; Python paths excluded.
[CmdletBinding()]
param([string]$JLinkSerial='', [string]$JLinkExe='',
 [ValidateScript({$_ -ge 1000 -and $_ -le 1000000 -and $_%1000 -eq 0})][int]$FrequencyHz=1000000,
 [switch]$WaitForTarget,
 [ValidateRange(3,120)][int]$AttemptTimeoutSeconds=12,[ValidateRange(30,1800)][int]$OperationTimeoutSeconds=360)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
. (Join-Path $PSScriptRoot '../terminal-log.ps1')
. (Join-Path $root 'tools/jlink-tools.ps1')
$terminalState=Get-InkTagTerminalColour
$operation='';$writeStarted=$false;$ramCaptureStarted=$false;$maintenanceStarted=$false;$lock=$null;$locked=$false;$presentation=@{}
$registrationDirectory='';$registrationFile='';$registrationSHA256='';$registrationValidated=$false
$targetAccessStarted=$false
$connectKHz=[Math]::Min(100,[int]($FrequencyHz/1000))

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

function Test-ControlledWaitCanceled {
    if([Console]::IsInputRedirected){return $false}
    while([Console]::KeyAvailable){
        if([Console]::ReadKey($true).Key -eq [ConsoleKey]::Escape){return $true}
    }
    return $false
}

function Resolve-ControlledProbe([string]$RequestedSerial) {
    $method='explicit'
    if($RequestedSerial){
        if($RequestedSerial -notmatch '^[0-9]{4,16}$'){throw 'Invalid -JLinkSerial; expected an exact numeric serial'}
        $selected=$RequestedSerial
    } else {
        # USB enumeration only: never guess from the startup S/N banner, select
        # the first of multiple probes, discover remote probes or attach a CPU.
        $attempt=0
        do {
            if($WaitForTarget -and (Test-ControlledWaitCanceled)){return}
            $attempt++
            $stem=if($WaitForTarget){'probe-list-{0:D3}' -f $attempt}else{'probe-list'}
            $text=Invoke-ControlledNative $stem @('ShowEmuList USB') -ProbeList -TimeoutSeconds 8
            $rows=[regex]::Matches($text,'(?im)^\s*J-Link\[\d+\]:\s*Connection:\s*USB,\s*Serial number:\s*([0-9]{4,16})(?=\s|,|$)')
            if(!$rows.Count -and $WaitForTarget){
                Write-InkTagTerminalMessage (Get-OmFlashText 'No USB J-Link yet. Waiting (Esc cancels).' '尚未检测到 USB J-Link，继续等待（Esc 取消）。')
                Start-Sleep -Seconds 1
            }
        } while(!$rows.Count -and $WaitForTarget)
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

function Read-ControlledCaptureTrace([string]$Path,[hashtable]$State) {
    if(!(Test-Path -LiteralPath $Path -PathType Leaf)){return}
    $stream=[IO.File]::Open($Path,'Open','Read','ReadWrite')
    $reader=[IO.StreamReader]::new($stream)
    try {
        if($stream.Length -lt $State.Offset){throw 'Native SWD capture trace changed'}
        $null=$stream.Seek($State.Offset,[IO.SeekOrigin]::Begin)
        $chunk=$State.Tail+$reader.ReadToEnd()
        $State.Offset=$stream.Position
        $State.Tail=$chunk.Substring([Math]::Max(0,$chunk.Length-64))
        foreach($marker in [regex]::Matches($chunk,'OM_CAPTURE_[A-Z_]+:')){
            $null=$State.Markers.Add($marker.Value)
        }
    } finally {$reader.Dispose()}
}

function Start-ControlledCaptureProcess([string]$Line,[string]$Stdout,[string]$Stderr) {
    return Start-Process -FilePath $JLinkExe -WorkingDirectory (Split-Path $JLinkExe -Parent) -ArgumentList $Line -WindowStyle Hidden -PassThru -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr
}

function Invoke-ControlledCapture([string]$Hook,[switch]$CompileOnly,[string[]]$MaintenanceCommands) {
    $stem=if($CompileOnly){'capture-compile'}elseif($MaintenanceCommands){'full-maintenance'}else{'detect-continuous'}
    $script=Join-Path $operation ($stem+'.jlink')
    # Older Commander versions can ignore hook compiler failures. Verify the
    # same script on a USB-only command file before enabling a target connect.
    $commands=@("usb $JLinkSerial",'exec SetRestartOnClose = 0')
    if($MaintenanceCommands){$commands+=$MaintenanceCommands}
    elseif(!$CompileOnly){$commands+=(Get-ControlledConnection)+@('mem32 0x40000000 2','regs')}
    [IO.File]::WriteAllLines($script,($commands+@('qc')),[Text.Encoding]::ASCII)
    $stdout=Join-Path $operation ($stem+'.stdout.log');$stderr=Join-Path $operation ($stem+'.stderr.log')
    $trace=Join-Path $operation ($stem+'.trace.log')
    $arguments=@('-NoGui','1','-SelectEmuBySN',$JLinkSerial,'-Device','CORTEX-M4','-If','SWD','-Speed',([string]$connectKHz),'-AutoConnect','0','-ExitOnError','1','-CommanderScript',$script,'-JLinkScriptFile',$Hook,'-Log',$trace)
    $line=($arguments|ForEach-Object {if($_ -match '["\r\n]'){throw 'Unsafe native argument'};'"'+$_+'"'}) -join ' '
    $state=@{Offset=0L;Tail='';Markers=[Collections.Generic.HashSet[string]]::new()}
    $child=$null;$exitCode=$null;$ready=$null;$bootState=$null;$status='failed'
    $watch=[Diagnostics.Stopwatch]::StartNew()
    try {
        if(!$MaintenanceCommands -and (Test-ControlledWaitCanceled)){$status='canceled';return}
        $child=Start-ControlledCaptureProcess $line $stdout $stderr
        $null=$child.Handle
        while(!$child.HasExited){
            if(!$MaintenanceCommands -and (Test-ControlledWaitCanceled)){$status='canceled';return}
            if(!$CompileOnly){
                Read-ControlledCaptureTrace $trace $state
                if(!$MaintenanceCommands -and $null -eq $ready -and $state.Markers.Contains('OM_CAPTURE_READY:')){
                    $ready=$watch.Elapsed.TotalSeconds
                    Write-InkTagTerminalMessage (Get-OmFlashText 'Continuous OM6626 SWD capture ready. Power on the board normally (Esc cancels).' 'OM6626 持续 SWD 探测已就绪。请正常给主板上电（Esc 取消）。')
                }
                if($state.Markers.Contains('OM_CAPTURE_WRONG_CPUID:') -or $state.Markers.Contains('OM_CAPTURE_WRONG_SYS:')){
                    $status='wrong-target';throw 'SWD target is not OM6626; no maintenance started'
                }
                if($state.Markers.Contains('OM_CAPTURE_TIMEOUT:')){
                    $status='timeout';throw 'Continuous SWD window expired; no maintenance started'
                }
                if($state.Markers.Contains('OM_CAPTURE_HANDOFF_LOST:')){
                    $status='handoff-lost';throw 'Captured OM6626 halt lost before maintenance; no Flash write'
                }
            }
            $limit=if($MaintenanceCommands){$OperationTimeoutSeconds}elseif($null -eq $ready){$AttemptTimeoutSeconds}else{$ready+$OperationTimeoutSeconds+5}
            if($watch.Elapsed.TotalSeconds -ge $limit){$status='timeout';throw "Native SWD timeout at $stem; no maintenance started"}
            Start-Sleep -Milliseconds 100;$child.Refresh()
        }
        $child.WaitForExit();$exitCode=$child.ExitCode
        Read-ControlledCaptureTrace $trace $state
        $text=(Read-ControlledText $stdout)+"`n"+(Read-ControlledText $stderr)
        $serial=[regex]::Match($text,'(?im)^\s*S/N:\s*([0-9]+)\s*$')
        if($exitCode -ne 0 -or !$serial.Success -or $serial.Groups[1].Value -ne $JLinkSerial -or
            $text -match '(?im)\*+ Error:|^\s*ERROR\s*:|error while|failed prepare script|unknown command|cannot connect|could not connect|failed to|could not (?:be halted|read)|syntax error|license.*(?:expired|invalid|missing)|access denied|timeout|timed out|reset fallback|reset via|implicit reset|Connect fallback|Resetting via'){
            throw "J-Link capture failed at $stem; preserve raw logs. No Flash write started"
        }
        if(!$CompileOnly){
            if(!$state.Markers.Contains('OM_CAPTURE_READY:') -or !$state.Markers.Contains('OM_CAPTURE_HALTED:') -or
                @($state.Markers|Where-Object {$_ -match '^OM_CAPTURE_(?:WRONG_|TIMEOUT:|HANDOFF_LOST:)'}).Count){throw 'J-Link did not qualify continuous SWD capture or handoff; no Flash write'}
            $cpu=[regex]::Match($text,'(?im)^E000ED00\s*=\s*([0-9A-F]{8})')
            if(!$cpu.Success -or ([Convert]::ToUInt32($cpu.Groups[1].Value,16) -band 0xff00fff0L) -ne 0x4100c240L){throw 'Cortex-M4 readback after capture failed'}
            $bootState=Get-ControlledBootState $text
        }
        $status='qualified'
        return $text
    } finally {
        if($child){
            if(!$child.HasExited){$child.Kill();if(!$child.WaitForExit(3000)){throw 'SWD capture process did not stop; no maintenance started'}}
            $child.Dispose()
        }
        if($status -eq 'canceled' -and !$CompileOnly){
            Write-InkTagTerminalMessage (Get-OmFlashText 'SWD wait canceled. If the board stopped, power it off and on to resume.' '已取消 SWD 等待。如主板已暂停，请断电重上电以恢复运行。')
        }
        @{status=$status;exitCode=$exitCode;probe=$JLinkSerial;bootState=$bootState;markers=@($state.Markers);elapsedSeconds=$watch.Elapsed.TotalSeconds} |
            ConvertTo-Json | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $operation ($stem+'.native.json'))
    }
}

function Get-ControlledDetection {
    if($writeStarted){throw 'Detection is forbidden after write start'}
    if($WaitForTarget){
        $hook=Join-Path $operation 'full-capture.JLinkScript'
        $body=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'full-capture.JLinkScript'))
        $body=$body.Replace('__WINDOW_MS__',[string]($OperationTimeoutSeconds*1000))
        [IO.File]::WriteAllText($hook,$body.Replace('__REQUIRE_HELD__','0'),[Text.Encoding]::ASCII)
        [IO.File]::WriteAllText((Join-Path $operation 'full-handoff.JLinkScript'),$body.Replace('__REQUIRE_HELD__','1'),[Text.Encoding]::ASCII)
        $compiled=Invoke-ControlledCapture $hook -CompileOnly
        if(!$compiled){return}
        return Invoke-ControlledCapture $hook
    }
    Write-InkTagTerminalMessage ("Detecting OM6626 SWD (contact timeout ${AttemptTimeoutSeconds}s). Use the InkTag boot SWD window. No scripted reset; J-Link native connect fallback may apply.")
    return Invoke-ControlledNative 'detect-001' ((Get-ControlledConnection)+@('mem32 0x40000000 2','regs')) -TimeoutSeconds $OperationTimeoutSeconds -Detection
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

function Enter-ControlledMaintenance([string]$Captured='') {
    # Full only, AFTER the exact SYS identity/halt gate. The pinned vendor
    # loader's Init also stops WDT with this key (PrgCode 0x2EC4..0x2ED4).
    # Our earlier RAM reader bypassed Init, leaving a running App/ROM watchdog
    # able to reset the core during the download or between reader processes.
    # Do not change BOOT_SEL, reset flags, Flash status/protection or firmware.
    $commands=@(Get-ControlledConnection)
    if($Captured){$commands=@($commands|Where-Object {$_ -cne 'halt'})}
    $commands+=@('mem32 0x40000000 2','regs','mem32 0x400E0094 1',
        'w4 0x400E00E8 0x00006666','sleep 20','mem32 0x400E00E8 1',
        'mem32 0x400E0094 1','sleep 20','mem32 0x400E0094 1','mem32 0xE000EDF0 1')
    $text=if($Captured){Invoke-ControlledCapture (Join-Path $operation 'full-handoff.JLinkScript') -MaintenanceCommands $commands}else{Invoke-ControlledNative 'full-maintenance' $commands}
    $null=Get-ControlledBootState $text
    if($Captured){
        $registers='(?im)^(?:E000ED00\s*=|40000000\s*=|PC\s*=)[^\r\n]+'
        $before=@([regex]::Matches($Captured,$registers)|ForEach-Object {$_.Value.Trim()})
        $after=@([regex]::Matches($text,$registers)|ForEach-Object {$_.Value.Trim()})
        if($before.Count -ne 3 -or ($before -join '|') -cne ($after -join '|')){
            throw 'Captured target registers changed during maintenance handoff; no RAM download or Flash write'
        }
    }
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
    # Mask pending App interrupts while halted, before the RAM reader's first instruction.
    $commands+=@("loadbin `"$code`", 0x20000000, noreset","loadbin `"$trampoline`", 0x20012000, noreset",
        "loadbin `"$request`", 0x20013000, noreset",'wreg "R13 (SP)" 0x20013D00','wreg "R15 (PC)" 0x20012001',
        'wreg "PRIMASK" 0x1','g','sleep 100','halt',("savebin `"{0}`", 0x20013000, 0x40" -f (Join-Path $Directory ('identity-'+$Suffix+'.bin'))),
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
