param(
    [ValidateSet("gr551x_zkc42vm")]
    [string]$Board,
    [string]$StockBackup = "",
    [Alias("h")]
    [switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)]
    [object[]]$RemainingArguments
)

. (Join-Path $PSScriptRoot "..\..\..\..\..\tools\powershell-cli.ps1")
$cliResult = Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help `
    -RemainingArguments $RemainingArguments -Summary "Restore a GR551x board's exact stock XIP Flash image"
if ($null -ne $cliResult) { exit $cliResult }
if (-not $Board) {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath `
        -Message "Missing required -Board. Supported board: gr551x_zkc42vm")
}
if ($StockBackup -eq "") {
    throw "GR551x restore requires -StockBackup pointing to the exact complete factory dump for this physical board."
}
$RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..'))
. (Join-Path $RepoRoot 'firmware/ports/goodix/common/tools/internal/goodix-backup-archive.ps1')
$StockBackup = Resolve-InkTagGoodixHistoricalBackupPath -RepoRoot $RepoRoot -Path $StockBackup
# Resolving a preserved path is not provenance or restore qualification.

throw "GR551x stock restore is intentionally guarded until byte-for-byte external-XIP writeback and recovery have passed destructive hardware validation."
