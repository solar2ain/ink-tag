param(
    [ValidateSet("gr5331_zkc21v")]
    [string]$Board,
    [string]$StockBackup = "",
    [Alias("h")]
    [switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)]
    [object[]]$RemainingArguments
)
. (Join-Path $PSScriptRoot 'internal/standalone-entry.ps1')
try {
    if($Help -or ($RemainingArguments -contains '--help')) {
        Write-Output 'GR tools-only directory: no firmware included. Full uses the existing guarded pilot backend; this bundle adds no physical/release qualification. Select a compatible signed Firmware explicitly. Stock restore remains guarded.'
    }
    Invoke-InkTagGoodixBundledEntry -EntryRoot $PSScriptRoot -Mcu 'gr5331' -Action 'restore' -Parameters $PSBoundParameters
    exit $LASTEXITCODE
} catch { Write-Error $_.Exception.Message; exit 1 }
