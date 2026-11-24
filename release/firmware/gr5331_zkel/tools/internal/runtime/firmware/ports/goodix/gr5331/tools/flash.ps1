[CmdletBinding()]
param(
    [Parameter(HelpMessage='PCB TOML/YAML/JSON configuration; never infers live MCU identity.')][string]$Config = '',
    [Parameter(HelpMessage='Production-signed app-full.zip; no BIN/ELF/SDK inputs.')][string]$Firmware = '',
    [switch]$DryRun,
    [Parameter(HelpMessage='Confirm Full clears MCU user data/history but retains verified same-device Pro; otherwise asks y/N before any device access.')][switch]$Yes,
    [string]$JLinkSerial = '',
    [Parameter(HelpMessage='SWD identity/diagnostic frequency only; GProgrammer controls Flash transfer frequency.')][ValidateScript({$_ -ge 1000 -and $_ -le 1000000 -and $_ % 1000 -eq 0})][int]$FrequencyHz = 500000,
    [switch]$RetryDetect,
    [Alias('TimeoutSeconds')][ValidateRange(1,3600)][int]$DetectTimeoutSeconds = 120,
    [ValidateRange(30,1800)][int]$OperationTimeoutSeconds = 300,
    [string]$Python = 'python',
    [Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)][object[]]$RemainingArguments
)
. (Join-Path $PSScriptRoot '../../common/tools/internal/goodix-configured-full.ps1')
Invoke-InkTagGoodixConfiguredFull -ScriptPath $PSCommandPath -Parameters $PSBoundParameters `
    -Config $Config -Firmware $Firmware -Yes:$Yes `
    -DryRun:$DryRun -Python $Python -HelpRequested:$Help `
    -RemainingArguments $RemainingArguments
exit $LASTEXITCODE
