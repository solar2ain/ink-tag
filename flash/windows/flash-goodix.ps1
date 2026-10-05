param(
    [string]$Board = "",
    [string]$Firmware = "",
    [string]$StockBackup = "",
    [string]$ConsoleExe = "",
    [string]$JLinkExe = "",
    [string]$Snr = "",
    [int]$FrequencyHz = 500000,
    [switch]$NoWait,
    [Alias("h")][switch]$Help
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "release-common.ps1")

$boards = @("gr5331_zkc21v", "gr5513_zkc42v", "gr5513_zkc42vm", "gr5515_zkc42vm")
if ($Help) {
    Show-InkTagFlashHelp -Title "InkTag Goodix Full flasher (Windows)" `
        -Usage '.\flash-goodix.ps1 -Board <board-id> -Firmware <full.bin> -StockBackup <stock.bin> -ConsoleExe <GR5xxx_console.exe>' `
        -Notes @(
            "Boards: $($boards -join ', ')",
            "GR551x additionally requires SEGGER J-Link Commander 7.88j; pass -JLinkExe only when auto-detection fails.",
            "Waiting is enabled by default. Use -NoWait for one attempt."
        )
    exit 0
}
if ($Board -notin $boards) { throw "-Board must be one of: $($boards -join ', ')" }
if ($StockBackup -eq "") { throw "-StockBackup is required before destructive first flash." }
if ($ConsoleExe -eq "" -and $env:INKTAG_GPROGRAMMER_HOME) {
    $ConsoleExe = Join-Path $env:INKTAG_GPROGRAMMER_HOME "GR5xxx_console.exe"
}
if ($ConsoleExe -eq "" -or !(Test-Path -LiteralPath $ConsoleExe -PathType Leaf)) {
    throw "GR5xxx_console.exe was not found. Install Goodix GProgrammer, then pass -ConsoleExe or set INKTAG_GPROGRAMMER_HOME."
}

$resolved = Resolve-InkTagFullArtifact -Board $Board -Firmware $Firmware
$runtimeRoot = Join-Path (Get-InkTagFlashRoot) "runtime"
$common = @{
    Firmware = $resolved.Path
    StockBackup = (Resolve-Path -LiteralPath $StockBackup).Path
    ConsoleExe = (Resolve-Path -LiteralPath $ConsoleExe).Path
    GProgrammerDir = (Split-Path -Parent (Resolve-Path -LiteralPath $ConsoleExe).Path)
    FrequencyHz = $FrequencyHz
}
if ($Snr -ne "") { $common.Snr = $Snr }
if ($NoWait) { $common.NoWait = $true }

if ($Board -eq "gr5331_zkc21v") {
    Assert-InkTagStockBackup -Path $StockBackup -AllowedSizes @(0x80000, 0x100000)
    $common.Remove("FrequencyHz")
    $common.Board = "gr5331_zkc21v"
    $entry = Join-Path $runtimeRoot "firmware\ports\goodix\gr5331\tools\flash.ps1"
} else {
    $expectedBackup = if ($Board -eq "gr5515_zkc42vm") { @(0x100000) } else { @(0x80000, 0x100000) }
    Assert-InkTagStockBackup -Path $StockBackup -AllowedSizes $expectedBackup
    $common.Board = if ($Board -eq "gr5513_zkc42v") { "gr551x_zkc42v" } else { "gr551x_zkc42vm" }
    if ($JLinkExe -ne "") { $common.JLinkExe = $JLinkExe }
    $entry = Join-Path $runtimeRoot "firmware\ports\goodix\gr5515\tools\flash.ps1"
}
if (!(Test-Path -LiteralPath $entry -PathType Leaf)) { throw "Release flash runtime is incomplete: $entry" }

Write-Host "Verified release artifact: $($resolved.Artifact.name)"
& $entry @common
exit $LASTEXITCODE
