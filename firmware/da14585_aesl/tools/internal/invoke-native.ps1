param([string]$Action, [object[]]$ForwardArguments)
$ErrorActionPreference = 'Stop'
function Get-InkTagContentHash([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return [System.Convert]::ToBase64String($sha.ComputeHash($stream)) }
    finally { $stream.Dispose(); $sha.Dispose() }
}
$layout = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'layout.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$entry = $layout.entries.PSObject.Properties[$Action]
if (-not $entry) { throw "This package does not provide '$Action'." }
$nativeRoot = Join-Path $PSScriptRoot 'native'
$native = Join-Path $nativeRoot ([string]$entry.Value)
$forward = @($ForwardArguments)
for ($i = 0; $i -lt ($forward.Count - 1); $i++) {
    if ([string]$forward[$i] -ieq '-Config') {
        $given = [string]$forward[$i + 1]
        if (Test-Path -LiteralPath $given -PathType Leaf) {
            $resolved = (Resolve-Path -LiteralPath $given).Path
            foreach ($mapping in $layout.configs) {
                $alias = Join-Path (Split-Path $PSScriptRoot -Parent) ('configs\' + $mapping.name)
                if ($resolved -ieq $alias) {
                    $original = Join-Path $nativeRoot $mapping.original
                    if ((Get-InkTagContentHash $resolved) -eq (Get-InkTagContentHash $original)) {
                        $forward[$i + 1] = $original
                    }
                    break
                }
            }
        }
    }
}
& $native @forward
exit $LASTEXITCODE
