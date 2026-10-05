param(
    [string]$Board = "",
    [string]$Firmware = "",
    [string]$StockBackup = "",
    [string]$OmSdkRoot = "",
    [string]$JLinkExe = "",
    [string]$Snr = "",
    [int]$FrequencyHz = 1000000,
    [switch]$NoWait,
    [Alias("h")][switch]$Help
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "release-common.ps1")

$boards = @("om6626_zkc21b", "om6626_zkc42b")
if ($Help) {
    Show-InkTagFlashHelp -Title "InkTag OM6626 Full flasher (Windows)" `
        -Usage '.\flash-om6626.ps1 -Board <board-id> -Firmware <full.zip> -StockBackup <stock.bin> [-OmSdkRoot <OM6626.SDK>]' `
        -Notes @(
            "Boards: $($boards -join ', ')",
            "Requires SEGGER J-Link Commander 7.88j and the OM6626 J-Link device pack from the public OM6626 SDK.",
            "The verified three-stage state/code/device-information order is fixed. Waiting is enabled by default."
        )
    exit 0
}
if ($Board -notin $boards) { throw "-Board must be one of: $($boards -join ', ')" }
if ($StockBackup -eq "") { throw "-StockBackup is required before destructive first flash." }
Assert-InkTagStockBackup -Path $StockBackup -AllowedSizes @(0x100000)
$resolved = Resolve-InkTagFullArtifact -Board $Board -Firmware $Firmware
if ([IO.Path]::GetExtension($resolved.Path).ToLowerInvariant() -ne ".zip") {
    throw "OM6626 Full must be the release ZIP containing factory-code/state/device-information HEX files."
}

$runtimeRoot = Join-Path (Get-InkTagFlashRoot) "runtime"
if ($OmSdkRoot -ne "") {
    $sdk = (Resolve-Path -LiteralPath $OmSdkRoot).Path
    $pack = Join-Path $sdk "tools\plugins\jlink\JLinkDevices.xml"
    if (!(Test-Path -LiteralPath $pack -PathType Leaf)) { throw "OM6626 SDK J-Link pack not found below: $sdk" }
    $sdkParent = Join-Path $runtimeRoot ".sdk\onmicro"
    $sdkLink = Join-Path $sdkParent "OM6626.SDK"
    New-Item -ItemType Directory -Force -Path $sdkParent | Out-Null
    if (!(Test-Path -LiteralPath $sdkLink)) {
        New-Item -ItemType Junction -Path $sdkLink -Target $sdk | Out-Null
    }
}

$temporary = Join-Path ([IO.Path]::GetTempPath()) ("inktag-om-full-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $temporary | Out-Null
try {
    Expand-Archive -LiteralPath $resolved.Path -DestinationPath $temporary
    $code = @(Get-ChildItem -LiteralPath $temporary -Recurse -File -Filter "*-factory-code.hex")
    $state = @(Get-ChildItem -LiteralPath $temporary -Recurse -File -Filter "*-factory-state.hex")
    $deviceInfo = @(Get-ChildItem -LiteralPath $temporary -Recurse -File -Filter "*-factory-device-info.hex")
    if ($code.Count -ne 1 -or $state.Count -ne 1 -or $deviceInfo.Count -ne 1) {
        throw "OM6626 Full ZIP must contain exactly one code, state and device-information HEX file."
    }
    $entry = Join-Path $runtimeRoot "firmware\ports\onmicro\om6626\tools\flash.ps1"
    if (!(Test-Path -LiteralPath $entry -PathType Leaf)) { throw "Release flash runtime is incomplete: $entry" }
    $arguments = @{
        Board = $Board
        Firmware = $code[0].FullName
        FactoryState = $state[0].FullName
        FactoryDeviceInfo = $deviceInfo[0].FullName
        StockBackup = (Resolve-Path -LiteralPath $StockBackup).Path
        FrequencyHz = $FrequencyHz
    }
    if ($JLinkExe -ne "") { $arguments.JLinkExe = $JLinkExe }
    if ($Snr -ne "") { $arguments.Snr = $Snr }
    if ($NoWait) { $arguments.NoWait = $true }
    Write-Host "Verified release artifact: $($resolved.Artifact.name)"
    & $entry @arguments
    exit $LASTEXITCODE
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}
