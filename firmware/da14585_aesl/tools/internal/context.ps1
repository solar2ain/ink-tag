# Importing resolves host paths only; no debugger/library/native process is opened.
# A PS5.1 child may inherit PS7's module search path. Resolve this built-in
# module from this shell, without changing PSModulePath or execution policy.
if (!(Get-Command Get-FileHash -ErrorAction SilentlyContinue)) {
    Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
}
function Get-InkTagDaToolContext {
    param([Parameter(Mandatory)][string]$ToolsRoot)
    $tools = [IO.Path]::GetFullPath($ToolsRoot).TrimEnd('\','/')
    $marker = Join-Path $tools 'internal/portable-files.json'
    if (Test-Path -LiteralPath $marker -PathType Leaf) {
        $map = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
        if ($map.format -ne 1 -or $map.chip -ne 'DA14585') { throw 'Unknown portable tools map.' }
        return [pscustomobject]@{Root=(Split-Path -Parent $tools);Tools=$tools;Portable=$true;Map=$map.files;ExternalMap=$map.externalFiles}
    }
    $root = [IO.Path]::GetFullPath((Join-Path $tools '../../../../..'))
    $expected = [IO.Path]::GetFullPath((Join-Path $root 'firmware/ports/dialog/da14585/tools'))
    if ($tools -ne $expected -or !(Test-Path -LiteralPath (Join-Path $root 'tools/powershell-cli.ps1'))) {
        throw 'Tool context unavailable; no SDK/check-out search or target operation.'
    }
    return [pscustomobject]@{Root=$root;Tools=$tools;Portable=$false;Map=$null;ExternalMap=$null}
}
function Assert-InkTagDaBundledFiles {
    param([Parameter(Mandatory)]$Context)
    if (!$Context.Portable) { return }
    $manifestPath=Join-Path $Context.Root 'tool-manifest.json'
    if (!(Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'Tool manifest missing; no target operation.' }
    $manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
    if ($manifest.format -ne 1 -or $manifest.kind -ne 'tools-only' -or $manifest.firmwareIncluded -isnot [bool] -or $manifest.firmwareIncluded -or $manifest.configuredWriterQualified -isnot [bool] -or $manifest.configuredWriterQualified -or !$manifest.files) { throw 'Unsupported portable tool manifest; no target operation.' }
    foreach($mapping in $Context.Map.PSObject.Properties){if(!$manifest.files.PSObject.Properties[[string]$mapping.Value]){throw 'Portable map is not bound to the tool manifest.'}}
    foreach($entry in $manifest.files.PSObject.Properties){
        $name=[string]$entry.Name
        if([IO.Path]::IsPathRooted($name) -or $name.Contains('\') -or $name.Contains(':') -or ($name -split '/') -contains '..'){throw 'Unsafe bundled path; no target operation.'}
        $path=[IO.Path]::GetFullPath((Join-Path $Context.Root $name))
        $prefix=[IO.Path]::GetFullPath($Context.Root).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
        if(!$path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $path -PathType Leaf)){throw "Bundled dependency missing: $name"}
        $ancestor=$path
        while($ancestor){if((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked bundled dependency refused.'};$parent=Split-Path -Parent $ancestor;if(!$parent -or $parent -eq $ancestor){break};$ancestor=$parent}
        if((Get-Item -LiteralPath $path).Length -ne $entry.Value.bytes -or (Get-FileHash -LiteralPath $path).Hash -ne $entry.Value.sha256){throw "Bundled dependency changed: $name"}
    }
}
function Resolve-InkTagDaToolFile {
    param([Parameter(Mandatory)]$Context,[Parameter(Mandatory)][string]$Relative)
    if ($Context.Portable) {
        $property = $Context.Map.PSObject.Properties[$Relative.Replace('\','/')]
        if (!$property -and $Context.ExternalMap) { $property=$Context.ExternalMap.PSObject.Properties[$Relative.Replace('\','/')] }
        if (!$property) { throw "File not in the exported allowlist: $Relative" }
        $relativePath = [string]$property.Value
    } else { $relativePath = $Relative }
    if ([IO.Path]::IsPathRooted($relativePath) -or ($relativePath -split '[\\/]') -contains '..') {
        throw 'Tool path escaped its package.'
    }
    $path = [IO.Path]::GetFullPath((Join-Path $Context.Root $relativePath))
    $prefix = [IO.Path]::GetFullPath($Context.Root).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    if (!$path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required tool file missing: $Relative"
    }
    return $path
}
function Resolve-InkTagDaPython {
    param([Parameter(Mandatory)]$Context,[string]$PythonExe='python')
    if (!$Context.Portable) { return (Get-Command $PythonExe -ErrorAction Stop | Select-Object -First 1).Source }
    Assert-InkTagDaBundledFiles $Context
    $lock = Join-Path $Context.Tools 'dependencies.lock.json'
    $digest = (Get-FileHash -LiteralPath $lock -Algorithm SHA256).Hash.ToLowerInvariant()
    $runtime = Join-Path $Context.Tools ('.runtime/py313-'+$digest.Substring(0,16)+'/python.exe')
    if ($PythonExe -ne 'python' -and [IO.Path]::GetFullPath($PythonExe) -ne [IO.Path]::GetFullPath($runtime)) { throw 'Portable tools use only their prepared pinned runtime.' }
    if (!(Test-Path -LiteralPath $runtime -PathType Leaf)) { throw 'Pinned runtime missing; run tools/setup.ps1 first.' }
    return $runtime
}
function Assert-InkTagDaToolEnvironment {
    param([Parameter(Mandatory)]$Context,[Parameter(Mandatory)][string]$PythonExe,
          [ValidateRange(1,300)][int]$OperationTimeoutSeconds=60)
    if (!$Context.Portable) { return }
    if(!(Get-Command Invoke-InkTagDaLoggedProcess -ErrorAction SilentlyContinue)){
        . (Join-Path $Context.Tools 'internal/host-common.ps1')
    }
    $check=Invoke-InkTagDaLoggedProcess -Exe $PythonExe -Arguments @('-I','-B',(Join-Path $Context.Tools 'internal/dependency-runtime.py'),'check','--lock',(Join-Path $Context.Tools 'dependencies.lock.json'),'--runtime',(Split-Path -Parent $PythonExe)) -OperationTimeoutSeconds $OperationTimeoutSeconds -Label 'Check local runtime'
    if ($check.ExitCode -ne 0) { throw "Pinned runtime integrity/ABI check failed; no target operation. $($check.Text)" }
}
function Resolve-InkTagDaVendorFile {
    param([Parameter(Mandatory)]$Context,[Parameter(Mandatory)][string]$Name,[string]$ExplicitPath='')
    $vendor = Get-Content -LiteralPath (Join-Path $Context.Tools 'vendor-dependencies.json') -Raw | ConvertFrom-Json
    $entry = @($vendor.segger.files | Where-Object file -eq $Name)
    if ($entry.Count -ne 1) { throw 'Unknown manual vendor file.' }
    $root = if ($env:INKTAG_JLINK_HOME) { $env:INKTAG_JLINK_HOME } else { 'C:/Program Files/SEGGER/JLink_V788j' }
    $path = if ($ExplicitPath) { [IO.Path]::GetFullPath($ExplicitPath) } else { Join-Path $root $Name }
    if (!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -ne $entry[0].bytes -or
        (Get-FileHash -LiteralPath $path).Hash -ne $entry[0].sha256) { throw "Missing/wrong pinned manual vendor file: $Name" }
    return $path
}
function Assert-InkTagDaExternalFlashSupport {
    param([Parameter(Mandatory)]$Context,[Parameter(Mandatory)][string]$XmlPath)
    $vendor = Get-Content -LiteralPath (Join-Path $Context.Tools 'vendor-dependencies.json') -Raw | ConvertFrom-Json
    if(!$vendor.PSObject.Properties['externalFlashSupport']){throw 'DA backup support is retired; no target accessed.'}
    foreach ($entry in $vendor.externalFlashSupport.files) {
        $path = if ($entry.file -eq 'JLinkDevices.xml') { $XmlPath } else { Join-Path (Split-Path -Parent $XmlPath) $entry.file }
        if (!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -ne $entry.bytes -or
            (Get-FileHash -LiteralPath $path).Hash -ne $entry.sha256) { throw "Missing/wrong manual Flash support: $($entry.file)" }
    }
}
