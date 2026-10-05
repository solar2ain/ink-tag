param(
    [string]$Board = "",
    [string]$Firmware = "",
    [string]$StockBackup = "",
    [string]$Port = "auto",
    [string]$ToolPath = "",
    [string]$Python = "python",
    [int]$Baud = 576000,
    [int]$ReadBaud = 921600,
    [switch]$AcknowledgeExperimentalLayout,
    [switch]$FullVerify = $true,
    [Alias("h")][switch]$Help
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "release-common.ps1")

$boards = @("hs_nowa154", "hs_nebular420")
if ($Help) {
    Show-InkTagFlashHelp -Title "InkTag Telink Full flasher (Windows)" `
        -Usage '.\flash-telink.ps1 -Board <board-id> -Firmware <full.bin> -StockBackup <backup-dir> -ToolPath <TLSR825xComFlasher.py> -AcknowledgeExperimentalLayout' `
        -Notes @(
            "Boards: $($boards -join ', ')",
            "Requires Python 3, pyserial, a qualified CH340/UART-to-SWS adapter and pinned pvvx/TlsrComSwireWriter.",
            "Full readback verification is enabled by default for release artifacts."
        )
    exit 0
}
if ($Board -notin $boards) { throw "-Board must be one of: $($boards -join ', ')" }
if ($StockBackup -eq "") { throw "-StockBackup is required before destructive first flash." }
if ($ToolPath -eq "") { throw "-ToolPath must point to the reviewed TLSR825xComFlasher.py." }
Assert-InkTagStockBackup -Path $StockBackup -AllowedSizes @(0x80000, 0x100000)
$resolved = Resolve-InkTagFullArtifact -Board $Board -Firmware $Firmware
$entry = Join-Path (Get-InkTagFlashRoot) "runtime\firmware\ports\telink\tlsr8359\tools\flash.ps1"
if (!(Test-Path -LiteralPath $entry -PathType Leaf)) { throw "Release flash runtime is incomplete: $entry" }
$arguments = @{
    Board = $Board
    Firmware = $resolved.Path
    StockBackup = (Resolve-Path -LiteralPath $StockBackup).Path
    Port = $Port
    ToolPath = $ToolPath
    Python = $Python
    Baud = $Baud
    ReadBaud = $ReadBaud
}
if ($AcknowledgeExperimentalLayout) { $arguments.AcknowledgeExperimentalLayout = $true }
if ($FullVerify) { $arguments.FullVerify = $true }
Write-Host "Verified release artifact: $($resolved.Artifact.name)"
& $entry @arguments
exit $LASTEXITCODE
