$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'internal/invoke-native.ps1') -Action 'probe' -ForwardArguments $args
exit $LASTEXITCODE
