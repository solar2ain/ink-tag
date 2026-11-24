# Host presentation/process supervision only. No BLE, target command generation
# or interpretation of a vendor percentage as verified Flash success.
$script:NordicProgressActive = $false
$script:NordicLastProgress = ''
$script:NordicProgressLabel = 'Flash'
$script:NordicProgressTotalBytes = 0
function Get-NordicTerminalState {
    $state = @{ UICulture = [Threading.Thread]::CurrentThread.CurrentUICulture }
    try {
        $state.Foreground = [Console]::ForegroundColor
        $state.Background = [Console]::BackgroundColor
        try {
            $state.HostForeground = $Host.UI.RawUI.ForegroundColor
            $state.HostBackground = $Host.UI.RawUI.BackgroundColor
        } catch {}
    } catch {}
    return $state
}
function Restore-NordicTerminalState {
    param($State)
    if ($null -eq $State) { return }
    # RawUI and Console may cache colours separately. A failed UI setter must
    # neither skip the other cleanup nor mask the operation's original error.
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
function Start-InkTagHardwareTerminal {
    $state = Get-NordicTerminalState
    [Threading.Thread]::CurrentThread.CurrentUICulture = [Globalization.CultureInfo]::GetCultureInfo('en-US')
    return $state
}
function Complete-NordicProgress {
    $active=$script:NordicProgressActive
    $script:NordicProgressActive = $false
    $script:NordicLastProgress = ''
    if ($active) { Write-Host '' }
}
function Restore-InkTagHardwareTerminal {
    param($State)
    try { Complete-NordicProgress }
    finally { Restore-NordicTerminalState $State }
}
function Write-InkTagHardwareMessage {
    param([string]$Message, [ValidateSet('INFO','DEBUG','WARNING','ERROR','SUCCESS')][string]$Level='INFO')
    if ($Level -eq 'DEBUG' -and $VerbosePreference -ne 'Continue') { return }
    Complete-NordicProgress
    $plain = ($Message -replace '\x1b\[[0-?]*[ -/]*[@-~]', '') -replace '[\x00-\x1f\x7f]', ' '
    $text = '[{0}] [{1}] {2}' -f [DateTimeOffset]::Now.ToString('yyyy-MM-dd HH:mm:sszzz'), $Level, $plain
    $colour = switch ($Level) { 'ERROR' {'Red'} 'WARNING' {'Yellow'} 'SUCCESS' {'Green'} 'DEBUG' {'DarkGray'} default {'Cyan'} }
    $state = Get-NordicTerminalState
    try {
        if ((Test-Path Env:NO_COLOR) -or !(Test-NordicInteractiveTerminal)) { Write-Host $text }
        else { Write-Host $text -ForegroundColor $colour }
    } finally { Restore-NordicTerminalState $state }
}
function Write-InkTagHardwareStage {
    param([string]$Name)
    Write-InkTagHardwareMessage ("-------------------- {0} --------------------" -f $Name)
}
function Format-NordicProgressLine {
    param([string]$Message, [string]$Prefix, [int]$Columns)
    $full = "$Prefix $Message"
    if ($full.Length -le $Columns) { return $full }
    if ($Message.Length -le $Columns) { return $Message }
    return $null # Emit a complete newline rather than truncating actual facts.
}
function Write-NordicProgress {
    param([string]$Message, [string[]]$CompactMessages=@())
    # Vendor lines/paths can contain CR, ANSI sequences or control characters.
    $safe = ($Message -replace '\x1b\[[0-?]*[ -/]*[@-~]', '') -replace '[\x00-\x1f\x7f]', ' '
    if ($safe -eq $script:NordicLastProgress) { return }
    $interactive = Test-NordicInteractiveTerminal
    $width = 0
    if ($interactive) { try { $width = [Console]::WindowWidth - 1 } catch {} }
    if (!$interactive -or $width -lt 40) {
        Write-InkTagHardwareMessage $safe; $script:NordicLastProgress = $safe; return
    }
    $prefix = '[{0}] [INFO]' -f [DateTimeOffset]::Now.ToString('yyyy-MM-dd HH:mm:sszzz')
    $line = Format-NordicProgressLine -Message $safe -Prefix $prefix -Columns $width
    foreach ($compact in $CompactMessages) {
        if ($null -ne $line) { break }
        $plain = ($compact -replace '\x1b\[[0-?]*[ -/]*[@-~]', '') -replace '[\x00-\x1f\x7f]', ' '
        $line = Format-NordicProgressLine -Message $plain -Prefix $prefix -Columns $width
    }
    if ($null -eq $line) { Write-InkTagHardwareMessage $safe; $script:NordicLastProgress = $safe; return }
    $state = Get-NordicTerminalState
    try {
        $script:NordicProgressActive = $true
        if (Test-Path Env:NO_COLOR) { Write-Host ("`r" + $line.PadRight($width)) -NoNewline }
        else { Write-Host ("`r" + $line.PadRight($width)) -NoNewline -ForegroundColor Cyan }
        $script:NordicLastProgress = $safe
    } finally { Restore-NordicTerminalState $state }
}
function Test-NordicInteractiveTerminal {
    try { return $Host.Name -eq 'ConsoleHost' -and ![Console]::IsOutputRedirected -and ![Console]::IsErrorRedirected } catch { return $false }
}
function Set-NordicProgressLabel {
    param([string]$Label, [long]$TotalBytes=0)
    $script:NordicProgressLabel=$Label
    $script:NordicProgressTotalBytes=$TotalBytes
}
function Write-NordicOperationProgress {
    param([hashtable]$State)
    $seconds=[int]$State.seconds
    if ($State.ContainsKey('percent')) {
        $percent=$State.percent
        $filled=[int][Math]::Floor($percent/5)
        $bar=('#'*$filled)+('.'*(20-$filled))
        $detail="$($State.action) $percent%"
        if ($State.ContainsKey('readBytes')) { $detail+=' {0}/{1} KiB' -f ($State.readBytes/1024).ToString('0.##',[Globalization.CultureInfo]::InvariantCulture),($State.totalBytes/1024).ToString('0.##',[Globalization.CultureInfo]::InvariantCulture) }
    } else {
        $position=$seconds%34; if ($position -gt 17) { $position=34-$position }
        $bar=('.'*$position)+'###'+('.'*(17-$position))
        $detail="$($State.action); percentage unavailable"
    }
    Write-NordicProgress "$($State.label) : [$bar] $detail | elapsed ${seconds}s" -CompactMessages @(
        "$($State.label) [$bar] $detail | ${seconds}s",
        "$($State.label) $detail | ${seconds}s")
}
function Show-NordicToolLine {
    param([string]$Line, [bool]$ShowProgress, [hashtable]$State)
    # A backend percentage is not readback verification. Never synthesize a
    # percentage from elapsed time, file size, or one successful command.
    if (!$State) { $State=@{label=$script:NordicProgressLabel;seconds=0;action='Preparing';totalBytes=$script:NordicProgressTotalBytes} }
    $percentages = [regex]::Matches($Line, '(?i)(?:read|writ|program|eras|download|verif|compar)[^\r\n]*?(?<![-\d.])\b([0-9]{1,3}(?:\.[0-9]+)?)\s*%')
    if ($ShowProgress -and $State.label -eq 'Write' -and $Line -match '^J-Link>savebin "[^"]+" 0x00000000 0x80000$') {
        Complete-NordicProgress
        Write-InkTagHardwareStage 'COMPLETE FLASH / INDEPENDENT WHOLE FLASH READBACK'
        $State.label='Verify';$State.action='Reading';$State.totalBytes=532480
        $State.Remove('percent');$State.Remove('readBytes')
    }
    if ($Line -match '(?i)^Cannot connect to target\.$') {
        Write-InkTagHardwareMessage ($Line+' Awaiting this attempt result; no new attempt started.') -Level WARNING
    } elseif ($Line -match '(?i)(could not|failed|error while|traceback|no ack)') {
        Write-InkTagHardwareMessage $Line -Level ERROR
    } elseif ($ShowProgress -and $percentages.Count) {
        $percent=[double]::Parse($percentages[$percentages.Count-1].Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture)
        if ($percent -gt 100) { return }
        $State.percent=$percent
        $State.action=if ($Line -match '(?i)eras') {'Erasing'} elseif ($Line -match '(?i)verif|compar') {'Comparing'} elseif ($Line -match '(?i)read') {'Reading'} else {'Programming'}
        Write-NordicOperationProgress $State
    } elseif ($ShowProgress -and $State.totalBytes -gt 0 -and $Line -match '^Reading ([0-9]+) bytes from addr 0x[0-9A-Fa-f]+ into file\.\.\.O\.K\.$') {
        $count=[long]$Matches[1]
        $previous=if ($State.ContainsKey('readBytes')) {[long]$State.readBytes} else {0}
        if ($count -gt $State.totalBytes-$previous) { return }
        $State.readBytes=$previous+$count
        $State.percent=[Math]::Floor($State.readBytes*100.0/$State.totalBytes)
        $State.action='Read'
        Write-NordicOperationProgress $State
    } else { Write-InkTagHardwareMessage $Line -Level DEBUG }
}
function Receive-NordicToolChunk {
    param([string]$Chunk, [hashtable]$State, $Lines, [bool]$ShowProgress, [switch]$Final)
    # Separate the screen's backspace/redraw frames from the classifier's
    # original logical lines. The evidence sink receives untouched chunks.
    foreach ($character in $Chunk.ToCharArray()) {
        if ($character -eq "`r" -or $character -eq "`n") {
            if (!($character -eq "`n" -and $State.afterCr)) {
                $Lines.Add([string]$State.rawLine)
                Show-NordicToolLine $State.display $ShowProgress $State.progress
            }
            $State.rawLine='';$State.display='';$State.afterCr=($character -eq "`r")
        } else {
            $State.afterCr=$false;$State.rawLine+=$character
            if ($character -eq "`b") {
                if ($State.display.Length) { $State.display=$State.display.Substring(0,$State.display.Length-1) }
            } else {
                $State.display+=$character
                if ($ShowProgress -and $character -eq '%') { Show-NordicToolLine $State.display $true $State.progress }
            }
        }
    }
    if ($Final -and $State.rawLine.Length) {
        $Lines.Add([string]$State.rawLine)
        Show-NordicToolLine $State.display $ShowProgress $State.progress
        $State.rawLine='';$State.display=''
    }
}
function ConvertTo-NordicNativeArgument {
    param([AllowEmptyString()][string]$Value)
    # Windows CommandLineToArgvW quoting, including paths with spaces and
    # trailing backslashes. PS5.1 has no ProcessStartInfo.ArgumentList.
    '"' + ([regex]::Replace(([regex]::Replace($Value, '(\\*)"', '$1$1\"')), '(\\+)$', '$1$1')) + '"'
}
function Invoke-NordicLoggedProcess {
    param([string]$Exe, [object[]]$Arguments, [string]$LogPath='',
          [int]$OperationTimeoutSeconds=120, [switch]$ShowProgress)
    if ($OperationTimeoutSeconds -lt 1) { throw 'OperationTimeoutSeconds must be >= 1' }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Exe
    $start.Arguments = (($Arguments | ForEach-Object { ConvertTo-NordicNativeArgument ([string]$_) }) -join ' ')
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    # Redirected Python/pyOCD stdout otherwise block-buffers its diagnostics.
    # Set only the child's environment; do not alter the invoking terminal.
    $start.EnvironmentVariables['PYTHONUNBUFFERED'] = '1'
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $lines = [Collections.Generic.List[string]]::new()
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $writer = $null
    $timedOut = $false
    $exitCode = 1
    $launched = $false
    try {
        if ($LogPath) {
            $writer = [IO.StreamWriter]::new($LogPath, $true, [Text.UTF8Encoding]::new($false))
            $writer.AutoFlush = $true
        }
        Write-InkTagHardwareMessage ('Tool: ' + $Exe + ' ' + $start.Arguments) -Level DEBUG
        if (!$process.Start()) { throw 'Unable to start hardware tool' }
        $launched = $true
        $progress=@{label=$script:NordicProgressLabel;action='Preparing';seconds=0;totalBytes=$script:NordicProgressTotalBytes}
        $streams=@()
        foreach ($reader in @($process.StandardOutput,$process.StandardError)) {
            $buffer=[char[]]::new(4096)
            $streams+=@{reader=$reader;buffer=$buffer;task=$reader.ReadAsync($buffer,0,$buffer.Length);
                state=@{rawLine='';display='';afterCr=$false;progress=$progress}}
        }
        $lastWait = -10
        while (@($streams | Where-Object {$null -ne $_.task}).Count -or !$process.HasExited) {
            $progress.seconds=[int][Math]::Floor($timer.Elapsed.TotalSeconds)
            foreach ($stream in $streams) {
                $task=$stream.task
                if ($null -ne $task -and $task.IsCompleted) {
                    $count=$task.GetAwaiter().GetResult()
                    if ($count -gt 0) {
                        $chunk=[string]::new($stream.buffer,0,$count)
                        if ($writer) { $writer.Write($chunk) }
                        Receive-NordicToolChunk $chunk $stream.state $lines ([bool]$ShowProgress)
                        $stream.task=$stream.reader.ReadAsync($stream.buffer,0,$stream.buffer.Length)
                    } else {
                        Receive-NordicToolChunk '' $stream.state $lines ([bool]$ShowProgress) -Final
                        $stream.task=$null
                    }
                }
            }
            if (!$timedOut -and $timer.Elapsed.TotalSeconds -ge $OperationTimeoutSeconds) {
                $timedOut = $true
                $message = "INKTAG_OPERATION_TIMEOUT: ${OperationTimeoutSeconds}s; result unknown, no automatic replay."
                $lines.Add($message)
                if ($writer) { $writer.WriteLine($message) }
                Write-InkTagHardwareMessage $message -Level ERROR
                if (!$process.HasExited) {
                    if ($process.PSObject.Methods['Kill'].OverloadDefinitions -match 'bool') { $process.Kill($true) }
                    else { $process.Kill() }
                }
            }
            $heartbeatInterval=if (Test-NordicInteractiveTerminal) {1} else {10}
            if ($ShowProgress -and [int]$timer.Elapsed.TotalSeconds -ge ($lastWait + $heartbeatInterval)) {
                $lastWait = [int]$timer.Elapsed.TotalSeconds
                Write-NordicOperationProgress $progress
            }
            if ($timedOut -and $timer.Elapsed.TotalSeconds -gt ($OperationTimeoutSeconds + 3)) { break }
            Start-Sleep -Milliseconds 20
        }
        if (!$process.WaitForExit(1000)) { throw 'Hardware tool did not exit after timeout termination; no replay.' }
        $exitCode = if ($timedOut) { 124 } else { $process.ExitCode }
    } finally {
      try { Complete-NordicProgress }
      finally {
        if ($launched -and !$process.HasExited) {
            if ($process.PSObject.Methods['Kill'].OverloadDefinitions -match 'bool') { $process.Kill($true) }
            else { $process.Kill() }
            $null = $process.WaitForExit(1000)
        }
        if ($writer) { $writer.Dispose() }
        $process.Dispose()
      }
    }
    [pscustomobject]@{ ExitCode=$exitCode; Lines=$lines.ToArray(); Text=($lines -join "`n"); TimedOut=$timedOut }
}
