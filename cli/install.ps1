param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$venv = Join-Path $PSScriptRoot '.venv'
if (!(Test-Path -LiteralPath (Join-Path $venv 'Scripts/python.exe'))) {
    & $Python -m venv $venv
    if ($LASTEXITCODE -ne 0) { throw 'Python 3.11+ with venv is required.' }
}
& (Join-Path $venv 'Scripts/python.exe') -m pip install $PSScriptRoot
if ($LASTEXITCODE -ne 0) { throw 'CLI dependency installation failed.' }
Write-Host 'Installed. Run .\inktag.ps1 --help from this directory.'
