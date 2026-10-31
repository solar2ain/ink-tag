[CmdletBinding()]
param(
    [string]$Programmer = "jlink",
    [Alias('Target')][string]$Chip = "nrf52832",
    [string]$Device = "nRF52832_xxAA",
    [string]$Uid = "",
    [Alias('Snr')][string]$JLinkSerial = "",
    [string]$JLinkExe = "",
    [int]$FrequencyHz = 500000,
    [string]$ConnectMode = "halt",
    [switch]$RetryDetect,
    [switch]$NoWait,
    [int]$IntervalSeconds = 1,
    [Parameter(HelpMessage='Overall readiness wait in seconds; 0 is unlimited only with explicit RetryDetect.')][int]$TimeoutSeconds = 0,
    [Parameter(HelpMessage='Maximum seconds for one vendor operation; timeout never retries an unknown result.')]
    [ValidateRange(1,86400)][int]$OperationTimeoutSeconds = 60,
    [switch]$DryRun,
    [Alias("h")]
    [switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)]
    [object[]]$RemainingArguments
)
. (Join-Path $PSScriptRoot 'internal/runtime.ps1')
. (Join-Path $PSScriptRoot 'internal/terminal-log.ps1')
$terminalState = Start-InkTagHardwareTerminal
try {
$ErrorActionPreference='Stop'

. (Join-Path (Get-NordicRuntimeRoot) 'tools/powershell-cli.ps1')
$guard = Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested ($Help -or $Programmer -eq '--help') `
    -RemainingArguments $RemainingArguments -Summary "Read-only nRF52832 xxAA SWD identity probe; no PCB selection; RetryDetect is opt-in"
if ($null -ne $guard) { exit $guard }
if ($Chip -ne 'nrf52832' -or $Device -ne 'nRF52832_xxAA') {
    throw 'This read-only tool supports -Chip nrf52832 / nRF52832_xxAA only; no PCB identity is inferred.'
}
if ($Programmer -notin @("daplink", "jlink")) {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "Programmer must be daplink or jlink")
}
if ($ConnectMode -notin @("halt", "pre-reset", "under-reset", "attach")) {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "ConnectMode must be halt, pre-reset, under-reset or attach")
}
if ($RetryDetect -and $NoWait) {
    throw 'Use -RetryDetect or legacy -NoWait, not both.'
}
if ($FrequencyHz -lt 1000 -or $FrequencyHz % 1000 -ne 0 -or $IntervalSeconds -lt 1 -or $TimeoutSeconds -lt 0) {
    throw 'FrequencyHz must be whole kHz >= 1000, IntervalSeconds >= 1 and TimeoutSeconds >= 0; no device accessed.'
}
if (($Programmer -eq 'jlink' -and ($Uid -or $ConnectMode -ne 'halt')) -or
    ($Programmer -eq 'daplink' -and ($JLinkSerial -or $JLinkExe))) { throw 'Transport option mismatch: J-Link uses JLinkSerial and qualified halt mode; DAPLink uses Uid/ConnectMode. No device accessed.' }

$impl = Join-Path $PSScriptRoot "internal/invoke-nrf52832-swd.ps1"
$effectiveWait = [bool]$RetryDetect
if ($DryRun) {
    [ordered]@{operation='probe';chip=$Chip;boardSelected=$false;programmer=$Programmer;connectMode=$ConnectMode;
        retryDetect=$effectiveWait;retryScope='Detection only; never replay identity reads';
        frequencyHz=$FrequencyHz;identityConfirmed=$false;deviceAccess=$false;
        warning='Real SWD attach may halt/reset depending on ConnectMode; probe is not a passive RF scan'} | ConvertTo-Json
    exit 0
}
Assert-NordicBundlePrerequisites $Programmer $JLinkExe
Write-InkTagHardwareStage 'PROBE'
Write-InkTagHardwareMessage 'PROBE: SWD transport, exact FICR identity still unconfirmed; no Flash programming.'
Write-InkTagHardwareMessage "Programmer=$Programmer; chip=$Chip; SWD=$FrequencyHz Hz; connect=$ConnectMode; RetryDetect=$effectiveWait"
& $impl -Operation probe -Programmer $Programmer -Target $Chip -Device $Device -Uid $Uid -Snr $JLinkSerial `
    -JLinkExe $JLinkExe -FrequencyHz $FrequencyHz -ConnectMode $ConnectMode -Wait $effectiveWait `
    -IntervalSeconds $IntervalSeconds -TimeoutSeconds $TimeoutSeconds -OperationTimeoutSeconds $OperationTimeoutSeconds `
    -Verbose:([bool]$PSBoundParameters['Verbose'])
$result = $LASTEXITCODE
if ($result -ne 0) { Write-InkTagHardwareMessage "Probe failed (exit $result); no Flash programmed. Use -Verbose for diagnostics." -Level ERROR }
exit $result
} catch {
    Write-InkTagHardwareMessage $_.Exception.Message -Level ERROR
    exit 1
} finally { Restore-InkTagHardwareTerminal $terminalState }
