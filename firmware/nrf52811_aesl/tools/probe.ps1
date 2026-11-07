[CmdletBinding()]
param(
    [ValidateSet('nrf52811')][string]$Chip,
    [string]$JLinkExe = "",
    [Alias('JLinkSerial')][string]$Snr = "",
    [int]$FrequencyHz = 500000,
    [switch]$RetryDetect,
    [switch]$DryRun,
    [int]$IntervalSeconds = 1,
    [int]$TimeoutSeconds = 0,
    [ValidateRange(1,86400)][int]$OperationTimeoutSeconds = 120,
    [string]$OutputDir = "",
    [string]$PythonExe = "python",
    [Alias("h")][switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)][object[]]$RemainingArguments
)
. (Join-Path $PSScriptRoot 'internal/terminal-log.ps1')
$terminalState = Get-InkTagTerminalColour
try {
Set-InkTagTerminalEnglish
. (Join-Path $PSScriptRoot 'internal/runtime-context.ps1')
$context = Get-InkTagRuntimeContext $PSScriptRoot
. $context.CliHelper
$guard = Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help -RemainingArguments $RemainingArguments -Summary "Probe an nRF52811 board with J-Link 7.88j"
if ($null -ne $guard) { exit $guard }
if ($context.IsBundle -and ($CaptureConfig -or $PSBoundParameters.ContainsKey('ReadAddress'))) { throw 'Developer memory/configuration diagnostics are not provided by the user tool bundle' }
if (-not $Chip) { exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message 'Missing required -Chip nrf52811; probe does not select a PCB') }
Assert-InkTagHostArguments $Snr $FrequencyHz $IntervalSeconds $TimeoutSeconds
$entry = Join-Path $PSScriptRoot "internal/capture-jlink.ps1"
$read = @{}
if ($PSBoundParameters.ContainsKey("ReadAddress")) {
    if ($CaptureConfig) { throw "Use ReadAddress or CaptureConfig, not both" }
    $read["ReadAddress"] = $ReadAddress
    $read["ReadWords"] = $ReadWords
    $read["ReadCount"] = $ReadCount
    $read["ReadIntervalMs"] = $ReadIntervalMs
}
$operation = if ($CaptureConfig) { "capture" } else { "probe" }
if ($DryRun) {
    Write-InkTagTerminalMessage "DryRun: nrf52811 $operation arguments valid; no hardware opened. A real probe may halt/resume the CPU."
    exit 0
}
Write-InkTagTerminalStage 'PROBE'
Write-InkTagTerminalMessage 'nRF52811 SWD probe; exact silicon is still unconfirmed. No PCB is selected. SWD may halt/resume the CPU.'
Write-InkTagTerminalMessage "Programmer=J-Link; SWD=$FrequencyHz Hz; RetryDetect=$($RetryDetect.IsPresent); operation timeout=${OperationTimeoutSeconds}s"
$environment = Assert-InkTagBundleEnvironment $context $PythonExe $JLinkExe
$PythonExe = $environment.PythonExe; $JLinkExe = $environment.JLinkExe
& $entry -Operation $operation -JLinkExe $JLinkExe -Snr $Snr -FrequencyHz $FrequencyHz -Wait ([bool]$RetryDetect) -IntervalSeconds $IntervalSeconds -TimeoutSeconds $TimeoutSeconds -OperationTimeoutSeconds $OperationTimeoutSeconds -OutputDir $OutputDir -PythonExe $PythonExe -Verbose:($VerbosePreference -eq 'Continue') @read
$probeExit = $LASTEXITCODE
if ($probeExit -ne 0) { Write-InkTagTerminalMessage "Probe failed (exit $probeExit); no Flash programmed. Detailed output is in the evidence logs." -Level ERROR }
exit $probeExit
} catch { Write-InkTagTerminalMessage $_.Exception.Message -Level ERROR; exit 1 }
finally { try { Complete-InkTagTerminalProgress } finally { Restore-InkTagTerminalColour $terminalState } }
