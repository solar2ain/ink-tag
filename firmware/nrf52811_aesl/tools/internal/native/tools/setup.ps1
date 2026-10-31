[CmdletBinding()]
param([string]$PythonExe='python', [string]$JLinkExe='', [Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments=$true)][object[]]$RemainingArguments)
$ErrorActionPreference='Stop'
try {
    . (Join-Path $PSScriptRoot 'internal/runtime-context.ps1')
    $context=Get-InkTagRuntimeContext $PSScriptRoot
    . $context.CliHelper
    $guard=Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help -RemainingArguments $RemainingArguments -Summary 'Prepare an isolated offline Python environment; no hardware or system-policy changes'
    if($null -ne $guard){exit $guard}
    if($context.IsBundle -and (Get-Content (Join-Path $context.Root 'bundle.json') -Raw|ConvertFrom-Json).runtimeKind -eq 'portable'){
        if($PythonExe -and $PythonExe -ne 'python'){throw 'Portable tools use their verified portable Python only'}
        & (Join-Path $PSScriptRoot 'internal/setup-full-runtime.ps1') -ToolsDirectory $PSScriptRoot
        exit $LASTEXITCODE
    }
    $arguments=@('-I','-B',(Join-Path $PSScriptRoot 'internal/package-environment.py'),'--tools',$PSScriptRoot,'setup')
    if($JLinkExe){$arguments+=@('--jlink-exe',$JLinkExe)}
    & $PythonExe @arguments
    exit $LASTEXITCODE
} catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }
