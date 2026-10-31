[CmdletBinding()]
param(
    [ValidateSet('nrf52811')][string]$Chip,
    [ValidateSet("nrf52811_asel22", "aesl22_nrf", "EL022HWRL", "EL022H2", "UNIDENTIFIED-PCB")][string]$Board,
    [string]$JLinkExe = "",
    [string]$Snr = "",
    [string]$OutputDir = "",
    [string]$BoardNote = "",
    [string]$PythonExe = "python",
    [int]$FrequencyHz = 500000,
    [switch]$RetryDetect,
    [switch]$DryRun,
    [int]$IntervalSeconds = 1,
    [int]$TimeoutSeconds = 0,
    [Alias("h")][switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)][object[]]$RemainingArguments
)
. (Join-Path $PSScriptRoot 'internal/terminal-log.ps1')
$terminalState = Get-InkTagTerminalColour
try {
. (Join-Path $PSScriptRoot 'internal/runtime-context.ps1')
$context = Get-InkTagRuntimeContext $PSScriptRoot
. $context.CliHelper
$guard = Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help -RemainingArguments $RemainingArguments -Summary "Back up nRF52811 Flash/FICR/UICR with J-Link 7.88j"
if ($null -ne $guard) { exit $guard }
if (-not $Chip) { exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message 'Missing required -Chip nrf52811') }
if (-not $Board) { exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message 'Missing required -Board nrf52811_asel22 (or UNIDENTIFIED-PCB)') }
Assert-InkTagHostArguments $Snr $FrequencyHz $IntervalSeconds $TimeoutSeconds
if ($BoardNote -and ($BoardNote -notmatch '^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$' -or $BoardNote.EndsWith('.') -or
    $BoardNote -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)')) { throw 'Invalid BoardNote' }
if ($OutputDir -and (([IO.Path]::GetFullPath($OutputDir) -split '[\\/]') -match '^(backups|factory_backups)$')) {
    throw 'Current/unknown captures require a diagnostic archive; factory backup admission is not provided by this entry'
}
$archiveBoard = if ($Board -eq 'UNIDENTIFIED-PCB') { 'UNIDENTIFIED-PCB' } else { 'aesl22_nrf' }
if ($DryRun) {
    Write-InkTagTerminalMessage "DryRun: two complete reads of nrf52811 internal Flash 192 KiB + FICR/UICR with matching SHA256; PCB=$archiveBoard; external Flash excluded; no hardware opened."
    exit 0
}
$entry = Join-Path $PSScriptRoot "internal/capture-jlink.ps1"
$environment = Assert-InkTagBundleEnvironment $context $PythonExe $JLinkExe
$PythonExe = $environment.PythonExe; $JLinkExe = $environment.JLinkExe
Write-InkTagTerminalMessage 'Read internal Flash and identity; detection may halt/resume the CPU. Please wait.'
& $entry -Operation backup -ArchiveBoard $archiveBoard -BoardNote $BoardNote -JLinkExe $JLinkExe -Snr $Snr -OutputDir $OutputDir -PythonExe $PythonExe -FrequencyHz $FrequencyHz -Wait ([bool]$RetryDetect) -IntervalSeconds $IntervalSeconds -TimeoutSeconds $TimeoutSeconds -Verbose:($VerbosePreference -eq 'Continue')
if (!$?) { exit 1 }
exit 0
} catch { Write-InkTagTerminalMessage $_.Exception.Message -Level ERROR; exit 1 }
finally { Restore-InkTagTerminalColour $terminalState }
