# Export launcher; unchanged native backend lives in internal/runtime.
& (Join-Path $PSScriptRoot 'internal/delivery-entry.ps1') -Action 'backup' -ForwardArguments $args
exit $LASTEXITCODE
