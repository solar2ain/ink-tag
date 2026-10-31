# Source and exported tool paths only. No device operation or global environment change.
if (!(Get-Command Get-FileHash -ErrorAction SilentlyContinue)) {
    Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
}
$script:NordicToolDirectory = Split-Path -Parent $PSScriptRoot
if (Test-Path -LiteralPath (Join-Path $script:NordicToolDirectory 'bundle.json')) {
    $version=$PSVersionTable.PSVersion
    if ($env:OS -ne 'Windows_NT' -or ![Environment]::Is64BitProcess -or !(($version.Major -eq 5 -and $version.Minor -eq 1) -or $version.Major -eq 7)) {
        throw 'This bundle requires Windows x64 with PowerShell5.1 or7; no hardware accessed.'
    }
}
function Test-NordicToolBundle { return Test-Path -LiteralPath (Join-Path $script:NordicToolDirectory 'bundle.json') }
function Get-NordicRuntimeRoot {
    if (Test-NordicToolBundle) { return Join-Path $script:NordicToolDirectory 'internal/offline' }
    return [IO.Path]::GetFullPath((Join-Path $script:NordicToolDirectory '../../../../../'))
}
function Get-NordicBundlePath([string]$Relative) {
    $root = [IO.Path]::GetFullPath((Split-Path -Parent $script:NordicToolDirectory))
    if ([IO.Path]::IsPathRooted($Relative)) { throw 'Bundle path must be relative' }
    $path = [IO.Path]::GetFullPath((Join-Path $root $Relative))
    if (!$path.StartsWith($root + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Bundle path escapes root' }
    $cursor=$path
    while ($cursor -and $cursor.Length -ge $root.Length) {
        if ((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Linked bundle path refused' }
        $cursor=[IO.Path]::GetDirectoryName($cursor)
    }
    return $path
}
function Assert-NordicRuntimeFiles {
    $manifest=Get-Content -LiteralPath (Join-Path $script:NordicToolDirectory 'internal/runtime-files.json') -Raw|ConvertFrom-Json
    foreach($entry in $manifest) {
        $path=Get-NordicBundlePath ('.runtime/' + $entry.path)
        if (!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -ne $entry.bytes -or (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -cne $entry.sha256) { throw 'Portable Python is missing/changed. Run tools/setup.ps1; no device accessed.' }
    }
}
function Invoke-NordicPython {
    if(Test-NordicToolBundle) {
        Assert-NordicRuntimeFiles
        $exe=Get-NordicBundlePath '.runtime/python.exe'
    } else { $exe=(Get-Command python -CommandType Application -ErrorAction Stop|Select-Object -First 1).Source }
    & $exe -B @args
    # Native processes update global:LASTEXITCODE. A caller script may already
    # have its own LASTEXITCODE from this helper's previous call; reading the
    # unqualified name here then returns that stale value (e.g. 0 after a
    # failed readback validator). Always copy the actual native exit result.
    $result=$global:LASTEXITCODE
    Set-Variable -Name LASTEXITCODE -Value $result -Scope 1
}
function Assert-NordicBundleFiles {
    foreach($entry in (Get-Content (Join-Path $script:NordicToolDirectory 'internal/package-files.json') -Raw|ConvertFrom-Json)) {
        $path=Get-NordicBundlePath $entry.path
        if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -ne $entry.bytes -or (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -cne $entry.sha256){throw 'Tool/package file missing or changed; re-extract the verified original bundle.'}
    }
}
function Assert-NordicBundlePrerequisites([string]$Programmer,[string]$JLinkExe) {
    if(!(Test-NordicToolBundle)){return}
    if($Programmer -ne 'jlink'){throw 'This Windows bundle qualifies J-Link only; DAPLink dependencies/hardware are not bundled.'}
    Assert-NordicBundleFiles
    Assert-NordicRuntimeFiles
    . (Join-Path (Get-NordicRuntimeRoot) 'tools/jlink-tools.ps1')
    $exe=Resolve-InkTagJLinkTool -ToolName 'JLink.exe' -ExplicitPath $JLinkExe
    $lock=Get-Content (Join-Path $script:NordicToolDirectory 'dependencies.lock.json') -Raw|ConvertFrom-Json
    foreach($entry in $lock.native.files) {
        $path=Join-Path (Split-Path -Parent $exe) $entry.name
        if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -cne $entry.sha256){throw 'Official J-Link 7.88j dependency mismatch; install the pinned pack or pass its exact JLinkExe. No device accessed.'}
    }
}
