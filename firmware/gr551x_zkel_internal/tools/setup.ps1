$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'internal/invoke-native.ps1') -Action 'setup' -ForwardArguments $args
exit $LASTEXITCODE
