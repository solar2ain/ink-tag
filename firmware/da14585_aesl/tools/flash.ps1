$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'internal/invoke-native.ps1') -Action 'flash' -ForwardArguments $args
exit $LASTEXITCODE
