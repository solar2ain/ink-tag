[CmdletBinding()]
param(
    [ValidateSet('gr5331','gr551x','gr5513','gr5515')][string]$Chip='',
    [string]$OutputDir='', [string]$JLinkExe='',
    [Alias('Snr')][string]$JLinkSerial='',
    [int]$FrequencyHz=500000,
    [Parameter(HelpMessage='Retry detection only; never replay a read or write.')][switch]$RetryDetect,
    [int]$IntervalSeconds=1, [int]$TimeoutSeconds=120,
    [int]$OperationTimeoutSeconds=60,
    [switch]$DryRun,
    [Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments=$true)][object[]]$RemainingArguments
)
. (Join-Path $PSScriptRoot 'internal/standalone-entry.ps1')
try {
    if($Help -or ($RemainingArguments -contains '--help')) {
        Write-Output 'GR tools-only directory: no firmware included. Full uses the existing guarded pilot backend; this bundle adds no physical/release qualification. Select a compatible signed Firmware explicitly. Stock restore remains guarded.'
    }
    Invoke-InkTagGoodixBundledEntry -EntryRoot $PSScriptRoot -Mcu 'gr551x' -Action 'probe' -Parameters $PSBoundParameters
    exit $LASTEXITCODE
} catch { Write-Error $_.Exception.Message; exit 1 }
