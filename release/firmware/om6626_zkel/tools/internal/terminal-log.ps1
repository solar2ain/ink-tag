# Presentation only; no device, retry or result-verification decisions.
# TLSR-style stages, live progress and scoped English terminal messages.
# Keep OM vendor-log parsing separate from the TLSR SWS parser.
$script:InkTagTerminalProgressActive = $false
function Get-InkTagTerminalColour {
    $state = @{ UICulture = [Threading.Thread]::CurrentThread.CurrentUICulture }
    try { $state.Foreground = [Console]::ForegroundColor; $state.Background = [Console]::BackgroundColor } catch {}
    try { $state.HostForeground = $Host.UI.RawUI.ForegroundColor; $state.HostBackground = $Host.UI.RawUI.BackgroundColor } catch {}
    return $state
}
function Set-InkTagTerminalEnglish {
    [Threading.Thread]::CurrentThread.CurrentUICulture = [Globalization.CultureInfo]::GetCultureInfo('en-US')
}
function Restore-InkTagTerminalColour {
    param($State)
    if ($null -eq $State) { return }
    try {
        if ($State.ContainsKey('HostBackground')) { $Host.UI.RawUI.BackgroundColor = $State.HostBackground }
        if ($State.ContainsKey('HostForeground')) { $Host.UI.RawUI.ForegroundColor = $State.HostForeground }
    } catch {}
    try {
        if ($State.ContainsKey('Background')) { [Console]::BackgroundColor = $State.Background }
        if ($State.ContainsKey('Foreground')) { [Console]::ForegroundColor = $State.Foreground }
    } catch {}
    if ($State.ContainsKey('UICulture')) { [Threading.Thread]::CurrentThread.CurrentUICulture = $State.UICulture }
}
function Test-InkTagInteractiveTerminal {
    try { return ($Host.Name -eq 'ConsoleHost' -and ![Console]::IsOutputRedirected -and ![Console]::IsErrorRedirected) } catch { return $false }
}
function Write-InkTagTerminalMessage {
    param([string]$Message, [ValidateSet('INFO','DEBUG','WARNING','ERROR','SUCCESS')][string]$Level = 'INFO')
    if ($Level -eq 'DEBUG' -and $VerbosePreference -ne 'Continue') { return }
    Complete-InkTagTerminalProgress
    $prefix = '[{0}] [{1}]' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:sszzz'), $Level
    $colour = switch ($Level) { 'ERROR' { 'Red' }; 'SUCCESS' { 'Green' }; 'WARNING' { 'Yellow' }; 'DEBUG' { 'DarkGray' }; default { 'Cyan' } }
    $state = Get-InkTagTerminalColour
    try {
        # Raw vendor text may contain control characters. Preserve those only in
        # the original evidence log, never execute escape/CR sequences on screen.
        $plain = [regex]::Replace($Message, '[\x00-\x1F\x7F]', ' ')
        if ((Test-Path Env:NO_COLOR) -or !(Test-InkTagInteractiveTerminal)) { Write-Host "$prefix $plain" }
        else { Write-Host "$prefix $plain" -ForegroundColor $colour }
    } finally { Restore-InkTagTerminalColour $state }
}
function Complete-InkTagTerminalProgress {
    if ($script:InkTagTerminalProgressActive) {
        # Clear state first so cancellation cannot leave a dangling live line.
        $script:InkTagTerminalProgressActive = $false
        Write-Host ''
    }
}
function Format-InkTagTerminalProgressLine {
    param([string]$Message, [string]$Prefix, [int]$Columns)
    $full = "$Prefix $Message"
    if ($full.Length -le $Columns) { return $full }
    if ($Message.Length -le $Columns) { return $Message }
    return $null # Never truncate confirmed quantities to keep decoration.
}
function Get-InkTagTerminalColumns {
    try { return [Math]::Max(0, [Console]::WindowWidth - 1) } catch { return 0 }
}
function Write-InkTagTerminalProgress {
    param([string]$Message, [string[]]$CompactMessages = @())
    $plain = [regex]::Replace($Message, '[\x00-\x1F\x7F]', ' ')
    $columns = if (Test-InkTagInteractiveTerminal) { Get-InkTagTerminalColumns } else { 0 }
    if ($columns -lt 40) { Write-InkTagTerminalMessage $plain; return }
    $prefix = '[{0}] [INFO]' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:sszzz')
    $line = Format-InkTagTerminalProgressLine $plain $prefix $columns
    foreach ($compact in $CompactMessages) {
        if ($null -ne $line) { break }
        $safe = [regex]::Replace($compact, '[\x00-\x1F\x7F]', ' ')
        $line = Format-InkTagTerminalProgressLine $safe $prefix $columns
    }
    if ($null -eq $line) { Write-InkTagTerminalMessage $plain; return }
    $state = Get-InkTagTerminalColour
    try {
        $script:InkTagTerminalProgressActive = $true
        if (Test-Path Env:NO_COLOR) { Write-Host ("`r" + $line.PadRight($columns)) -NoNewline }
        else { Write-Host ("`r" + $line.PadRight($columns)) -NoNewline -ForegroundColor Cyan }
    } finally { Restore-InkTagTerminalColour $state }
}
function Write-InkTagTerminalStage {
    param([string]$Name)
    Write-InkTagTerminalMessage ("-------------------- {0} --------------------" -f $Name)
}
function Show-OmProbeMessage {
    param([string]$Message)
    if ($Message -match '^Probe OK:') {
        Write-InkTagTerminalMessage 'Detected OM6626.' -Level SUCCESS
    } elseif ($Message -match '^ERROR:') {
        Write-InkTagTerminalMessage $Message -Level ERROR
    } elseif ($Message -match '^\s*Log:|^Waiting for OM6626|^\s*Target not ready;') {
        Write-InkTagTerminalMessage $Message
    } else { Write-InkTagTerminalMessage $Message -Level DEBUG }
}
function Update-OmBackupReadTrace {
    param([string]$Line, [hashtable]$State)
    # Presentation only, qualified to the installed read-only loader's SHA.
    # SEGGER_OPEN_Read=0x200030C5; R0=Flash, R1=length, R2=RAM buffer.
    # Count only a successful loader result AND successful host buffer read.
    if (!$State.ContainsKey('trace-enabled') -or !$State['trace-enabled'] -or
        $Line -notmatch '^(T[0-9A-F]+)\s+\d+:\d+\.\d+\s+(.+)$') { return $false }
    $thread = $Matches[1]; $event = $Matches[2]
    if (!$State.ContainsKey('trace-threads')) { $State['trace-threads'] = @{}; $State['read-blocks'] = @{} }
    if (!$State['trace-threads'].ContainsKey($thread)) { $State['trace-threads'][$thread] = @{regs=@{}} }
    $trace = $State['trace-threads'][$thread]
    if ($event -match '^JLINK_WriteReg\((R[012]|R15 \(PC\)), (0x[0-9A-F]+)\)') {
        if ($trace.ContainsKey('loaded')) { $trace.Remove('loaded'); $trace.Remove('request') }
        $trace['write'] = $Matches[1]; $trace['value'] = [Convert]::ToUInt32($Matches[2],16)
        $trace.Remove('await'); return $false
    }
    if ($event -match '^JLINK_GoIntDis\(\)') {
        $r = $trace['regs']; $trace.Remove('request'); $trace.Remove('loaded')
        if ($r.ContainsKey('R0') -and $r.ContainsKey('R1') -and $r.ContainsKey('R2') -and
            $r.ContainsKey('R15 (PC)') -and $r['R15 (PC)'] -eq 0x200030c5 -and
            $r['R1'] -eq 4096 -and $r['R2'] -eq 0x20004640 -and
            $r['R0'] -ge 0x400000 -and $r['R0'] -lt 0x500000 -and $r['R0'] % 4096 -eq 0) {
            $trace['request'] = $r['R0']
        }
        return $false
    }
    if ($event -match '^JLINK_ReadReg\(R0\)' -and $trace.ContainsKey('request')) {
        $trace['await'] = 'loader'; return $false
    }
    if ($event -match '^JLINK_ReadMemEx\(0x20004640, 0x1000 Bytes, Flags = 0x00000000\)' -and
        $trace.ContainsKey('loaded') -and $trace['loaded']) {
        $trace['await'] = 'buffer'; return $false
    }
    if ($event -match '^- [\d.]+ms returns (.+)$') {
        $result = $Matches[1]
        if ($trace.ContainsKey('write')) {
            if ($result -eq '0') { $trace['regs'][$trace['write']] = $trace['value'] }
            else { $trace['regs'].Clear(); $trace.Remove('request'); $trace.Remove('loaded') }
            $trace.Remove('write'); return $false
        }
        if ($trace.ContainsKey('await')) {
            $kind = $trace['await']; $trace.Remove('await')
            if ($kind -eq 'loader') { $trace['loaded'] = ($result -eq '0x00001000'); return $false }
            if ($kind -eq 'buffer') {
                $address = $trace['request']; $trace.Remove('request'); $trace.Remove('loaded'); $trace['regs'].Clear()
                if ($result -eq '4096 (0x1000)' -and !$State['read-blocks'].ContainsKey($address)) {
                    $State['read-blocks'][$address] = $true
                    $State['read-bytes'] = $State['read-blocks'].Count * 4096
                    return $true
                }
            }
        }
    } elseif ($event -match '^JLINK_') {
        # An intervening API call cannot supply a missing return receipt.
        $trace.Remove('await')
        if ($trace.ContainsKey('loaded')) { $trace.Remove('loaded'); $trace.Remove('request') }
    }
    return $false
}
function Write-OmBackupProgress {
    param([string]$Stage, [hashtable]$State, [switch]$Complete)
    # An absent target is not a read in progress. Do not redraw a Read N/2
    # bar for every failed connection attempt; the caller logs readiness only.
    if (!$Complete -and !$State.ContainsKey('read-bytes') -and
        (!$State.ContainsKey('activity') -or $State['activity'] -notmatch '^(Reading|Saving)')) { return }
    $label = if ($Stage -match '^pass([12])$') { 'Read {0}/2' -f $Matches[1] } else { $Stage }
    $seconds = if ($State.ContainsKey('elapsed-seconds')) { [Math]::Max(0, [int]$State['elapsed-seconds']) } else { 0 }
    if ($Complete) {
        # Same 20-cell #/. bar as TLSR. Only the vendor's complete-read
        # receipt can fill it; archive success still needs size/hash checks.
        $bar = '#' * 20
        $detail = 'read 100% (1048576/1048576 bytes, 1024 KiB)'
        $summary = '100% 1024/1024 KiB'
    } elseif ($State.ContainsKey('read-bytes') -and !$State.ContainsKey('saving')) {
        $bytes = [int]$State['read-bytes']; $percent = [int][Math]::Floor($bytes * 100.0 / 1048576)
        $filled = [int][Math]::Floor($percent / 5)
        $bar = ('#' * $filled) + ('.' * (20 - $filled))
        $kib = [int]($bytes / 1024)
        $detail = "read $percent% ($bytes/1048576 bytes, $kib/1024 KiB)"
        $summary = "$percent% $kib/1024 KiB"
    } else {
        # Unknown/changed loader or missing receipts: animate, never estimate
        # from elapsed time, checked addresses or output file size.
        $position = $seconds % 34
        if ($position -gt 17) { $position = 34 - $position }
        $bar = ('.' * $position) + '###' + ('.' * (17 - $position))
        $activity = if ($State.ContainsKey('activity')) { $State['activity'] } else { 'Connecting / preparing read' }
        $detail = "$activity | 1024 KiB total; percentage unavailable"
        # Do not hide the actual phase behind "pending" on ordinary 60/80
        # column terminals. '?' explicitly means no measured byte progress.
        $phase = if ($activity -like 'Saving*') { 'Saving' } elseif ($activity -like 'Reading*') { 'Reading' } else { 'Connecting' }
        $summary = "$phase ? 1024 KiB total"
    }
    $message = "[OM6626] $label [$bar] $detail | elapsed ${seconds}s"
    $compact = @(
        "$label [$bar] $summary | elapsed ${seconds}s",
        "$label [$bar] $summary"
    )
    Write-InkTagTerminalProgress $message -CompactMessages $compact
}
function Confirm-OmBackupConnection {
    param([hashtable]$State, [string]$Stage)
    if ($State.ContainsKey('target-connected')) { return }
    $State['target-connected'] = $true
    Write-InkTagTerminalMessage 'SWD target connected.' -Level SUCCESS
    $label = if ($Stage -match '^pass([12])$') { 'BACKUP READ {0}/2' -f $Matches[1] } else { $Stage }
    Write-InkTagTerminalStage $label
}
function Show-OmBackupLog {
    param([string[]]$Paths, [hashtable]$State, [string]$Stage, [switch]$Final)
    foreach ($path in $Paths) {
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        # A growing file may yield several Raw string chunks. Concatenate
        # without separators before doing string offsets; do not let member
        # enumeration turn LastIndexOf/cursor arithmetic into Object[].
        $raw = [string]::Join('', @(Get-Content -LiteralPath $path -Raw -ErrorAction SilentlyContinue))
        if (!$raw.Length) { continue }
        $offset = if ($State.ContainsKey($path)) { [int]$State[$path] } else { 0 }
        if ($offset -gt $raw.Length) { $offset = 0 }
        # Only complete records: a buffered partial line is not lost or parsed
        # twice when the next vendor write appends the remaining bytes.
        $tail = $raw.Substring($offset)
        $last = $tail.LastIndexOf("`n")
        if ($Final) { $last = $tail.Length - 1 }
        if ($last -lt 0) { continue }
        $State[$path] = $offset + $last + 1
        foreach ($line in ($tail.Substring(0, $last + 1) -split "`r?`n")) {
            if (!$line) { continue }
            Write-InkTagTerminalMessage $line -Level DEBUG
            if ((Update-OmBackupReadTrace $line $State) -and !$State.ContainsKey('read-complete')) {
                Confirm-OmBackupConnection $State $Stage
                Write-OmBackupProgress $Stage $State
            }
            if ($line -match '^\s*-\s*Connected successfully') {
                Confirm-OmBackupConnection $State $Stage
                $State['activity'] = 'Reading Flash'
            } elseif ($line -match '^\s*-\s*Blank checking (0x[0-9A-F]+ - 0x[0-9A-F]+)') {
                # A vendor range check is activity, not completed read bytes.
                $State['activity'] = 'Reading Flash; checking range ' + $Matches[1]
            } elseif ($line -match '^Saving data file') {
                $State['activity'] = 'Saving readout'
                $State['saving'] = $true
            } elseif ($line -match 'Data file saved successfully \(1048576 bytes, 1 range\)' -and !$State.ContainsKey('save-complete')) {
                $State['save-complete'] = $true
                Write-InkTagTerminalMessage "$Stage : saved 1048576 bytes; validating."
            }
            # J-Flash 7.88j exposes a confirmed byte total, not a reliable
            # incremental byte callback. Do not convert wall time/file size to %.
            if ($line -match 'Target memory read successfully\. \(1048576 bytes, 1 range\)' -and !$State.ContainsKey('read-complete')) {
                Confirm-OmBackupConnection $State $Stage
                $State['read-complete'] = $true
                Write-OmBackupProgress $Stage $State -Complete
                Write-InkTagTerminalMessage 'Saving and validating, please wait.'
            }
        }
    }
}

function Show-OmBackupHeartbeat {
    param([hashtable]$State, [string]$Stage, [double]$ElapsedSeconds)
    if (!$State.ContainsKey('read-bytes') -and
        (!$State.ContainsKey('activity') -or $State['activity'] -notmatch '^(Reading|Saving)')) { return }
    if ($State.ContainsKey('save-complete') -or
        ($State.ContainsKey('read-complete') -and !$State.ContainsKey('saving'))) { return }
    $seconds = [int][Math]::Floor($ElapsedSeconds)
    $State['elapsed-seconds'] = $seconds
    $interval = if (Test-InkTagInteractiveTerminal) { 1 } else { 10 }
    $tick = [int][Math]::Floor($seconds / $interval)
    $activity = if ($State.ContainsKey('activity')) { $State['activity'] } else { 'Connecting / preparing read' }
    $key = "$tick/$activity"
    if ($State.ContainsKey('heartbeat') -and $State['heartbeat'] -eq $key) { return }
    $State['heartbeat'] = $key
    Write-OmBackupProgress $Stage $State
}

function Write-OmFullProgress {
    param([hashtable]$State)
    $stage=if($State.ContainsKey('stage')){$State['stage']}else{'Complete Flash'}
    $short=if($stage -match 'Complete Flash (\d/3)'){'Flash '+$Matches[1]}else{'Flash'}
    $seconds=if($State.ContainsKey('elapsed-seconds')){[int]$State['elapsed-seconds']}else{0}
    if($State.ContainsKey('percent')){
        $percent=[int]$State['percent'];$action=$State['action']
        $filled=[int][Math]::Floor($percent/5)
        $bar=('#'*$filled)+('.'*(20-$filled))
        $detail="$action $percent%"
    }else{
        $position=$seconds%34;if($position -gt 17){$position=34-$position}
        $bar=('.'*$position)+'###'+('.'*(17-$position))
        $detail='Preparing; percentage unavailable'
    }
    Write-InkTagTerminalProgress "$stage : [$bar] $detail | elapsed ${seconds}s" -CompactMessages @(
        "$short [$bar] $detail | ${seconds}s",
        "$short [$bar] $(if($State.ContainsKey('percent')){$detail}else{'Preparing ?'}) | ${seconds}s"
    )
}

function Show-OmFullHeartbeat {
    param([hashtable]$State,[double]$ElapsedSeconds)
    if($State.ContainsKey('failed') -and $State['failed']){return}
    $seconds=[int][Math]::Floor($ElapsedSeconds)
    $State['elapsed-seconds']=$seconds
    $interval=if(Test-InkTagInteractiveTerminal){1}else{10}
    $tick=[int][Math]::Floor($seconds/$interval)
    if($State.ContainsKey('heartbeat') -and $State['heartbeat'] -eq $tick){return}
    $State['heartbeat']=$tick
    Write-OmFullProgress $State
}

function Show-OmFullLine {
    param([string]$Line, [hashtable]$State)
    $line = $Line.Trim()
    if (!$line) { return }
    if ($line -match '^OM_REVIEW_STAGE ([1-4])/4: (.+)$') {
        Write-InkTagTerminalStage ("COMPLETE FLASH / LOCAL VALIDATION {0}/4 - {1}" -f $Matches[1], $Matches[2])
    } elseif ($line -match '^Clearing stale Swap state') {
        $State['stage'] = 'Complete Flash 1/3 - FactoryState'
        Write-InkTagTerminalStage $State['stage']
    } elseif ($line -match '^Programming Boot and Slot A') {
        $State['stage'] = 'Complete Flash 2/3 - Boot and App'
        Write-InkTagTerminalStage $State['stage']
    } elseif ($line -match '^Programming the final FactoryDeviceInfo') {
        $State['stage'] = 'Complete Flash 3/3 - FactoryDeviceInfo'
        Write-InkTagTerminalStage $State['stage']
    } elseif ($line -match '(?i)^(Comparing|Erasing|Programming|Verifying) flash\s*\[(\d{1,3})%\]') {
        $action = $Matches[1]; $percent = [int]$Matches[2]
        if ($percent -gt 100) { return }
        $stage = if ($State.ContainsKey('stage')) { $State['stage'] } else { 'Complete Flash' }
        $key = "$stage/$action/$percent"
        if (!$State.ContainsKey('progress') -or $State['progress'] -ne $key) {
            $State['progress'] = $key
            $State['percent']=$percent;$State['action']=$action
            Write-OmFullProgress $State
        }
    } elseif ($line -match '(?i)(^\**\s*(Error:|ERROR:)|^Verify failed|^Error while programming|^Programming did not start|INKTAG_ATTEMPT_TIMEOUT)') {
        $State['failed'] = $true
        Write-InkTagTerminalMessage $line -Level ERROR
    } elseif ($line -match '^Flash OK\.') {
        $State['verified'] = $true # Presentation receipt only, still require native success.
        Write-InkTagTerminalMessage 'Programmer reported Flash OK; independent whole-Flash readback is still required.'
    } elseif ($line -match '^Target detected:') {
        Write-InkTagTerminalMessage 'Detected OM6626.'
        Write-InkTagTerminalMessage $line -Level DEBUG
    } elseif ($line -match '^\s*Log:|^Waiting for OM6626|^\s*Target not ready;') {
        Write-InkTagTerminalMessage $line
    } else {
        Write-InkTagTerminalMessage $line -Level DEBUG
    }
}

function Show-OmFullOutput {
    param($InputObject, [hashtable]$State, [switch]$Final)
    if (!$State.ContainsKey('buffer')) { $State['buffer'] = '' }
    if ($null -ne $InputObject) {
        if ($InputObject -is [Management.Automation.WarningRecord]) {
            Write-InkTagTerminalMessage ([string]$InputObject) -Level WARNING
            return
        }
        $text = [string]$InputObject
        # Write-Host's InformationRecord retains the original NoNewLine flag.
        # Native log tails can split a record and use backspaces for percentages.
        $noNewline = $InputObject -is [Management.Automation.InformationRecord] -and
            $InputObject.MessageData.PSObject.Properties['NoNewLine'] -and $InputObject.MessageData.NoNewLine
        if (!$noNewline) { $text += "`n" }
        foreach ($char in $text.ToCharArray()) {
            if ($char -eq "`b") {
                if ($State['buffer'].Length) { $State['buffer'] = $State['buffer'].Substring(0,$State['buffer'].Length-1) }
            } elseif ($char -eq "`r" -or $char -eq "`n") {
                Show-OmFullLine $State['buffer'] $State
                $State['buffer'] = ''
            } else {
                $State['buffer'] += $char
                # A single pipe read can contain many backspace updates. Render
                # each complete vendor percentage, not only the last one (100%).
                if($char -eq ']' -and $State['buffer'] -match '(?i)^(Comparing|Erasing|Programming|Verifying) flash\s*\[\d{1,3}%\]$'){
                    Show-OmFullLine $State['buffer'] $State
                }
            }
        }
        # Render an in-flight percentage without prematurely committing a split line.
        if ($State['buffer'] -match '(?i)^(Comparing|Erasing|Programming|Verifying) flash\s*\[\d{1,3}%\]') {
            Show-OmFullLine $State['buffer'] $State
        }
    }
    if ($Final -and $State['buffer']) {
        Show-OmFullLine $State['buffer'] $State
        $State['buffer'] = ''
    }
}

function Complete-OmFullPresentation {
    param([int]$ExitCode, [hashtable]$State)
    Complete-InkTagTerminalProgress
    if ($ExitCode -eq 0 -and $State.ContainsKey('verified') -and !$State.ContainsKey('failed')) {
        Write-InkTagTerminalMessage 'Congratulations! All three complete flashing/verification stages passed. Cold power cycle and BLE/panel/function checks are still required.' -Level SUCCESS
        return $true
    }
    Write-InkTagTerminalMessage 'Complete flashing did not pass verification; inspect the preserved raw log. No success claimed.' -Level ERROR
    return $false
}
