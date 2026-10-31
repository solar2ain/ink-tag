[CmdletBinding()]
param([Alias('h')][switch]$Help,[Parameter(ValueFromRemainingArguments=$true)][object[]]$RemainingArguments)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'internal/runtime.ps1')
. (Join-Path (Get-NordicRuntimeRoot) 'tools/powershell-cli.ps1')
$guard=Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help -RemainingArguments $RemainingArguments -Summary 'Prepare bundled pinned portable dependencies offline; no device, network, global install or security-policy change'
if($null -ne $guard){exit $guard}
try {
 if(!(Test-NordicToolBundle)){throw 'Run setup from the exported tools-only directory, not the development source.'}
 $target=Get-NordicBundlePath '.runtime'
 if(Test-Path -LiteralPath $target){Assert-NordicRuntimeFiles; Write-Output 'Portable dependencies already match; no overwrite.';exit 0}
 $lock=Get-Content (Join-Path $PSScriptRoot 'dependencies.lock.json') -Raw|ConvertFrom-Json
 foreach($entry in $lock.archives){
  $path=Get-NordicBundlePath ('dependencies/'+$entry.file)
  if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -cne $entry.sha256){throw 'Dependency archive missing/changed. Re-extract the original verified bundle; no download or device access.'}
 }
 Add-Type -AssemblyName System.IO.Compression.FileSystem
 New-Item -ItemType Directory -Path $target|Out-Null
 foreach($entry in $lock.archives){
  $path=Get-NordicBundlePath ('dependencies/'+$entry.file)
  $output=if($entry.kind -eq 'python'){$target}else{Join-Path $target 'Lib/site-packages'}
  if(!(Test-Path $output)){New-Item -ItemType Directory -Path $output -Force|Out-Null}
  $archive=[IO.Compression.ZipFile]::OpenRead($path)
  try {foreach($member in $archive.Entries){
   $candidate=[IO.Path]::GetFullPath((Join-Path $output $member.FullName))
   if(!$candidate.StartsWith([IO.Path]::GetFullPath($output)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Archive path escapes runtime'}
  }}finally{$archive.Dispose()}
  [IO.Compression.ZipFile]::ExtractToDirectory($path,$output)
 }
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'internal/python313._pth') -Destination (Join-Path $target 'python313._pth') -Force
 Assert-NordicRuntimeFiles
 Invoke-NordicPython (Join-Path (Get-NordicRuntimeRoot) 'tools/hardware/nordic_environment.py')
 if($LASTEXITCODE -ne 0){throw 'Portable dependency import failed; partial runtime retained, not accepted.'}
 exit 0
}catch{Write-Error $_.Exception.Message -ErrorAction Continue;exit 1}
