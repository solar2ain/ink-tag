[CmdletBinding()]
param([string]$Python='',[string]$JLinkHome='',[switch]$OfflinePackageOnly,[string]$Firmware='',[string]$Config='',[Alias('h')][switch]$Help)
if($Help) {
    Write-Output 'check.ps1 [-Python <path>] [-JLinkHome <7.88j-directory>] [-OfflinePackageOnly] [-Firmware <external-signed-Full.zip>] [-Config <PCB.toml>]'
    Write-Output 'Offline tools/dependency/config checks; optional external Firmware signature/pair checks. No probe, BLE, service or network access.'
    Write-Output 'OfflinePackageOnly excludes installed native tools/device pack; it is NOT a hardware-readiness result.'
    exit 0
}
try {
    . (Join-Path $PSScriptRoot 'internal/delivery-common.ps1')
    $root=Get-OmDeliveryRoot
    $null=Assert-OmBundleFiles $root
    $pythonExe=Get-OmEnvironmentPython $root $Python
    Assert-OmPython $root $pythonExe $Firmware $Config
    if(!$OfflinePackageOnly) {
        Assert-OmNative (Get-OmNativeHome $JLinkHome)
        Assert-OmDevicePack (Get-OmPackRoot)
    }
    Write-Output 'CHECK PASSED: offline checks only; no hardware qualification is inferred.'
    exit 0
} catch {Write-Error ($_.Exception.Message+' See tools/setup.ps1 -Help and DEPENDENCIES.txt.');exit 1}
