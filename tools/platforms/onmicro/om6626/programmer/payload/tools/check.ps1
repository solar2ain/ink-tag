[CmdletBinding()]
param([switch]$OfflinePackageOnly,[switch]$NativeInstalled,[Alias('h')][switch]$Help)
$ErrorActionPreference='Stop'
try {
 if($Help){Write-Output 'check.ps1 [-OfflinePackageOnly] [-NativeInstalled]: verify the complete local tools-only directory. No device/service/network access.';exit 0}
 Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Management/Microsoft.PowerShell.Management.psd1')
 Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1')
 $payload=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$bundle=Split-Path -Parent $payload
 $inventory=Get-Content -LiteralPath (Join-Path $bundle 'records/tools-files.json') -Raw|ConvertFrom-Json
 if($inventory.schemaVersion -ne 1 -or !$inventory.files.Count){throw 'Invalid tools inventory'}
 $expected=@{}
 foreach($row in $inventory.files){
  if($row.path -notmatch '^payload/[A-Za-z0-9_./-]+$' -or $row.path -match '(^|/)\.\.?(/|$)' -or $expected.ContainsKey($row.path)){throw 'Invalid/duplicate inventory path'}
  $expected[$row.path]=$row
 }
 $actual=@(Get-ChildItem -LiteralPath $payload -Recurse -Force)
 foreach($item in $actual){if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked tools entries are not accepted'}}
 $files=@($actual|Where-Object {!$_.PSIsContainer})
 if($files.Count -ne $expected.Count){throw 'Tools inventory has missing/extra files'}
 foreach($file in $files){
  $relative='payload/'+$file.FullName.Substring($payload.Length+1).Replace('\','/');$row=$expected[$relative]
  if(!$row -or $file.Length -ne $row.bytes -or (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -cne $row.sha256){throw ('Tools inventory differs: '+$relative)}
 }
 if($NativeInstalled){
  . (Join-Path $payload 'tools/jlink-tools.ps1')
  $dependency=Get-Content -LiteralPath (Join-Path $bundle 'records/dependencies.json') -Raw|ConvertFrom-Json
  $exe=Resolve-InkTagJLinkTool -RepoRoot $payload;$jlinkDirectory=Split-Path -Parent $exe
  foreach($entry in $dependency.jlinkFiles.PSObject.Properties){$path=Join-Path $jlinkDirectory $entry.Name;if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $entry.Value){throw ('External SEGGER file differs: '+$entry.Name)}}
  $pack=Join-Path $payload 'device-pack'
  foreach($entry in $dependency.devicePackFiles.PSObject.Properties){if((Get-FileHash -LiteralPath (Join-Path $pack $entry.Name) -Algorithm SHA256).Hash.ToLowerInvariant() -cne $entry.Value){throw ('Included OM6626 device-pack file differs: '+$entry.Name)}}
 }
 Write-Output 'TOOLS CHECK PASSED: exact local directory; no device accessed.'
 if(!$NativeInstalled){Write-Output 'External licensed SEGGER/device-pack readiness was not checked.'}
 exit 0
} catch {[Console]::Error.WriteLine($_.Exception.Message);exit 1}
