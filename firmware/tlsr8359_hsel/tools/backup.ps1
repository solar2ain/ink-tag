$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'internal/invoke-native.ps1') -Action 'backup' -ForwardArguments $args
exit $LASTEXITCODE
