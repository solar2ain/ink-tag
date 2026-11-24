# Presentation only; no probe, retry, read/write or result-verification logic.
# Stable progress contract plus the frozen TLSR 2026-10-16 presentation.
# Console/RawUI helpers reviewed in OM ba183897; do not import vendor parsers.
# Progress API: tlsr-terminal-contract-v1, ZIP SHA CEAA2E60...A002BF5F.
# No OM/TLSR vendor-log parser is imported here.
$script:InkTagTerminalProgressActive = $false
function Complete-InkTagTerminalProgress {
    if ($script:InkTagTerminalProgressActive) {
        try { Write-Host '' }
        finally { $script:InkTagTerminalProgressActive = $false }
    }
}
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
    if ($State.ContainsKey('UICulture')) { [Threading.Thread]::CurrentThread.CurrentUICulture = $State.UICulture }
    try {
        if ($State.ContainsKey('Background')) { [Console]::BackgroundColor = $State.Background }
        if ($State.ContainsKey('Foreground')) { [Console]::ForegroundColor = $State.Foreground }
    } catch {}
}
function Test-InkTagInteractiveTerminal {
    try { return ($Host.Name -eq 'ConsoleHost' -and ![Console]::IsOutputRedirected -and ![Console]::IsErrorRedirected) } catch { return $false }
}
function Write-InkTagTerminalMessage {
    param([string]$Message, [ValidateSet('INFO','DEBUG','WARNING','ERROR','SUCCESS')][string]$Level = 'INFO')
    if ($Level -eq 'DEBUG' -and $VerbosePreference -ne 'Continue') { return }
    $prefix = '[{0}] [{1}]' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:sszzz'), $Level
    $colour = switch ($Level) { 'ERROR' { 'Red' }; 'SUCCESS' { 'Green' }; 'WARNING' { 'Yellow' }; 'DEBUG' { 'DarkGray' }; default { 'Cyan' } }
    $state = Get-InkTagTerminalColour
    try {
        Complete-InkTagTerminalProgress
        # Retain original control bytes only in the existing raw log files.
        $plain = [regex]::Replace($Message, '\x1B\[[0-?]*[ -/]*[@-~]', '')
        $plain = [regex]::Replace($plain, '[\x00-\x1F\x7F]', ' ')
        if ((Test-Path Env:NO_COLOR) -or !(Test-InkTagInteractiveTerminal)) { Write-Host "$prefix $plain" }
        else { Write-Host "$prefix $plain" -ForegroundColor $colour }
    } finally { Restore-InkTagTerminalColour $state }
}
function Format-InkTagTerminalProgressLine {
    param([string]$Message, [string]$Prefix, [int]$Columns)
    $full = "$Prefix $Message"
    if ($full.Length -le $Columns) { return $full }
    if ($Message.Length -le $Columns) { return $Message }
    return $null # Emit the complete body on a newline; never truncate counts.
}
function Write-InkTagTerminalProgress {
    param([string]$Message)
    $state = Get-InkTagTerminalColour
    try {
        $plain = [regex]::Replace($Message, '\x1B\[[0-?]*[ -/]*[@-~]', '')
        $plain = [regex]::Replace($plain, '[\x00-\x1F\x7F]', ' ')
        $columns = 0
        if (!(Test-Path Env:NO_COLOR) -and (Test-InkTagInteractiveTerminal)) { try { $columns = [Console]::WindowWidth - 1 } catch {} }
        if ($columns -lt 40) { Write-InkTagTerminalMessage $plain; return }
        $prefix = '[{0}] [INFO]' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:sszzz')
        $line = Format-InkTagTerminalProgressLine -Message $plain -Prefix $prefix -Columns $columns
        if ($null -eq $line) { Write-InkTagTerminalMessage $plain; return }
        $line = "`r" + $line.PadRight($columns)
        if (Test-Path Env:NO_COLOR) { Write-Host $line -NoNewline }
        else { Write-Host $line -NoNewline -ForegroundColor Cyan }
        $script:InkTagTerminalProgressActive = $true
    } finally { Restore-InkTagTerminalColour $state }
}
function Write-InkTagTerminalStage {
    param([string]$Name)
    Write-InkTagTerminalMessage ("-------------------- {0} --------------------" -f $Name)
}
function Show-InkTagGoodixTerminalLog {
    [CmdletBinding()]
    param([Parameter(ValueFromPipeline=$true)][object]$InputObject)
    process {
        $line = [string]$InputObject
        if ($line -match '^\[\d{4}-\d{2}-\d{2} [^\]]+\] \[(INFO|DEBUG|WARNING|ERROR|SUCCESS)\] (.*)$') {
            $level = $Matches[1]; $message = $Matches[2]
            # Only the existing logger's normalized completed-count rows.
            # Bare native percentages, start addresses and waits stay messages.
            if ($level -eq 'INFO' -and $message -match '\d+(?:\.\d+)?%.*\d+(?:\.\d+)?/\d+(?:\.\d+)? KiB.*elapsed \d') {
                # Compact the live line only; raw logs retain the original.
                $display = $message -replace '^\[(?:GR|gr5331|gr551[35x])\]\s*', ''
                # Verbose rows retain [phase]; ordinary rows use bare phase.
                $display = $display -replace '^(?:\[read-1\]|read-1\b)', 'Read 1/2' -replace '^(?:\[read-2\]|read-2\b)', 'Read 2/2'
                $display = $display -replace '^(Read [12]/2)\s+Reading\s+', '$1 '
                $display = $display -replace '^single-full-write\b', 'Write' -replace '^full-readback\b', 'Verify'
                $display = $display -replace '^(?i)Read/verify', 'Verify' -replace '^(?i)Reading', 'Read' -replace '^(?i)Programming', 'Write'
                $display = $display -replace '[()]', '' -replace '(?:\s*\|\s*|;\s*)elapsed ', '  '
                Write-InkTagTerminalProgress $display
            } elseif($level -eq 'INFO' -and $message -match 'Vendor (read|write) progress: (\d+(?:\.\d+)?)%') {
                $operation=$Matches[1];$value=$Matches[2]
                $caption=if($operation -eq 'write'){'Write'}elseif($message -match 'full-readback'){'Verify'}elseif($message -match 'read-1'){'Read 1/2'}elseif($message -match 'read-2'){'Read 2/2'}else{'Read'}
                $filled=[Math]::Min(20,[Math]::Max(0,[int][Math]::Floor([double]$value/5)))
                $bar=('#'*$filled)+('.'*(20-$filled))
                Write-InkTagTerminalProgress "$caption [$bar] $value%"
            } else { Write-InkTagTerminalMessage $message -Level $level }
        } elseif ($InputObject -is [Management.Automation.ErrorRecord] -or $line -match '^ERROR:') {
            Write-InkTagTerminalMessage $line -Level ERROR
        } else {
            Write-InkTagTerminalMessage $line -Level DEBUG
        }
    }
    end { Complete-InkTagTerminalProgress }
}
function Invoke-InkTagGoodixHostProcess {
    param([string]$Python, [string]$Script, [string[]]$Options, [switch]$MachineReadable)
    # Native stderr is presentation input, not an alternative success/failure
    # classifier. The original process exit code remains authoritative.
    $savedErrorPreference = $ErrorActionPreference
    $processExit = 1
    try {
        $null = Get-Command -Name $Python -ErrorAction Stop
        $ErrorActionPreference = 'Continue'
        $PSNativeCommandUseErrorActionPreference = $false
        $global:LASTEXITCODE = 1
        if ($MachineReadable) {
            & $Python $Script @Options 2>&1 | ForEach-Object {
                if ($_ -is [Management.Automation.ErrorRecord] -or [string]$_ -match '^ERROR:') {
                    Write-InkTagTerminalMessage ([string]$_) -Level ERROR
                } else { Write-Output $_ }
            }
        } else {
            & $Python $Script @Options 2>&1 | Show-InkTagGoodixTerminalLog
        }
        $processExit = $LASTEXITCODE
    } catch [Management.Automation.PipelineStoppedException] { throw }
    catch [OperationCanceledException] { throw }
    catch {
        Write-InkTagTerminalMessage ('Cannot start/receive host tool: ' + $_.Exception.Message) -Level ERROR
    } finally { $ErrorActionPreference = $savedErrorPreference }
    $global:LASTEXITCODE = $processExit
}
