[CmdletBinding()]
param(
    [Parameter(HelpMessage='PCB TOML/YAML/JSON configuration, including fitted NFC variant.')][string]$Config = '',
    [Parameter(HelpMessage='Signed app-full.zip matching Config: large GR551x, or no-auxiliary GR5513 internal-image family.')][string]$Firmware = '',
    [switch]$DryRun,
    [Parameter(HelpMessage='Confirm Full clears MCU user data/history but retains verified same-device Pro; fitted GR5513 external Flash remains; otherwise asks y/N.')][switch]$Yes,
    [string]$JLinkSerial = '',
    [Parameter(HelpMessage='SWD identity/diagnostic frequency only; GProgrammer controls Flash transfer frequency.')][ValidateScript({$_ -ge 1000 -and $_ -le 1000000 -and $_ % 1000 -eq 0})][int]$FrequencyHz = 500000,
    [switch]$RetryDetect,
    [Alias('TimeoutSeconds')][ValidateRange(1,3600)][int]$DetectTimeoutSeconds = 120,
    [ValidateRange(30,1800)][int]$OperationTimeoutSeconds = 300,
    [string]$Python = 'python',
    [Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)][object[]]$RemainingArguments
)
. (Join-Path $PSScriptRoot 'internal/standalone-entry.ps1')
try {
    if($Help -or ($RemainingArguments -contains '--help')) {
        Write-Output 'GR tools-only directory: no firmware included. Full uses the existing guarded pilot backend; this bundle adds no physical/release qualification. Select a compatible signed Firmware explicitly. Stock restore remains guarded.'
    }
    Invoke-InkTagGoodixBundledEntry -EntryRoot $PSScriptRoot -Mcu 'gr551x' -Action 'flash' -Parameters $PSBoundParameters
    exit $LASTEXITCODE
} catch { Write-Error $_.Exception.Message; exit 1 }
