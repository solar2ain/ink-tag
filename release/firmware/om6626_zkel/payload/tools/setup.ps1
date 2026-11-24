[CmdletBinding()]
param([switch]$NativeInstalled,[Alias('h')][switch]$Help)
$ErrorActionPreference='Stop'
if($Help){
 Write-Output 'setup.ps1 [-NativeInstalled]: validate this native tools-only directory; no Python/pip/venv installation.'
 Write-Output 'Manually supply licensed SEGGER J-Link/J-Flash 7.88j and the hash-bound OM device pack listed in ../records/dependencies.json.'
 Write-Output 'No download, SDK lookup, global ExecutionPolicy change, security setting, service or device operation.'
 exit 0
}
& (Join-Path $PSScriptRoot 'check.ps1') -NativeInstalled:$NativeInstalled
exit $LASTEXITCODE
