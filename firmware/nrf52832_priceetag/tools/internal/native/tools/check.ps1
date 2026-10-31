[CmdletBinding()]
param([string]$JLinkExe='',[Alias('h')][switch]$Help,[Parameter(ValueFromRemainingArguments=$true)][object[]]$RemainingArguments)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'internal/runtime.ps1')
. (Join-Path (Get-NordicRuntimeRoot) 'tools/powershell-cli.ps1')
$guard=Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help -RemainingArguments $RemainingArguments -Summary 'Offline dependency/file/version check only; never opens J-Link, serial, BLE or network'
if($null -ne $guard){exit $guard}
try {
 if(!(Test-NordicToolBundle)){throw 'Run check from an exported bundle.'}
 Assert-NordicBundlePrerequisites 'jlink' $JLinkExe
 $lock=Get-Content (Join-Path $PSScriptRoot 'dependencies.lock.json') -Raw|ConvertFrom-Json
 $versions=Invoke-NordicPython (Join-Path (Get-NordicRuntimeRoot) 'tools/hardware/nordic_environment.py')
 if($LASTEXITCODE -ne 0){throw 'Dependency import/version check failed'}
 $actual=$versions|ConvertFrom-Json
 if($actual.python -cne $lock.pythonVersion){throw 'Python version mismatch'}
 foreach($entry in $lock.packages){if($actual.packages.($entry.name) -cne $entry.version){throw "Dependency version mismatch: $($entry.name)"}}
 $bundle=Get-Content (Join-Path $PSScriptRoot 'bundle.json') -Raw|ConvertFrom-Json
 if($bundle.firmware -or $bundle.firmwareVersion){throw 'This tools-only checker does not accept an embedded firmware binding.'}
 Write-Output 'CHECK OK: offline dependencies/files valid; tools-only, no firmware delivered, complete writer guarded. No device or release qualification implied.'
 exit 0
}catch{Write-Error ($_.Exception.Message+' Use tools/setup.ps1 and the official pinned J-Link installer; no device accessed.') -ErrorAction Continue;exit 1}
