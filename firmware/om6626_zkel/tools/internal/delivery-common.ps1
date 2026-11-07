$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

function Get-OmDeliveryRoot {
    $path=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
    if(!(Test-Path -LiteralPath (Join-Path $path 'bundle.json') -PathType Leaf)) {
        throw 'This entry requires an extracted OM tools bundle, not a source checkout.'
    }
    return $path
}

function Get-OmDependencySpec {
    Get-Content -LiteralPath (Join-Path $PSScriptRoot 'delivery-dependencies.json') -Raw | ConvertFrom-Json
}

function Assert-OmRegularFile([string]$Path,[string]$Sha256) {
    $item=Get-Item -LiteralPath $Path -ErrorAction Stop
    if($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {throw "Expected regular file: $Path"}
    if((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ine $Sha256) {throw "Pinned file differs: $Path"}
}

function Assert-OmBundleFiles([string]$Root) {
    $bundle=Get-Content -LiteralPath (Join-Path $Root 'bundle.json') -Raw|ConvertFrom-Json
    if($bundle.kind -ne 'tools-only' -or $bundle.firmwareIncluded -ne $false -or
       $null -ne $bundle.PSObject.Properties['defaultFirmware'] -or
       $null -ne $bundle.PSObject.Properties['firmwareVersion']) {throw 'Tools-only manifest required; firmware binding is forbidden'}
    foreach($entry in $bundle.files.PSObject.Properties) {
        $path=[IO.Path]::GetFullPath((Join-Path $Root $entry.Name))
        if(!$path.StartsWith($Root.TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {throw 'Bundle path escapes its root'}
        $parent=Split-Path -Parent $path
        while($parent.Length -ge $Root.Length) {
            if((Get-Item -LiteralPath $parent).Attributes -band [IO.FileAttributes]::ReparsePoint) {throw 'Bundle directory is a reparse point'}
            if($parent -ieq $Root) {break}
            $parent=Split-Path -Parent $parent
        }
        Assert-OmRegularFile $path $entry.Value
    }
    return $bundle
}

function Get-OmEnvironmentPython([string]$Root,[string]$Explicit='') {
    if($Explicit) {
        $resolved=(Get-Command $Explicit -CommandType Application -ErrorAction Stop|Select-Object -First 1).Source
        return $resolved
    }
    $python=Join-Path $Root 'environment/Scripts/python.exe'
    if(!(Test-Path -LiteralPath $python -PathType Leaf)) {throw 'Isolated Python is missing. Run tools/setup.ps1 -Python <Python-3.12.0-path>.'}
    return $python
}

function Assert-OmPython([string]$Root,[string]$Python,[string]$Firmware='',[string]$Config='') {
    $arguments=@('--root',$Root,'--check')
    if($Firmware) {$arguments+=@('--firmware',$Firmware)}
    if($Config) {$arguments+=@('--config',$Config)}
    & $Python -I (Join-Path $PSScriptRoot 'delivery-runtime.py') @arguments
    if($LASTEXITCODE -ne 0) {throw 'Python/package validation failed; run tools/setup.ps1 with the pinned interpreter.'}
}

function Get-OmNativeHome([string]$Explicit='') {
    if($Explicit) {return [IO.Path]::GetFullPath($Explicit)}
    if($env:INKTAG_JLINK_HOME) {return [IO.Path]::GetFullPath($env:INKTAG_JLINK_HOME)}
    return (Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'SEGGER/JLink_V788j')
}

function Assert-OmNative([string]$NativeDirectory) {
    $spec=Get-OmDependencySpec
    foreach($entry in $spec.jlinkFiles.PSObject.Properties) {Assert-OmRegularFile (Join-Path $NativeDirectory $entry.Name) $entry.Value}
    $version=[Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $NativeDirectory 'JLinkARM.dll')).FileVersion
    if($version -ne $spec.jlinkVersion) {throw "J-Link version $version is not $($spec.jlinkVersion)"}
    # File/version inspection only: NEVER run a vendor executable from check/setup.
}

function Get-OmPackRoot {
    if(!$env:APPDATA) {throw 'APPDATA missing; Windows user device-pack path unavailable'}
    return (Join-Path $env:APPDATA 'SEGGER/JLinkDevices/InkTag/OM6626')
}

function Assert-OmDevicePack([string]$Path) {
    $spec=Get-OmDependencySpec
    foreach($entry in $spec.devicePackFiles.PSObject.Properties) {Assert-OmRegularFile (Join-Path $Path $entry.Name) $entry.Value}
}
