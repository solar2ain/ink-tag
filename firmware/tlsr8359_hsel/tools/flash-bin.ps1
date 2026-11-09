$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'internal/invoke-native.ps1') -Action 'flash-bin' -ForwardArguments $args
exit $LASTEXITCODE
