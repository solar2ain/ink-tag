[CmdletBinding()]
param([string]$Dependencies='', [Alias('h')][switch]$Help)
$ErrorActionPreference='Stop'
if($Help){Write-Host 'setup.ps1 [-Dependencies <offline archives>] : prepare pinned tool-local Python only; no network, driver, service, probe or target access.';exit 0}
. (Join-Path $PSScriptRoot 'internal/context.ps1')
$context=Get-InkTagDaToolContext -ToolsRoot $PSScriptRoot
Assert-InkTagDaBundledFiles $context
$lockPath=Join-Path $PSScriptRoot 'dependencies.lock.json'
$lock=Get-Content -LiteralPath $lockPath -Raw|ConvertFrom-Json
if($lock.status -ne 'locked' -or !$lock.closureReviewed){throw 'Dependency closure is not reviewed/locked.'}
if([Environment]::OSVersion.Platform -ne 'Win32NT' -or ![Environment]::Is64BitProcess){throw 'Setup requires 64-bit Windows PowerShell.'}
if(!$Dependencies){$Dependencies=Join-Path $context.Root 'dependencies'}
$Dependencies=[IO.Path]::GetFullPath($Dependencies)
$archive=Join-Path $Dependencies $lock.runtime.file
if(!(Test-Path -LiteralPath $archive -PathType Leaf) -or (Get-Item -LiteralPath $archive).Length -ne $lock.runtime.bytes -or (Get-FileHash -LiteralPath $archive).Hash -ne $lock.runtime.sha256){throw 'Pinned Python archive missing/wrong; no download or fallback.'}
$lockHash=(Get-FileHash -LiteralPath $lockPath).Hash.ToLowerInvariant()
$base=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '.runtime')).TrimEnd('\','/')
$final=Join-Path $base ('py313-'+$lockHash.Substring(0,16))
if(Test-Path -LiteralPath $final){throw 'Runtime already exists; run check.ps1, do not overwrite it.'}
$stage=Join-Path $base ('prepare-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
foreach($path in @($base,$final,$stage)){
 $full=[IO.Path]::GetFullPath($path)
 if($full -ne $base -and !$full.StartsWith($base+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Runtime target escaped package.'}
 $ancestor=$full
 while($ancestor){if((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Runtime path contains link.'};$parent=Split-Path -Parent $ancestor;if(!$parent -or $parent -eq $ancestor){break};$ancestor=$parent}
}
New-Item -ItemType Directory -Path $stage|Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::OpenRead($archive)
try{
 $seen=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
 foreach($entry in $zip.Entries){
  if([IO.Path]::IsPathRooted($entry.FullName) -or $entry.FullName.Contains(':') -or $entry.FullName.Contains('\') -or ($entry.FullName -split '/') -contains '..' -or !$seen.Add($entry.FullName) -or (($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000){throw 'Unsafe Python archive entry.'}
 }
}finally{$zip.Dispose()}
[IO.Compression.ZipFile]::ExtractToDirectory($archive,$stage)
# Local isolated search path only: no site.main/user-site/PYTHONPATH/registry.
Set-Content -LiteralPath (Join-Path $stage 'python313._pth') -Value "python313.zip`n.`nsite`n" -Encoding ASCII
$python=Join-Path $stage 'python.exe'
$helper=Join-Path $PSScriptRoot 'internal/dependency-runtime.py'
& $python -I -B $helper setup --lock $lockPath --runtime $stage --artifacts $Dependencies
if($global:LASTEXITCODE -ne 0){throw 'Runtime preparation failed; partial directory retained, not selected.'}
# Both absolute paths were checked inside this explicit package .runtime root.
Move-Item -LiteralPath $stage -Destination $final
Write-Host 'Pinned local runtime prepared. No vendor installation or device access performed.'
