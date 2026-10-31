# Export launcher; unchanged native backend lives in internal/runtime.
& (Join-Path $PSScriptRoot 'internal/delivery-entry.ps1') -Action 'flash' -ForwardArguments $args
exit $LASTEXITCODE
