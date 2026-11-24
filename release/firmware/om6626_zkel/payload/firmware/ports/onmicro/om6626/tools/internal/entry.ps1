[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][ValidateSet('preserve')][string]$Action,
 [string]$Firmware='', [string]$Config='',
 [string]$JLinkSerial='', [string]$JLinkExe='',
 [ValidateScript({$_ -ge 1000 -and $_ -le 1000000 -and $_%1000 -eq 0})][int]$FrequencyHz=1000000,
 [ValidateRange(30,1800)][int]$OperationTimeoutSeconds=360,
 [switch]$Yes,[switch]$DryRun,[Alias('h')][switch]$Help,
 [switch]$InteractiveFlash,[switch]$Chinese,[switch]$RetryDetect=$true
)
$ErrorActionPreference='Stop'
$tools=Split-Path -Parent $PSScriptRoot
$repo=[IO.Path]::GetFullPath((Join-Path $tools '../../../../..'))
function Resolve-OmInputPath([string]$Path) {
 $Path=$Path.Trim().Trim('"')
 if(![IO.Path]::IsPathRooted($Path) -and !(Test-Path -LiteralPath $Path)) {
  foreach($base in @($tools,$repo)) {
   $local=Join-Path $base $Path
   if(Test-Path -LiteralPath $local){return [IO.Path]::GetFullPath($local)}
  }
 }
 return [IO.Path]::GetFullPath($Path)
}
function Get-OmFlashText([string]$English,[string]$ChineseText) {
 if($Chinese){return $ChineseText}
 return $English
}
function Select-OmFullFile {
 Add-Type -AssemblyName System.Windows.Forms
 $dialog=[Windows.Forms.OpenFileDialog]::new()
 try {
  $dialog.Title=Get-OmFlashText 'Select OM Full firmware' '选择 OM 完整刷机固件'
  $dialog.InitialDirectory=$tools
  $dialog.Filter='OM Full ZIP or component manifest (*.zip;*.json)|*.zip;*.json'
  if($dialog.ShowDialog() -eq [Windows.Forms.DialogResult]::OK){return $dialog.FileName}
 } finally {$dialog.Dispose()}
}
function Read-OmFlashChoice([string]$Prompt) {
 Write-Host $Prompt
 return Read-Host
}
function Confirm-OmFullFlash {
 Write-Host (Get-OmFlashText 'Full initialization clears ordinary main-Flash user data. External Flash is untouched.' '完整初始化会清除主闪存中的普通用户数据，外置闪存不受影响。')
 return (Read-OmFlashChoice (Get-OmFlashText 'Proceed with Full flash? [y/N]' '确认执行完整刷机？[y/N]')) -match '^(?i:y|yes)$'
}
try {
 if([Console]::IsInputRedirected){[Console]::InputEncoding=[Text.UTF8Encoding]::new($false)}
 if([Console]::IsOutputRedirected){[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)}
 if($InteractiveFlash) {
  Write-Host (Get-OmFlashText 'InkTag OM6626 Full flash' 'InkTag OM6626 完整刷机')
  Write-Host (Get-OmFlashText 'Install licensed SEGGER J-Link/J-Flash 7.88j and the OM6626 device pack. Connect SWDIO, SWCLK, GND and VTref, then power the board.' '安装已授权的 SEGGER J-Link/J-Flash 7.88j 和 OM6626 设备包。连接 SWDIO、SWCLK、GND 和 VTref，再给主板上电。')
  Write-Host (Get-OmFlashText 'Check the Full firmware and choose the matching PCB, then confirm Full flash to start waiting for the board. Power-cycle the board if needed. Enter a blank choice or press Esc while waiting to cancel.' '核对完整固件并选择对应主板，确认完整刷机后开始等待设备。必要时可重新给主板上电。选择时直接回车或等待时按 Esc 可取消。')
 }
 if($Help){
  Write-Output 'OM native tools: Windows PowerShell 5.1/.NET + existing licensed J-Link/J-Flash 7.88j/device pack. No Python, pip, CLI, BLE or Find service.'
  Write-Output 'flash.bat -Firmware components.json|app-full.zip -Config board.toml|board.json [-JLinkSerial SERIAL] [-Yes] [-DryRun]'
  Write-Output 'Double-click flash.bat (English) or flash-zh.bat (Chinese) to select Full firmware and the PCB, then confirm before device detection. Press Esc while waiting to cancel.'
  Write-Output 'Full clears ordinary main-Flash settings/password/Find/images/fonts/health. Necessary boot/CPFT, runtime MBR and ZK CONF are skipped; native ROM bookkeeping remains vendor-loader owned. OM42B external images are retained.'
  exit 0
 }
 # A Windows PowerShell child can inherit the PowerShell 7 module path.
 Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Management/Microsoft.PowerShell.Management.psd1')
 Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1')
 if(!$Firmware){
  $candidates=@()
  $directories=@($tools,(Join-Path $tools 'firmware'))
  if($InteractiveFlash){
   $directories+=Join-Path $repo 'tools'
   $bundle=Split-Path -Parent $repo
   if(Test-Path -LiteralPath (Join-Path $bundle 'records/tools-files.json') -PathType Leaf){
    $directories+=@($bundle,(Join-Path $bundle 'full'))
   }
  }
  foreach($base in @('release/firmware','firmware')) {
   $directories+=Join-Path $repo ($base+'/om6626_zkel/full')
  }
  foreach($directory in ($directories|Sort-Object -Unique)) {
   if(Test-Path -LiteralPath $directory -PathType Container){
    $candidates+=@(Get-ChildItem -LiteralPath $directory -File | Where-Object {($_.Extension -eq '.zip' -and $_.Name -like '*full*') -or $_.Name -eq 'components.json'} | ForEach-Object {$_.FullName})
   }
  }
  $candidates=@($candidates|Sort-Object -Unique)
  if($InteractiveFlash -and !$candidates.Count){
   Write-Host (Get-OmFlashText 'No bundled Full firmware found. Select a file in the dialog.' '未找到随附的完整固件，请在文件窗口选择。')
   $Firmware=Select-OmFullFile
  } elseif($candidates.Count -eq 1){
   $Firmware=$candidates[0]
  } else {
   for($i=0;$i -lt $candidates.Count;$i++){Write-Output ("{0}. {1}" -f ($i+1),$candidates[$i])}
   if($InteractiveFlash){
    $choice=Read-OmFlashChoice (Get-OmFlashText 'Full firmware number (blank cancels)' '完整固件编号（直接回车取消）')
    if(!$choice){exit 2}
    $number=0
    if(![int]::TryParse($choice,[ref]$number) -or $number -lt 1 -or $number -gt $candidates.Count){
     throw (Get-OmFlashText 'Invalid Full firmware selection.' '完整固件编号无效。')
    }
    $Firmware=$candidates[$number-1]
   } else {
    $Firmware=Read-Host 'Firmware number or component manifest/Full ZIP path (blank cancels)'
    $number=0
    if([int]::TryParse($Firmware,[ref]$number) -and $number -ge 1 -and $number -le $candidates.Count){$Firmware=$candidates[$number-1]}
   }
  }
  if(!$Firmware){exit 2}
 }
 if($InteractiveFlash){
  . (Join-Path $PSScriptRoot 'om-format.ps1')
  $Firmware=Resolve-OmInputPath $Firmware
  Write-Host ((Get-OmFlashText 'Firmware: ' '固件：')+$Firmware)
  $fw=Read-OmFirmware $Firmware
 }
 if(!$Config){
  $configDir=Join-Path $tools 'configs'
  if(!(Test-Path -LiteralPath $configDir -PathType Container)){
   throw (Get-OmFlashText "Board config directory is missing: $configDir" "找不到主板配置目录：$configDir")
  }
  $presets=@(Get-ChildItem -LiteralPath $configDir -Filter '*.toml' -File|Sort-Object Name)
  if($InteractiveFlash -and !$presets.Count){
   throw (Get-OmFlashText "No board configurations found in: $configDir" "主板配置目录中没有 TOML 文件：$configDir")
  }
  for($i=0;$i -lt $presets.Count;$i++){Write-Output ("{0}. {1}" -f ($i+1),$presets[$i].BaseName)}
  $prompt=if($InteractiveFlash){Get-OmFlashText 'PCB config number (blank cancels)' '主板配置编号（直接回车取消）'}else{'PCB config number or TOML/JSON path'}
  $choice=Read-OmFlashChoice $prompt
  if(!$choice){exit 2}
  $number=0
  if([int]::TryParse($choice,[ref]$number) -and $number -ge 1 -and $number -le $presets.Count){$Config=$presets[$number-1].FullName}
  elseif($InteractiveFlash){throw (Get-OmFlashText 'Invalid PCB config selection.' '主板配置编号无效。')}
  else{$Config=$choice}
 }
 if(!$InteractiveFlash){. (Join-Path $PSScriptRoot 'om-format.ps1')}
 $Firmware=Resolve-OmInputPath $Firmware;$Config=Resolve-OmInputPath $Config
 if(!$InteractiveFlash){Write-Output ('Firmware: '+$Firmware);$fw=Read-OmFirmware $Firmware}
 Write-Output ((Get-OmFlashText 'Config: ' '主板配置：')+$Config)
 $cfg=Read-OmConfig $Config
 if($cfg.board.family -cne $fw.meta.family){throw (Get-OmFlashText 'PCB config and firmware executable family differ' '主板配置与固件家族不匹配') }
 if($DryRun){Write-Output 'Firmware/config validation passed. No native tool resolved, no device accessed, no write.';exit 0}
 if((!$Yes -or $InteractiveFlash) -and !(Confirm-OmFullFlash)){exit 2}
 . (Join-Path $repo 'tools/hardware/activation-preservation.ps1')
 . (Join-Path $PSScriptRoot 'om-native.ps1')
 $result=Invoke-OmNativeFlash $fw $cfg @{JLinkSerial=$JLinkSerial;JLinkExe=$JLinkExe;FrequencyHz=$FrequencyHz;OperationTimeoutSeconds=$OperationTimeoutSeconds;InteractiveFlash=[bool]$InteractiveFlash;RetryDetect=[bool]$RetryDetect}
 if($null -eq $result){Write-Host (Get-OmFlashText 'Flash canceled. No Flash write started.' '已取消刷机，未开始写入闪存。');exit 2}
 if(!$InteractiveFlash){$result|ConvertTo-Json -Depth 8}
 exit 0
} catch {
 [Console]::Error.WriteLine((Get-OmFlashText 'Error: ' '错误：')+$_.Exception.Message)
 exit 1
} finally {
 if($InteractiveFlash -and $Chinese){Write-Host '按任意键关闭窗口。'}
}
