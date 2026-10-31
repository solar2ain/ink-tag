[CmdletBinding()]
param(
    [Parameter(HelpMessage='Required MCU selector: om6626. Actual SYS identity registers are still checked; this does not override a different chip.')][ValidateSet('om6626')][string]$Chip,
    [Parameter(HelpMessage='Optional path to SEGGER JLink.exe 7.88j; otherwise resolve the maintained installation.')][string]$JLinkExe = "",
    [Parameter(HelpMessage='Optional exact USB probe serial. If omitted, SEGGER selects the probe; specify it when multiple probes are connected.')][string]$JLinkSerial = "",
    [Parameter(HelpMessage='SWD frequency in Hz, default 100000; positive whole kHz, at least 1000 Hz.')][ValidateScript({$_ -ge 1000 -and $_ % 1000 -eq 0})][int]$FrequencyHz = 100000,
    [Parameter(HelpMessage='halt: connect then halt (default); pre-reset: reset before connecting; under-reset: connect with reset held; attach: no explicit halt. Native J-Link connect fallback may still halt/reset. A successful probe releases the target.')][ValidateSet('halt','pre-reset','under-reset','attach')][string]$ConnectMode = 'halt',
    [Parameter(HelpMessage='Opt in to bounded Probe retry after explicit no-contact failure or a no-output contact timeout. Default is one attempt. Never retry a readable/wrong target, partial output or another error; no Flash programming is performed.')][switch]$RetryDetect,
    [Parameter(HelpMessage='Seconds between detection retries, default 1; range 1 to 3600. Used only with RetryDetect.')][ValidateRange(1,3600)][int]$IntervalSeconds = 1,
    [Parameter(HelpMessage='Detection retry window in seconds, default 120; range 1 to 3600. Checked between native attempts; does not interrupt a connected read.')][ValidateRange(1,3600)][int]$TimeoutSeconds = 120,
    [Parameter(HelpMessage='Legacy probe name for the INITIAL CONTACT timeout per attempt: default 12 seconds (previously 8), range 3 to 120. After CPUID is read, the backend allows its 300-second operation limit.')][ValidateRange(3,120)][int]$OperationTimeoutSeconds = 12,
    [Parameter(HelpMessage='Validate arguments and print the plan only; resolve no programmer and access no device.')][switch]$DryRun,
    [Alias("h")][switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)][object[]]$RemainingArguments
)

. (Join-Path $PSScriptRoot 'terminal-log.ps1')
$terminalState = Get-InkTagTerminalColour
try {
Set-InkTagTerminalEnglish
. (Join-Path $PSScriptRoot "..\..\..\..\..\tools\powershell-cli.ps1")
$cliResult = Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help `
    -RemainingArguments $RemainingArguments -Summary "Probe an OM6626 board with J-Link 7.88j"
if ($null -ne $cliResult) { exit $cliResult }
if (-not $Chip) { exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message 'Missing required -Chip om6626') }
if ($DryRun) {
    Write-InkTagTerminalMessage "DryRun: OM6626 identity probe; SWD $FrequencyHz Hz, connect $ConnectMode; no tool resolved, no device accessed. Probe may halt/reset a target when actually executed."
    exit 0
}
$entry = Join-Path $PSScriptRoot "internal/probe-om6626.ps1"
$forward = @{RetryDetect=$RetryDetect.IsPresent;IntervalSeconds=$IntervalSeconds;TimeoutSeconds=$TimeoutSeconds;AttemptTimeoutSeconds=$OperationTimeoutSeconds;
    FrequencyHz=$FrequencyHz; ConnectMode=$ConnectMode}
if ($JLinkExe) { $forward.JLinkExe=$JLinkExe }
if ($JLinkSerial) { $forward.Snr=$JLinkSerial }
Write-InkTagTerminalStage 'PROBE'
Write-InkTagTerminalMessage 'OM6626: waiting for SWD. Power on InkTag normally to use its 30-second attach window; BOOT low is only a stock/ROM ISP fallback. No byte progress is available for detection.'
& $entry @forward 3>&1 6>&1 | ForEach-Object {
    if ([string]$_ -match '^OM_PROBE_ATTEMPT ([0-9]+)$') { Write-InkTagTerminalMessage ("Detecting OM6626 via SWD, attempt {0}." -f $Matches[1]) }
    elseif ([string]$_ -match '^OM_PROBE_RETRY (.+)$') { Write-InkTagTerminalMessage $Matches[1] -Level WARNING }
    elseif ([string]$_ -match '^OM_PROBE_WAIT (.+)$') { Write-InkTagTerminalMessage $Matches[1] }
    elseif ($_ -is [Management.Automation.WarningRecord]) { Write-InkTagTerminalMessage ([string]$_) -Level WARNING }
    else { Show-OmProbeMessage ([string]$_) }
}
$probeExit = $LASTEXITCODE
exit $probeExit
} catch { Write-InkTagTerminalMessage $_.Exception.Message -Level ERROR; exit 1 }
finally { try { Complete-InkTagTerminalProgress } finally { Restore-InkTagTerminalColour $terminalState } }
