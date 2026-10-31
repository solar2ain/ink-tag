$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'internal/invoke-native.ps1') -Action 'check' -ForwardArguments $args
exit $LASTEXITCODE
