# Host console/argument helpers. TLSR presentation reference:
# D652AC738BDA00359AD20F0549EF048A98062EF06E61F6487066507DBC9805DC.
# No device access, configuration writer or total dispatcher; the backend owns operations.
$script:InkTagTerminalProgressActive = $false
$script:InkTagTerminalLastProgress = ''
function Get-InkTagTerminalColour {
    $state = @{ UICulture = [Threading.Thread]::CurrentThread.CurrentUICulture }
    try { $state.Foreground = [Console]::ForegroundColor; $state.Background = [Console]::BackgroundColor } catch {}
    try { $state.HostForeground = $Host.UI.RawUI.ForegroundColor; $state.HostBackground = $Host.UI.RawUI.BackgroundColor } catch {}
    return $state
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

function Set-InkTagTerminalEnglish {
    [Threading.Thread]::CurrentThread.CurrentUICulture = [Globalization.CultureInfo]::GetCultureInfo('en-US')
}
function Test-InkTagInteractiveTerminal {
    try { return $Host.Name -eq 'ConsoleHost' -and ![Console]::IsOutputRedirected -and ![Console]::IsErrorRedirected } catch { return $false }
}
function Complete-InkTagTerminalProgress {
    $active = $script:InkTagTerminalProgressActive
    $script:InkTagTerminalProgressActive = $false
    $script:InkTagTerminalLastProgress = ''
    if ($active) { Write-Host '' }
}
function Format-InkTagTerminalProgressLine {
    param([string]$Message, [string]$Prefix, [int]$Columns)
    if (("$Prefix $Message").Length -le $Columns) { return "$Prefix $Message" }
    if ($Message.Length -le $Columns) { return $Message }
    return $null
}

function Write-InkTagTerminalMessage {
    param([string]$Message, [ValidateSet('INFO','DEBUG','WARNING','ERROR','SUCCESS')][string]$Level = 'INFO')
    if ($Level -eq 'DEBUG' -and $VerbosePreference -ne 'Continue') { return }
    Complete-InkTagTerminalProgress
    # Native output is data; neither redirected nor interactive output executes
    # its escape sequences. Plain lines also work in a narrow console.
    $clean = $Message -replace '\x1B\[[0-?]*[ -/]*[@-~]', '' -replace '[\x00-\x1F\x7F]', ' '
    $prefix = '[{0}] [{1}]' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:sszzz'), $Level
    $state = Get-InkTagTerminalColour
    $plain = $true
    try { $plain = !(Test-InkTagInteractiveTerminal) -or (Test-Path Env:NO_COLOR) } catch {}
    $colour = switch ($Level) { 'DEBUG' {'DarkGray'} 'WARNING' {'Yellow'} 'ERROR' {'Red'} 'SUCCESS' {'Green'} default {'Cyan'} }
    try {
        if ($plain) { Write-Host "$prefix $clean" }
        else { Write-Host "$prefix $clean" -ForegroundColor $colour }
    } finally { Restore-InkTagTerminalColour $state }
}

function Write-InkTagTerminalStage {
    param([string]$Name)
    Write-InkTagTerminalMessage ("-------------------- {0} --------------------" -f $Name)
}
function Write-InkTagTerminalProgress {
    param([hashtable]$State)
    $seconds = [int]$State.seconds
    $label = [string]$State.label
    if ($State.ContainsKey('percent')) {
        $percent = [double]$State.percent
        if ($percent -lt 0 -or $percent -gt 100 -or [double]::IsNaN($percent)) { return }
        $filled = [int][Math]::Floor($percent / 5)
        $bar = ('#' * $filled) + ('.' * (20 - $filled))
        $detail = '{0} {1:0.0}%' -f $State.action, $percent
        if ($State.ContainsKey('done') -and $State.ContainsKey('total')) {
            $detail += ' {0}/{1} bytes ({2:0.##}/{3:0.##} KiB)' -f $State.done,$State.total,($State.done/1024.0),($State.total/1024.0)
        }
    } else {
        $position = $seconds % 34
        if ($position -gt 17) { $position = 34 - $position }
        $bar = ('.' * $position) + '###' + ('.' * (17-$position))
        $detail = "$($State.action); percentage unavailable"
    }
    $plain = "$label [$bar] $detail | elapsed ${seconds}s"
    if (!(Test-InkTagInteractiveTerminal)) {
        # Keep actual progress immediately; throttle activity-only waiting lines.
        if (!$State.ContainsKey('percent') -and $State.ContainsKey('lastWait') -and $seconds -lt $State.lastWait+10) { return }
        $State.lastWait = $seconds
        Write-InkTagTerminalMessage $plain
        return
    }
    $columns = 0
    try { $columns = [Console]::WindowWidth-1 } catch {}
    $prefix = '[{0}] [INFO]' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:sszzz')
    $line = Format-InkTagTerminalProgressLine $plain $prefix $columns
    if ($null -eq $line) { $line = Format-InkTagTerminalProgressLine "$label $detail | ${seconds}s" $prefix $columns }
    if ($columns -lt 40 -or $null -eq $line) { Write-InkTagTerminalMessage $plain; return }
    $theme = Get-InkTagTerminalColour
    try {
        $script:InkTagTerminalProgressActive = $true
        if (Test-Path Env:NO_COLOR) { Write-Host ("`r"+$line.PadRight($columns)) -NoNewline }
        else { Write-Host ("`r"+$line.PadRight($columns)) -NoNewline -ForegroundColor Cyan }
    } finally { Restore-InkTagTerminalColour $theme }
}

function Receive-InkTagNativeChunk {
    param([string]$Chunk,[hashtable]$State,[string]$Stream,[string]$Label,$ProgressState,[switch]$Final)
    foreach ($character in $Chunk.ToCharArray()) {
        if ($character -eq "`r" -or $character -eq "`n") {
            if (!($character -eq "`n" -and $State.afterCr) -and $State.raw.Length) {
                Write-InkTagNativeMessage -Line $State.raw -Display $State.display -Stream $Stream -Label $Label -ProgressState $ProgressState
            }
            $State.raw='';$State.display='';$State.afterCr=($character -eq "`r")
        } else {
            $State.afterCr=$false;$State.raw+=$character
            if ($character -eq "`b") {
                if ($State.display.Length) { $State.display=$State.display.Substring(0,$State.display.Length-1) }
            } else {
                $State.display+=$character
                if ($character -eq '%') { Write-InkTagNativeMessage -Line $State.raw -Display $State.display -Stream $Stream -Label $Label -ProgressState $ProgressState -ProgressOnly }
            }
        }
    }
    if ($Final -and $State.raw.Length) {
        Write-InkTagNativeMessage -Line $State.raw -Display $State.display -Stream $Stream -Label $Label -ProgressState $ProgressState
        $State.raw='';$State.display=''
    }
}

function Write-InkTagNativeMessage {
    param([string]$Line, [string]$Stream = 'stdout', [string]$Label = '', $ProgressState = $null, [string]$Display='', [switch]$ProgressOnly)
    if (!$Display) { $Display = $Line }
    $seconds = 0
    if ($null -ne $ProgressState -and $ProgressState.ContainsKey('OperationStarted')) {
        $seconds = [int][Math]::Floor($ProgressState.OperationStarted.Elapsed.TotalSeconds)
    }
    if ($Line -ceq 'J-Link Commander will now exit on Error' -or $Line -match '^J-Link>(?:EoE|ExitOnError) 1$') {
        if (!$ProgressOnly) { Write-InkTagTerminalMessage "$Label $Line" -Level DEBUG }
        return
    }
    if ($ProgressOnly -and $Stream -eq 'stderr') { return }
    if ($Stream -eq 'stderr' -or $Line -match '(?i)\b(error|failed|failure|cannot|could not|fault|abort)\b') {
        Write-InkTagTerminalMessage "$Label $Line" -Level ERROR
    } elseif ($Line -match '(?i)\bwarning\b') {
        Write-InkTagTerminalMessage "$Label $Line" -Level WARNING
    } elseif ($Display -match '(?i)^Reading (\d+) bytes from addr .*O\.K\.') {
        $count = [long]$Matches[1]
        $state=@{label=$Label;action='Read transfer';percent=100;done=$count;total=$count;seconds=$seconds}
        if ($null -ne $ProgressState) { $ProgressState.LastProgress=$state }
        Write-InkTagTerminalProgress $state
    } elseif ($Display -match '(?i)\b(reading|writing|programming|verifying)\s+(\d+)\s*/\s*(\d+)\s+bytes') {
        $done = [long]$Matches[2]; $total = [long]$Matches[3]
        if ($total -le 0 -or $done -gt $total) { Write-InkTagTerminalMessage "$Label $Line" -Level WARNING; return }
        $elapsed = ''
        if ($null -ne $ProgressState) {
            if ($null -eq $ProgressState.Started) { $ProgressState.Started = [Diagnostics.Stopwatch]::GetTimestamp() }
            $elapsed = '; elapsed {0:0.00}s from first native data progress' -f (([Diagnostics.Stopwatch]::GetTimestamp() - $ProgressState.Started) / [double][Diagnostics.Stopwatch]::Frequency)
        }
        $state=@{label=$Label;action=$Matches[1];done=$done;total=$total;percent=(100.0*$done/$total);seconds=$seconds}
        if ($null -ne $ProgressState) { $ProgressState.LastProgress=$state }
        Write-InkTagTerminalProgress $state
    } elseif ($Display -match '(?i)\b(read|writ|program|eras|verif|compar)[^\r\n]*?(?<![-\d.])\b([0-9]{1,3}(?:\.[0-9]+)?)\s*%') {
        $percent = [double]::Parse($Matches[2],[Globalization.CultureInfo]::InvariantCulture)
        $state=@{label=$Label;action=$Matches[1];percent=$percent;seconds=$seconds}
        if ($null -ne $ProgressState) { $ProgressState.LastProgress=$state }
        Write-InkTagTerminalProgress $state
    } elseif (!$ProgressOnly -and $Line -match '(?i)^Reading \d+ bytes|^Downloading file|^Erasing|^Verifying') {
        # Only real vendor progress. Never derive completion from file size,
        # a timer or a "Downloading file" start banner.
        $elapsed = ''
        if ($null -ne $ProgressState -and $Line -match '\d+(?:\.\d+)?%') {
            if ($null -eq $ProgressState.Started) { $ProgressState.Started = [Diagnostics.Stopwatch]::GetTimestamp() }
            $elapsed = '; elapsed {0:0.00}s from first native data progress' -f (([Diagnostics.Stopwatch]::GetTimestamp() - $ProgressState.Started) / [double][Diagnostics.Stopwatch]::Frequency)
        }
        Write-InkTagTerminalMessage "$Label $Line$elapsed"
    } elseif (!$ProgressOnly) { Write-InkTagTerminalMessage "$Label $Line" -Level DEBUG }
}

function Show-InkTagFullEvent {
    param([string]$Line,[hashtable]$ProgressState)
    if (!$Line.StartsWith('INKTAG_FULL_EVENT ')) { Write-InkTagTerminalMessage $Line -Level DEBUG; return }
    $event = $Line.Substring(18) | ConvertFrom-Json
    switch ($event.kind) {
        'stage' { Write-InkTagTerminalStage ([string]$event.name) }
        'message' { Write-InkTagTerminalMessage ([string]$event.text) -Level $event.level }
        'native' { Write-InkTagTerminalMessage ([string]$event.text) -Level DEBUG }
        'progress' {
            foreach ($key in @('label','action','seconds','percent','done','total')) {
                if ($event.PSObject.Properties.Name -contains $key) { $ProgressState[$key]=$event.$key }
                else { $ProgressState.Remove($key) }
            }
            Write-InkTagTerminalProgress $ProgressState
        }
        default { throw 'Unknown Full presentation event' }
    }
}

function Assert-InkTagHostArguments {
    param([string]$Snr, [int]$FrequencyHz, [int]$IntervalSeconds, [int]$TimeoutSeconds)
    if ($Snr -and $Snr -notmatch '^\d+$') { throw 'J-Link serial must contain digits only.' }
    if ($FrequencyHz -lt 1000 -or $FrequencyHz % 1000 -ne 0 -or $IntervalSeconds -lt 1 -or $TimeoutSeconds -lt 0) { throw 'Invalid SWD frequency (whole kHz), retry interval or timeout.' }
}

function Test-InkTagTargetAbsent {
    param($Result)
    # Retry only a known no-contact response; nonzero/unknown tool failures and
    # access/protection/identity errors remain terminal.
    $other = [regex]::Replace($Result.Text,'(?im)^Cannot connect to target\.?\r?$(?:\n)?','')
    return (Test-InkTagNativeReadResult ([pscustomobject]@{ExitCode=0;Text=$other})) -and
        $Result.ExitCode -in @(0,1) -and $Result.Text -match '(?i)Cannot connect to target\.?\s*$' -and
        $Result.Text -notmatch '(?i)denied|protected|permission|invalid|mismatch|fatal|unsupported|unknown|voltage|J-Link.*not found|Cortex-M4 identified|CPUID register|Reading \d+ bytes' -and
        $Result.Text -notmatch '(?im)^\s*(?:E000ED00|1000[0-9A-F]{4})\s*='
}

function Test-InkTagNativeReadResult {
    param($Result)
    # Strict nRF11 read path: do not inherit nRF32's separately qualified
    # initial-connect recovery exception. Later success cannot hide a fault.
    $diagnostic = '(?im)^(?!J-Link Commander will now exit on Error\r?$).*?(Cannot|Could not|Unable to|\bFailed\b|\bfailure\b|\bError\b|\bdenied\b|\bprotected\b|Traceback|No ACK|communication failure|unknown (?:command|register)|invalid register|timeout|timed out)'
    return $Result.ExitCode -eq 0 -and $Result.Text -notmatch $diagnostic
}

function Invoke-InkTagOfflineCheck {
    param([string]$PythonExe, [string[]]$Arguments, [string]$LogPath)
    $oldPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = @(& $PythonExe @Arguments 2>&1)
        $nativeOK = $?
        $exitValue = Get-Variable LASTEXITCODE -ErrorAction SilentlyContinue
        $code = if ($null -ne $exitValue) { [int]$exitValue.Value } else { -1 }
    } catch {
        $nativeOK = $false
        $code = -1
        $lines = @([string]$_)
    } finally { $ErrorActionPreference = $oldPreference }
    $strings = @($lines | ForEach-Object { [string]$_ })
    [IO.File]::WriteAllLines($LogPath, [string[]]$strings, [Text.UTF8Encoding]::new($false))
    foreach ($line in $strings) { Write-InkTagTerminalMessage $line -Level $(if (-not $nativeOK -or $code) {'ERROR'} else {'DEBUG'}) }
    if (-not $nativeOK -or $code -ne 0) { throw "Offline validation failed; raw output: $LogPath" }
}
