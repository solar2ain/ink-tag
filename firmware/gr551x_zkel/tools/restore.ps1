$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'internal/invoke-native.ps1') -Action 'restore' -ForwardArguments $args
exit $LASTEXITCODE
