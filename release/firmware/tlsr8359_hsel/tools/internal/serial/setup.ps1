[CmdletBinding()]
param([string]$Python='python', [Alias('h')][switch]$Help)
$ErrorActionPreference='Stop'
if ($Help) {
    Write-Output 'setup.bat [-Python <python.exe>] installs and checks the TLSR serial-tool dependencies.'
    Write-Output 'Double-click to keep the result on screen. With arguments it returns the exit code.'
    exit 0
}
$toolsDirectory = $PSScriptRoot
& $Python -m pip install -r (Join-Path $toolsDirectory 'requirements-tools.txt')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Python (Join-Path $toolsDirectory '../python/check.py')
exit $LASTEXITCODE
