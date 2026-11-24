[CmdletBinding()]
param(
    [string]$Listen = "127.0.0.1:8787",
    [string]$TlsCert,
    [string]$TlsKey,
    [string]$DataDir,
    [string]$WebRoot,
    [string]$FindService,
    [string]$FindDataDir,
    [switch]$FindNoInstall,
    [switch]$NoOpen,
    [Alias("h")][switch]$Help
)

$ErrorActionPreference = "Stop"
$serverRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

if ($Help) {
    $script:extraArguments = @("--help")
} else {
    $script:extraArguments = @("--listen", $Listen)
    if ($TlsCert) { $script:extraArguments += @("--tls-cert", $TlsCert) }
    if ($TlsKey) { $script:extraArguments += @("--tls-key", $TlsKey) }
    if ($DataDir) { $script:extraArguments += @("--data-dir", $DataDir) }
    if ($WebRoot) { $script:extraArguments += @("--web-root", $WebRoot) }
    if ($FindService) { $script:extraArguments += @("--find-service", $FindService) }
    if ($FindDataDir) { $script:extraArguments += @("--find-data-dir", $FindDataDir) }
    if ($FindNoInstall) { $script:extraArguments += "--find-no-install" }
    if ($NoOpen) { $script:extraArguments += "--no-open" }
}

$python = Get-Command python -ErrorAction SilentlyContinue
if ($python) {
    & $python.Source (Join-Path $serverRoot "start.py") @script:extraArguments
    exit $LASTEXITCODE
}

$py = Get-Command py -ErrorAction SilentlyContinue
if ($py) {
    & $py.Source -3 (Join-Path $serverRoot "start.py") @script:extraArguments
    exit $LASTEXITCODE
}

throw "Python 3.11 or newer was not found. Install Python, then run this script again."
