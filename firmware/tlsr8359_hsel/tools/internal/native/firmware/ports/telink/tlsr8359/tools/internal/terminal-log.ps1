# Presentation only. No device operation, retry, timeout or file-write logic.
$script:InkTagTerminalProgressActive = $false

function Get-TlsrExpectedBluetoothNames {
    param([string]$MacAddress)
    if ($MacAddress -notmatch '^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$') {
        throw 'Cannot derive TLSR Bluetooth names: invalid immutable MAC address.'
    }
    # Match inktag_ble.c build_default_name(): s_mac[1], then s_mac[0].
    # The host MAC is printed in the opposite byte order to Flash storage.
    $parts = $MacAddress.ToUpperInvariant().Split(':')
    $appName = 'IKT_TL_' + $parts[4] + $parts[5]
    return @{ App = $appName; Recovery = $appName + '_OTA' }
}

function Complete-InkTagTerminalProgress {
    if ($script:InkTagTerminalProgressActive) {
        Write-Host ''
        $script:InkTagTerminalProgressActive = $false
    }
}

function Get-InkTagTerminalColour {
    $state = @{ UICulture = [Threading.Thread]::CurrentThread.CurrentUICulture }
    try {
        $state.Foreground = [Console]::ForegroundColor
        $state.Background = [Console]::BackgroundColor
        try {
            $state.HostForeground = $Host.UI.RawUI.ForegroundColor
            $state.HostBackground = $Host.UI.RawUI.BackgroundColor
        } catch {}
    }
    catch {} # Non-console hosts may not expose colour state.
    return $state # UI language must still be restored in a headless host.
}

function Set-InkTagTerminalEnglish {
    # Per-thread presentation only; do not change OS language, time or number
    # formatting. The operation's finally restores the caller's UI culture.
    [Threading.Thread]::CurrentThread.CurrentUICulture = [Globalization.CultureInfo]::GetCultureInfo('en-US')
}

function Restore-InkTagTerminalColour {
    param($State)
    if ($null -eq $State) { return }
    # Unsupported RawUI setters must not skip the separate Console cleanup.
    try {
        # RawUI and Console can have separate cached state in PowerShell hosts.
        # Restore both; never guess White/Black or discard the user's theme.
        if ($State.ContainsKey('HostBackground')) { $Host.UI.RawUI.BackgroundColor = $State.HostBackground }
        if ($State.ContainsKey('HostForeground')) { $Host.UI.RawUI.ForegroundColor = $State.HostForeground }
    } catch {}
    try {
        [Console]::BackgroundColor = $State.Background
        [Console]::ForegroundColor = $State.Foreground
    } catch {} # Colour cleanup must not replace the operation's original error.
    if ($State.ContainsKey('UICulture')) {
        [Threading.Thread]::CurrentThread.CurrentUICulture = $State.UICulture
    }
}

function Write-InkTagTerminalMessage {
    param([string]$Message, [ValidateSet('INFO','DEBUG','WARNING','ERROR','SUCCESS')][string]$Level = 'INFO')
    Complete-InkTagTerminalProgress
    $prefix = '[{0}] [{1}]' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:sszzz'), $Level
    $colour = switch ($Level) { 'ERROR' { 'Red' }; 'SUCCESS' { 'Green' }; 'WARNING' { 'Yellow' }; 'DEBUG' { 'DarkGray' }; default { 'Cyan' } }
    $messageColourState = Get-InkTagTerminalColour
    try {
        if (Test-Path Env:NO_COLOR) { Write-Host ("$prefix $Message") }
        else { Write-Host ("$prefix $Message") -ForegroundColor $colour }
    } finally { Restore-InkTagTerminalColour $messageColourState }
}

function Format-InkTagTerminalProgressLine {
    param([string]$Message, [string]$Prefix, [int]$Columns)
    $full = "$Prefix $Message"
    if ($full.Length -le $Columns) { return $full }
    # Actual progress wins over decoration in a narrow console. Never chop
    # off confirmed quantities, percentages or elapsed time to keep a date.
    if ($Message.Length -le $Columns) { return $Message }
    return $null # Caller emits the complete message as newline output.
}

function Write-InkTagTerminalProgress {
    param([string]$Message)
    $interactive = $false
    try { $interactive = $Host.Name -eq 'ConsoleHost' -and ![Console]::IsOutputRedirected } catch {}
    $columns = 0
    if ($interactive) { try { $columns = [Console]::WindowWidth - 1 } catch {} }
    if (!$interactive -or $columns -lt 40) {
        # Pipes/CI/transcripts get complete newline-delimited progress, too.
        Write-InkTagTerminalMessage $Message
        return
    }
    $colourState = Get-InkTagTerminalColour
    try {
        $prefix = '[{0}] [INFO]' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:sszzz')
        $line = Format-InkTagTerminalProgressLine -Message $Message -Prefix $prefix -Columns $columns
        if ($null -eq $line) { Write-InkTagTerminalMessage $Message; return }
        $line = "`r" + $line.PadRight($columns)
        if (Test-Path Env:NO_COLOR) { Write-Host $line -NoNewline }
        else { Write-Host $line -NoNewline -ForegroundColor Cyan }
        $script:InkTagTerminalProgressActive = $true
    } finally { Restore-InkTagTerminalColour $colourState }
}

function Write-InkTagTerminalStage {
    param([string]$Name)
    Write-InkTagTerminalMessage ("-------------------- {0} --------------------" -f $Name)
}

function Show-InkTagTerminalLog {
    [CmdletBinding()]
    param([Parameter(ValueFromPipeline = $true)][object]$InputObject,
          [string]$CompletionMessage = 'Transport step finished; checking operation result.')
    begin { $lastPercent = ""; $lastAction = ""; $readbackSkipped = $false }
    process {
        $line = [string]$InputObject
        if ($InputObject -isnot [Management.Automation.ErrorRecord] -and
            $line -match '^Continue with the detected Flash model for this run\? \[y/N\]$') {
            # Interactive input is not verbose/debug output. Always show the
            # complete question before the native child waits for stdin.
            Write-InkTagTerminalMessage $line -Level WARNING
        } elseif ($InputObject -isnot [Management.Automation.ErrorRecord] -and
            $line -match '^WARNING: Config Flash JEDEC |^Only the lower 512-KiB Full window |^Flash model mismatch confirmed for this run:') {
            Write-InkTagTerminalMessage $line -Level WARNING
        } elseif ($InputObject -isnot [Management.Automation.ErrorRecord] -and
            $line.Trim() -eq 'A validated SWS response ends the activation window immediately; the full duration is only a timeout.') {
            Write-InkTagTerminalMessage $line -Level INFO
        } elseif ($InputObject -is [Management.Automation.ErrorRecord] -or
            $line -match '(?i)\b(error|exception|failed|failure|refused|denied|mismatch|invalid|timeout|timed out|cannot|not found)\b|Flash JEDEC differs|no array read') {
            Write-InkTagTerminalMessage $line -Level ERROR
        } elseif ($line -match '^\s*WARNING:|Unstable 256-byte SWS frame|^FULL WRITE COMPLETE / READBACK SKIPPED:|^FULL READBACK SKIPPED:') {
            if ($line -match '^FULL WRITE COMPLETE / READBACK SKIPPED:|^FULL READBACK SKIPPED:') { $readbackSkipped = $true }
            Write-InkTagTerminalMessage $line -Level WARNING
        } elseif ($line -match '(?i)\b(reading|read/verify|verifying|programming|writing|erasing)\s+(\d+(?:\.\d+)?)%') {
            if ($Matches[2] -ne $lastPercent -or $Matches[1] -ne $lastAction -or $VerbosePreference -eq 'Continue') {
                if ($lastAction -and $lastAction -ne $Matches[1]) { Complete-InkTagTerminalProgress }
                $lastAction = $Matches[1]
                $lastPercent = $Matches[2]
                # Compact the live line only; Tee-Object keeps original output.
                $display = $line.Trim() -replace '^(?i)Read/verify', 'Verify' -replace '^(?i)Reading', 'Read' -replace '^(?i)Programming', 'Write'
                $display = $display -replace '[()]', '' -replace '; elapsed ', '  '
                Write-InkTagTerminalProgress $display
                # Keep the final row visible before any next phase or the
                # vendor's cursor-clearing output. 100% is not overall success.
                if ([double]::Parse($lastPercent,[Globalization.CultureInfo]::InvariantCulture) -ge 100) { Complete-InkTagTerminalProgress }
            }
        } elseif ($line -match '^\s*-{3,} DETECTION RETRY \d+ -{3,}\s*$') {
            Write-InkTagTerminalMessage $line
        } elseif ($line.Trim() -eq 'Done!') {
            # The vendor tool reports this after individual reads as well as
            # writes. Only the owning operation may announce overall success.
            if ($readbackSkipped) { Write-InkTagTerminalMessage 'Write transport finished; readback was explicitly skipped. NOT VERIFIED.' -Level WARNING }
            else { Write-InkTagTerminalMessage $CompletionMessage }
        } elseif ($line -match '^\s*(RESET RTS:|RESET SWS:|Activation detected|CONTACT WAIT:|FLASH BUS:|PC =|Observed Flash JEDEC:|Observed Flash status:|Flash JEDEC samples:|Flash UID:|Flash status samples:|BACKUP STAGE:|BACKUP READ|PROGRAMMING:|FULL WRITE:|FULL WRITE COMPLETE:|FULL READBACK:|FULL READBACK MATCHED:|FULL-READBACK-MATCHED:|Readback complete|Flash and .* verification OK)') {
            Write-InkTagTerminalMessage $line -Level $(if ($line -match 'verification OK|FULL READBACK MATCHED:|FULL-READBACK-MATCHED:') { 'SUCCESS' } else { 'INFO' })
        } elseif ($VerbosePreference -eq 'Continue') {
            Write-InkTagTerminalMessage $line -Level DEBUG
        }
    }
    end { Complete-InkTagTerminalProgress }
}
