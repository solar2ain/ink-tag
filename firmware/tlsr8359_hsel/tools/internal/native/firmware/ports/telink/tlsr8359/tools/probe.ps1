[CmdletBinding()]
param(
    [Parameter(HelpMessage='Required MCU selector: tlsr8359 or hs9118. Does not select a PCB; HS9118 marking alone is not silicon proof.')][string]$Chip = "",
    [Parameter(HelpMessage='Serial port, for example COM13. Full requires an exact port; probe/backup may auto-select a qualified adapter.')][string]$Port = "auto",
    [Parameter(HelpMessage='Probe UART baud; 0 selects the existing transport default. Does not select a Flash-write speed.')][int]$Baud = 0,
    [Parameter(HelpMessage='Probe activation window in milliseconds; 0 selects the existing transport default.')][int]$ActivationMs = 0,
    [Parameter(HelpMessage='SWS response-clock timing estimate in MHz; -1 uses the existing default, 0 selects automatic estimation. Does not change the MCU runtime clock.')][int]$ClockMHz = -1,
    [Parameter(HelpMessage='Retry detection only; never automatically repeat Flash programming or an unknown write.')][switch]$RetryDetect,
    [Parameter(HelpMessage='Seconds between outer detection attempts; not a data-read retry interval.')][int]$IntervalSeconds = 1,
    [Parameter(HelpMessage='Overall detection-wait limit in seconds; 0 means no overall wait limit.')][int]$TimeoutSeconds = 0,
    [Parameter(HelpMessage='Maximum seconds for one tool operation; does not replay failed operations.')][int]$OperationTimeoutSeconds = 60,
    [Parameter(HelpMessage='Optional evidence/output directory; backup uses the maintained factory archive layout by default.')][string]$OutputDir = "",
    [Parameter(HelpMessage='Optional path to the maintained UART/SWS backend; normally discovered automatically.')][string]$ToolPath = "",
    [Parameter(HelpMessage='Python executable for the maintained host tools.')][string]$Python = "python",
    [Parameter(HelpMessage='Validate and print the offline plan only; no serial/reset/Flash/device access.')][switch]$DryRun,
    [Parameter(HelpMessage='Show all options and descriptions without accessing a device.')][Alias("h")][switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)][object[]]$RemainingArguments
)
. (Join-Path $PSScriptRoot 'internal/terminal-log.ps1')
$probeTerminalColour = Get-InkTagTerminalColour
try {
Set-InkTagTerminalEnglish
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "../../../../..")).Path
. (Join-Path $repoRoot "tools/powershell-cli.ps1")
$guard = Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help `
    -RemainingArguments $RemainingArguments -Summary "TLSR SWS probe: no Flash access; activation may reset/halt the target. Exact package identity may remain unconfirmed."
if ($null -ne $guard) { exit $guard }
if ($Chip -notin @('tlsr8359', 'hs9118')) { exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "-Chip tlsr8359 or hs9118 is required; probe does not select a PCB.") }
if ($Baud -lt 0 -or ($Baud -ne 0 -and $Baud -lt 340000) -or $ActivationMs -lt 0 -or $ActivationMs -gt 120000 -or $ClockMHz -lt -1 -or $ClockMHz -gt 192) {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message 'Probe baud must be 0 or >=340000, activation 0..120000 ms and clock estimate -1..192 MHz; no device accessed.')
}
. (Join-Path $PSScriptRoot "internal/tlsr8359-uart-sws-common.ps1")
$pythonExe = Resolve-TlsrPython -RequestedPython $Python
$engineArgs = @("probe", "--chip", $Chip, "--port", $Port,
    "--interval-seconds", "$IntervalSeconds", "--timeout-seconds", "$TimeoutSeconds",
    "--operation-timeout-seconds", "$OperationTimeoutSeconds", "--powershell", (Get-Process -Id $PID).Path)
if (!$RetryDetect) { $engineArgs += "--no-wait" }
if ($Baud) { $engineArgs += @("--probe-baud", "$Baud") }
if ($ActivationMs) { $engineArgs += @("--probe-activation-ms", "$ActivationMs") }
if ($ClockMHz -ge 0) { $engineArgs += @("--probe-clock-mhz", "$ClockMHz") }
if ($OutputDir) { $engineArgs += @("--log-dir", $OutputDir) }
if ($ToolPath) { $engineArgs += @("--tool-path", $ToolPath) }
if ($DryRun) { $engineArgs += "--dry-run" }
if ($PSBoundParameters.ContainsKey("Verbose") -and $PSBoundParameters["Verbose"]) { $engineArgs += "--verbose" }
if (!$DryRun -and $Host.Name -eq 'ConsoleHost' -and ![Console]::IsOutputRedirected) { $engineArgs += "--console-output" }
& $pythonExe (Join-Path $repoRoot "tools/hardware/flash-workflow.py") @engineArgs
exit $LASTEXITCODE
} catch {
    Write-InkTagTerminalMessage $_.Exception.Message -Level ERROR
    exit 1
} finally { Restore-InkTagTerminalColour $probeTerminalColour }
