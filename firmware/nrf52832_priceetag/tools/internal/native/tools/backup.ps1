[CmdletBinding()]
param(
    [Parameter(HelpMessage='Required exact operator PCB label, including spaces/case; naming metadata only, not a silicon selector.')][string]$Board = '',
    [Parameter(HelpMessage='Optional bounded single-line operator note; never changes the selected chip.')][string]$BoardNote = '',
    [string]$Programmer = "jlink",
    [string]$OutputDir = "",
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
    [Parameter(HelpMessage='Maximum seconds for each independent vendor operation; failed captures are retained, never retried.')]
    [ValidateRange(1,86400)][int]$OperationTimeoutSeconds = 180,
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
$doubleDashHelp = $Board -eq "--help"
if ($doubleDashHelp) { $Board = "" }
$guard = Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested ($Help -or $doubleDashHelp) `
    -RemainingArguments $RemainingArguments -Summary "Back up nRF52832 xxAA Flash/FICR/UICR; Board is the required exact operator PCB label, not PCB qualification; RetryDetect is opt-in. Separate from Full; two independent reads and archive verification."
if ($null -ne $guard) { exit $guard }
if ([string]::IsNullOrWhiteSpace($Board)) { throw 'Backup requires -Board <exact PCB label>; no device accessed.' }
if ($Chip -ne 'nrf52832' -or $Device -ne 'nRF52832_xxAA') {
    throw 'This read-only tool supports -Chip nrf52832 / nRF52832_xxAA only.'
}
if ($Board.Length -gt 128 -or $BoardNote.Length -gt 512 -or $Board -match '[\p{Cc}\p{Zl}\p{Zp}]' -or $BoardNote -match '[\p{Cc}\p{Zl}\p{Zp}]') {
    throw 'Board/BoardNote must be bounded single-line operator labels; control characters are refused.'
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

$repoRoot = (Get-NordicRuntimeRoot)
$effectiveWait = [bool]$RetryDetect
if ($DryRun) {
    [ordered]@{operation='backup';board=$Board;boardNote=$BoardNote;boardEvidence='Operator label only';chip=$Chip;programmer=$Programmer;
        retryDetect=$effectiveWait;retryScope='Detection only; two independent read passes, each attempted once; partial reads retained';
        independentReadCount=2;operationTimeoutSeconds=$OperationTimeoutSeconds;
        flashBytes=524288;ficrBytes=4096;uicrBytes=4096;output=$OutputDir;
        factoryProvenance='Unconfirmed until complete capture and exact classification';deviceAccess=$false} | ConvertTo-Json
    exit 0
}
Assert-NordicBundlePrerequisites $Programmer $JLinkExe
Write-InkTagHardwareStage 'BACKUP'
Write-InkTagHardwareMessage 'BACKUP: two independent Flash/FICR/UICR reads; no factory provenance inferred from a filename.'
. (Join-Path $PSScriptRoot 'internal/backup-archive.ps1')
$defaultPath = $OutputDir -eq ''
if ($defaultPath) {
    $OutputDir = Get-NordicCaptureDirectory (Get-NordicCanonicalRoot $repoRoot) 'quarantine-source-unconfirmed'
}
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
Assert-NordicArchivePath $OutputDir
if (Test-Path -LiteralPath $OutputDir) { throw 'Backup requires a new independent session directory; never overwrite a previous capture.' }
if ($OutputDir.Replace('\', '/') -match '(?i)/\.local/backups(?:/|$)') {
    throw 'Explicit output cannot claim factory provenance. Use the default classification flow or a diagnostic archive path.'
}
$impl = Join-Path $PSScriptRoot "internal/invoke-nrf52832-swd.ps1"
Write-InkTagHardwareMessage "Programmer=$Programmer; chip=$Chip; SWD=$FrequencyHz Hz; connect=$ConnectMode; RetryDetect=$effectiveWait"
Write-InkTagHardwareMessage "PCB label=$Board; output=$OutputDir"
& $impl -Operation backup -Programmer $Programmer -Target $Chip -Device $Device -Uid $Uid -Snr $JLinkSerial `
    -JLinkExe $JLinkExe -FrequencyHz $FrequencyHz -ConnectMode $ConnectMode -Wait $effectiveWait `
    -IntervalSeconds $IntervalSeconds -TimeoutSeconds $TimeoutSeconds -OutputDir $OutputDir -Board $Board -BoardNote $BoardNote `
    -OperationTimeoutSeconds $OperationTimeoutSeconds -Verbose:([bool]$PSBoundParameters['Verbose'])
$captureExit = $LASTEXITCODE
if ($captureExit -ne 0) { Write-InkTagHardwareMessage "Backup failed (exit $captureExit); partial evidence retained, no automatic replay." -Level ERROR; exit $captureExit }
Complete-NordicCapture $repoRoot $OutputDir $defaultPath
Write-InkTagHardwareMessage 'Congratulations! Your backup is complete: both independent reads matched and archive validation passed. Factory provenance is reported separately.' -Level SUCCESS
exit 0
} catch {
    Write-InkTagHardwareMessage $_.Exception.Message -Level ERROR
    exit 1
} finally { Restore-InkTagHardwareTerminal $terminalState }
