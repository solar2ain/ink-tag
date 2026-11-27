[CmdletBinding()]
param([switch]$OfflinePackageOnly,[switch]$NativeInstalled,[Alias('h')][switch]$Help)
$ErrorActionPreference='Stop'
try {
 if($Help){Write-Output 'check.ps1 [-OfflinePackageOnly] [-NativeInstalled]: verify the complete local tools-only directory. No device/service/network access.';exit 0}
 Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Management/Microsoft.PowerShell.Management.psd1')
 Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1')
 $manifestPath=Join-Path $PSScriptRoot 'records/tools-files.json'
 $inventory=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
 if($inventory.schemaVersion -ne 1 -or !$inventory.files.Count){throw 'Invalid tools inventory'}
 $expected=@{}
 foreach($row in $inventory.files){
  if($row.path -notmatch '^[A-Za-z0-9_./-]+$' -or $row.path -match '(^|/)\.\.?(/|$)' -or $expected.ContainsKey($row.path)){throw 'Invalid/duplicate inventory path'}
  $expected[$row.path]=$row
 }
 $actual=@(Get-ChildItem -LiteralPath $PSScriptRoot -Recurse -Force)
 foreach($item in $actual){if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked tools entries are not accepted'}}
 $files=@($actual|Where-Object {!$_.PSIsContainer -and $_.FullName -ne $manifestPath})
 if($files.Count -ne $expected.Count){throw 'Tools inventory has missing/extra files'}
 foreach($file in $files){
  $relative=$file.FullName.Substring($PSScriptRoot.Length+1).Replace('\','/');$row=$expected[$relative]
  if(!$row -or $file.Length -ne $row.bytes -or (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -cne $row.sha256){throw ('Tools inventory differs: '+$relative)}
 }
 if($NativeInstalled){
  . (Join-Path $PSScriptRoot 'jlink-tools.ps1')
  $dependency=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'records/dependencies.json') -Raw|ConvertFrom-Json
  $exe=Resolve-InkTagJLinkTool -RepoRoot (Join-Path $PSScriptRoot '../..');$jlinkDirectory=Split-Path -Parent $exe
  foreach($entry in $dependency.jlinkFiles.PSObject.Properties){$path=Join-Path $jlinkDirectory $entry.Name;if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $entry.Value){throw ('External SEGGER file differs: '+$entry.Name)}}
  $pack=Join-Path $PSScriptRoot 'device-pack'
  foreach($entry in $dependency.devicePackFiles.PSObject.Properties){if((Get-FileHash -LiteralPath (Join-Path $pack $entry.Name) -Algorithm SHA256).Hash.ToLowerInvariant() -cne $entry.Value){throw ('Included OM6626 device-pack file differs: '+$entry.Name)}}
 }
 Write-Output 'TOOLS CHECK PASSED: exact local runtime; no device accessed.'
 if(!$NativeInstalled){Write-Output 'External licensed SEGGER/device-pack readiness was not checked.'}
 exit 0
} catch {[Console]::Error.WriteLine($_.Exception.Message);exit 1}
