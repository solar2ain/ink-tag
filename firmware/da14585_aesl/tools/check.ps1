[CmdletBinding()]
param([string]$JLinkRoot='C:/Program Files/SEGGER/JLink_V788j',
 [Alias('h')][switch]$Help)
$ErrorActionPreference='Stop'
if($Help){Write-Host 'check.ps1 [-JLinkRoot <7.88j>] : offline hashes/ABI only; never runs native probe tools or installs dependencies.';exit 0}
. (Join-Path $PSScriptRoot 'internal/context.ps1')
$context=Get-InkTagDaToolContext -ToolsRoot $PSScriptRoot
Assert-InkTagDaBundledFiles $context
$lockPath=Join-Path $PSScriptRoot 'dependencies.lock.json'
$lockHash=(Get-FileHash -LiteralPath $lockPath).Hash.ToLowerInvariant()
$runtime=Join-Path $PSScriptRoot ('.runtime/py313-'+$lockHash.Substring(0,16))
$python=Join-Path $runtime 'python.exe'
if(!(Test-Path -LiteralPath $python -PathType Leaf)){throw 'Pinned local runtime missing; run setup.ps1 first.'}
& $python -I -B (Join-Path $PSScriptRoot 'internal/dependency-runtime.py') check --lock $lockPath --runtime $runtime
if($global:LASTEXITCODE -ne 0){throw 'Runtime missing/wrong/modified; no debugger was opened.'}
$vendor=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'vendor-dependencies.json') -Raw|ConvertFrom-Json
foreach($entry in $vendor.segger.files){
 $path=Join-Path $JLinkRoot $entry.file
 if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -ne $entry.bytes -or (Get-FileHash -LiteralPath $path).Hash -ne $entry.sha256){throw "Missing/wrong pinned manual SEGGER dependency: $($entry.file)"}
}
$fullHelper=Resolve-InkTagDaToolFile $context '_build/dialog/da14585_guarded_helper/InkTag-da14585-guarded-helper-0f183172ce2d.bin'
if((Get-Item -LiteralPath $fullHelper).Length -ne $vendor.guardedFullHelper.bytes -or (Get-FileHash -LiteralPath $fullHelper).Hash -ne $vendor.guardedFullHelper.sha256){throw 'Wrong fixed external Full helper; no target operation.'}
Write-Host 'Offline dependency checks passed; native tools were not executed and no device was accessed. Full writer remains guarded.'
