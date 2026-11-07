[CmdletBinding()]
param([string]$PythonExe='', [string]$JLinkExe='', [Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments=$true)][object[]]$RemainingArguments)
$ErrorActionPreference='Stop'
try {
    . (Join-Path $PSScriptRoot 'internal/runtime-context.ps1')
    $context=Get-InkTagRuntimeContext $PSScriptRoot
    . $context.CliHelper
    $guard=Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help -RemainingArguments $RemainingArguments -Summary 'Offline file/runtime/version check; never starts J-Link, BLE or a service'
    if($null -ne $guard){exit $guard}
    if($context.IsBundle -and (Get-Content (Join-Path $context.Root 'bundle.json') -Raw|ConvertFrom-Json).runtimeKind -eq 'portable'){
        . (Join-Path $PSScriptRoot 'internal/full-bundle-guard.ps1')
        Assert-InkTagCompleteBundleFiles -ToolsDirectory $PSScriptRoot
        $bundledPython=Join-Path $PSScriptRoot '.runtime/python/python.exe'
        if($PythonExe -and [IO.Path]::GetFullPath($PythonExe) -ne [IO.Path]::GetFullPath($bundledPython)){throw 'Portable tools use their verified portable Python only'}
        if($JLinkExe){
            if([IO.Path]::GetFileName($JLinkExe) -ine 'JLink.exe'){throw 'The executable must be the pinned JLink.exe'}
            $lock=Get-Content (Join-Path $PSScriptRoot 'full-dependencies.lock.json') -Raw|ConvertFrom-Json
            foreach($row in $lock.native.files){
                $native=Join-Path (Split-Path -Parent $JLinkExe) $row.name
                if(!(Test-Path -LiteralPath $native -PathType Leaf)){throw 'J-Link dependency missing'}
                if((Get-FileHash -LiteralPath $native).Hash.ToLowerInvariant() -cne $row.sha256){throw 'J-Link dependency hash mismatch'}
            }
        }
        $PythonExe=$bundledPython
        & $PythonExe -I -B (Join-Path $PSScriptRoot 'internal/full-runtime.py') check --tools $PSScriptRoot
        exit $LASTEXITCODE
    }
    if(-not $PythonExe){$PythonExe=Join-Path $PSScriptRoot '.venv/Scripts/python.exe'}
    if(-not(Test-Path -LiteralPath $PythonExe -PathType Leaf)){throw 'Isolated Python missing. Run powershell -NoProfile -ExecutionPolicy Bypass -File tools/setup.ps1'}
    $arguments=@('-I','-B',(Join-Path $PSScriptRoot 'internal/package-environment.py'),'--tools',$PSScriptRoot,'check')
    if($JLinkExe){$arguments+=@('--jlink-exe',$JLinkExe)}
    & $PythonExe @arguments
    exit $LASTEXITCODE
} catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }
