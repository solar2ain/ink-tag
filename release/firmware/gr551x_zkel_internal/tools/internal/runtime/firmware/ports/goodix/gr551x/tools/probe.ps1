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
$grProbePublicEntry=$PSCommandPath
$grProbePublicParameters=$PSBoundParameters
. (Join-Path $PSScriptRoot '../../common/tools/internal/goodix-probe-entry.ps1')
