$ErrorActionPreference = 'Stop'
$installer = Join-Path $PSScriptRoot 'install_inktag.cmd'
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw 'Incomplete InkTag package: install_inktag.cmd is missing.'
}
& $installer @args
exit $LASTEXITCODE
