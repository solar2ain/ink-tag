[CmdletBinding()]
param([string]$Python='python',[string]$DevicePackSource='',[Alias('h')][switch]$Help)
if($Help) {
    Write-Output 'setup.ps1 [-Python <Python-3.12.0-path>] [-DevicePackSource <official-extracted-jlink-pack>]'
    Write-Output 'Explicit offline setup: creates a package-local venv from bundled hash-locked wheels.'
    Write-Output 'J-Link software/USB driver must be installed manually from SEGGER under its license; driver installation may need administrator rights.'
    Write-Output 'Optional DevicePackSource copies only pinned device support into the ordinary user SEGGER directory; never overwrites existing files.'
    Write-Output 'No network, probe, reset, programming, global execution-policy or certificate/security changes.'
    exit 0
}
try {
    . (Join-Path $PSScriptRoot 'internal/delivery-common.ps1')
    $root=Get-OmDeliveryRoot
    $null=Assert-OmBundleFiles $root
    $basePython=Get-OmEnvironmentPython $root $Python
    $spec=Get-OmDependencySpec
    $actual= & $basePython -I -c 'import platform;print(platform.python_version())'
    if($LASTEXITCODE -ne 0 -or $actual -ne $spec.pythonVersion) {throw "Python $($spec.pythonVersion) x64 required; actual $actual"}
    $environment=Join-Path $root 'environment'
    if(Test-Path -LiteralPath $environment) {throw 'Environment already exists; refusing overwrite. Run check.ps1, or unpack a fresh bundle.'}
    & $basePython -I -m venv $environment
    if($LASTEXITCODE -ne 0) {throw 'Isolated environment creation failed'}
    $pythonExe=Join-Path $environment 'Scripts/python.exe'
    & $pythonExe -I -m pip install --disable-pip-version-check --no-index --only-binary=:all: --require-hashes --find-links (Join-Path $root 'dependencies/wheels') -r (Join-Path $root 'dependencies/requirements.lock')
    if($LASTEXITCODE -ne 0) {throw 'Hash-locked offline installation failed; evidence/environment retained, no automatic retry'}
    Assert-OmPython $root $pythonExe
    if($DevicePackSource) {
        $source=[IO.Path]::GetFullPath($DevicePackSource)
        Assert-OmDevicePack $source
        $destination=[IO.Path]::GetFullPath((Get-OmPackRoot))
        if(Test-Path -LiteralPath $destination) {throw 'Device-pack destination exists; refusing overwrite. Validate it with check.ps1.'}
        $null=New-Item -ItemType Directory -Path $destination
        foreach($entry in $spec.devicePackFiles.PSObject.Properties) {
            $target=Join-Path $destination $entry.Name
            $null=New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target)
            Copy-Item -LiteralPath (Join-Path $source $entry.Name) -Destination $target
        }
        Assert-OmDevicePack $destination
    }
    Write-Output 'SETUP COMPLETE. Run tools/check.ps1; J-Link/driver/device support remain separately checked.'
    exit 0
} catch {Write-Error $_.Exception.Message;exit 1}
