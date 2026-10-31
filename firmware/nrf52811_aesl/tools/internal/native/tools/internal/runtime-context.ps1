# Host-only source/bundle resolution. Never opens a device or searches ancestors.
function Get-InkTagRuntimeContext {
    param([Parameter(Mandatory=$true)][string]$ToolsDirectory)
    $tools = [IO.Path]::GetFullPath($ToolsDirectory)
    $package = Split-Path -Parent $tools
    $marker = Join-Path $package 'bundle.json'
    $shared = Join-Path $tools 'internal/shared'
    $bundled = (Test-Path -LiteralPath $marker) -or (Test-Path -LiteralPath $shared)
    if ($bundled) {
        if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) { throw 'Incomplete tool bundle: bundle.json missing' }
        $record = Get-Content -LiteralPath $marker -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($record.schemaVersion -ne 1 -or $record.chip -ne 'nrf52811' -or
            $record.firmwareFamily -ne 'aesl22_nrf' -or $record.artifactKind -ne 'tools-only' -or
            $record.fullWriter -ne 'native-v1' -or $record.runtimeKind -ne 'portable' -or
            $record.firmwareIncluded -ne $false) { throw 'Wrong or unsupported tool bundle identity' }
        return [pscustomobject]@{ IsBundle=$true; Root=$package; Tools=$tools;
            CliHelper=(Join-Path $shared 'powershell-cli.ps1'); JLinkHelper=(Join-Path $shared 'jlink-tools.ps1') }
    }
    $repo = [IO.Path]::GetFullPath((Join-Path $tools '../../../../../'))
    if (-not (Test-Path -LiteralPath (Join-Path $repo '.git'))) { throw 'Neither an intact tool bundle nor this source checkout was found' }
    return [pscustomobject]@{ IsBundle=$false; Root=$repo; Tools=$tools;
        CliHelper=(Join-Path $repo 'tools/powershell-cli.ps1'); JLinkHelper=(Join-Path $repo 'tools/jlink-tools.ps1') }
}

function Assert-InkTagBundleEnvironment {
    param($Context, [string]$PythonExe='python', [string]$JLinkExe='')
    if (-not $Context.IsBundle) { return [pscustomobject]@{ PythonExe=$PythonExe; JLinkExe=$JLinkExe } }
    if ((Get-Content (Join-Path $Context.Root 'bundle.json') -Raw|ConvertFrom-Json).runtimeKind -eq 'portable') {
        $bundledPython=Join-Path $Context.Tools '.runtime/python/python.exe'
        if ($PythonExe -and $PythonExe -ne 'python' -and [IO.Path]::GetFullPath($PythonExe) -ne [IO.Path]::GetFullPath($bundledPython)) { throw 'Portable tools use their verified portable Python only' }
        $PythonExe=$bundledPython
        . (Join-Path $Context.Tools 'internal/full-bundle-guard.ps1')
        Assert-InkTagCompleteBundleFiles -ToolsDirectory $Context.Tools
        $checked = & $PythonExe -I -B (Join-Path $Context.Tools 'internal/full-runtime.py') check --tools $Context.Tools
        if ($LASTEXITCODE -ne 0) { throw 'Complete bundle runtime check failed; no probe opened' }
        return [pscustomobject]@{PythonExe=$PythonExe;JLinkExe=$JLinkExe}
    }
    if (-not $PythonExe -or $PythonExe -eq 'python') { $PythonExe = Join-Path $Context.Tools '.venv/Scripts/python.exe' }
    if (-not (Test-Path -LiteralPath $PythonExe -PathType Leaf)) { throw 'Run powershell -NoProfile -ExecutionPolicy Bypass -File tools/setup.ps1 first' }
    $arguments = @('-I', '-B', (Join-Path $Context.Tools 'internal/package-environment.py'), '--tools', $Context.Tools, 'check')
    if ($JLinkExe) { $arguments += @('--jlink-exe', $JLinkExe) }
    $lines = & $PythonExe @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Bundle dependency/integrity check failed; no hardware opened' }
    $checked = ($lines -join "`n") | ConvertFrom-Json
    $env:PYTHONNOUSERSITE = '1'
    $env:PYTHONDONTWRITEBYTECODE = '1'
    $env:PYTHONPATH = ''
    return [pscustomobject]@{ PythonExe=$PythonExe; JLinkExe=[string]$checked.jlinkExe }
}
