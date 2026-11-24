<#
.SYNOPSIS
Build a matching App, Recovery, GUI and complete flashing package.
.DESCRIPTION
Uses the existing Python builder and Windows WSL toolchain. Recovery is CRC-only. This entry does not sign firmware or access devices.
.EXAMPLE
.\build.ps1
.EXAMPLE
.\build.ps1 -SdkRoot C:/SDK/nRF5_SDK_17.1.0_ddde560 -ToolchainPath C:/Tools/arm/bin
#>
[CmdletBinding()]
param(
    [string]$SdkRoot = '',
    [string]$ToolchainPath = '',
    [ValidateRange(1, 64)]
    [int]$Jobs = 4,
    [string]$BuildRoot = '',
    [string]$OutputRoot = '',
    [Alias('h')]
    [switch]$Help
)

$ErrorActionPreference = 'Stop'
$pythonExe = (Get-Command python -CommandType Application -ErrorAction Stop |
    Select-Object -First 1).Source
$buildArguments = @('-B', (Join-Path $PSScriptRoot 'build.py'))
if ($Help) {
    $buildArguments += '--help'
} else {
    $buildArguments += @('--jobs', [string]$Jobs)
    foreach ($option in @(
        @('--sdk-root', $SdkRoot),
        @('--toolchain-path', $ToolchainPath),
        @('--build-root', $BuildRoot),
        @('--output-root', $OutputRoot)
    )) {
        if ($option[1]) { $buildArguments += $option }
    }
}

$buildExitCode = 1
Push-Location -LiteralPath $PSScriptRoot
try {
    & $pythonExe @buildArguments
    $buildExitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $buildExitCode
