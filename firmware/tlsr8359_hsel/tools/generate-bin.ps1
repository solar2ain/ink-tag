$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'internal/invoke-native.ps1') -Action 'generate-bin' -ForwardArguments $args
exit $LASTEXITCODE
