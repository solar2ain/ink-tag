[CmdletBinding()]
param([string]$Python='python')
$ErrorActionPreference='Stop'
& $Python -m pip install -r (Join-Path $PSScriptRoot 'requirements-tools.txt')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Python (Join-Path $PSScriptRoot 'check.py')
exit $LASTEXITCODE
