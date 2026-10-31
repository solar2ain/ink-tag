$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'internal/invoke-native.ps1') -Action 'validate' -ForwardArguments $args
exit $LASTEXITCODE
