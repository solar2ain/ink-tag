[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('generate','flash-hex','preserve')][string]$Action,
    [string]$Firmware='', [string]$Config='', [string]$Output='', [string]$ImageDirectory='',
    [ValidateSet('auto','base','pro')][string]$SourceProfile='auto',
    [Alias('Snr')][string]$JLinkSerial='', [string]$JLinkExe='',
    [int]$FrequencyHz=500000, [ValidateRange(1,86400)][int]$OperationTimeoutSeconds=180,
    [switch]$NoWait, [int]$IntervalSeconds=1, [int]$TimeoutSeconds=0,
    [ValidateSet('en','zh')][string]$Language='en', [switch]$InteractiveLaunch,
    [switch]$Yes, [switch]$DryRun, [Alias('h')][switch]$Help
)
$ErrorActionPreference='Stop'
function Write-NordicInstruction([string]$English,[string]$Chinese) {
    if($Language -eq 'zh'){Write-Host $Chinese}else{Write-Host $English}
}
function Show-NordicFirmwareDialog {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog=New-Object Windows.Forms.OpenFileDialog
    try {
        $dialog.Title=if($Language -eq 'zh'){'选择 nRF52832 完整刷机包（Full ZIP）'}else{'Select nRF52832 complete Full ZIP'}
        $dialog.Filter='Full firmware (*.zip)|*.zip'
        if($dialog.ShowDialog() -eq [Windows.Forms.DialogResult]::OK){return $dialog.FileName}
        return ''
    } finally {$dialog.Dispose()}
}
function Select-NordicInitializationFirmware {
    $choices=@()
    foreach($directory in @($PSScriptRoot,(Split-Path $PSScriptRoot -Parent),(Join-Path $PSScriptRoot 'firmware'),(Join-Path (Split-Path $PSScriptRoot -Parent) 'firmware'),(Join-Path (Split-Path $PSScriptRoot -Parent) 'full'))) {
        if(!(Test-Path -LiteralPath $directory -PathType Container)){continue}
        foreach($file in @(Get-ChildItem -LiteralPath $directory -File -Filter '*.zip' | Sort-Object Name)) {
            try {$null=Read-NordicNativeFirmware $file.FullName $SourceProfile}
            catch {Write-NordicInstruction ("Skipping {0}: {1}" -f $file.Name,$_.Exception.Message) ("跳过 {0}：{1}" -f $file.Name,$_.Exception.Message);continue}
            $choices+=$file.FullName
        }
    }
    if(!$choices.Count) {
        Write-NordicInstruction 'No bundled Full firmware found. Choose an external complete Full ZIP; OTA/App-only files are not accepted.' '未找到随附的完整固件。请选择外部 Full ZIP；不能选择 OTA 或单独 App。'
        return Show-NordicFirmwareDialog
    }
    if($choices.Count -eq 1) {
        Write-NordicInstruction ('Using Full firmware: {0}' -f [IO.Path]::GetFileName($choices[0])) ('已自动采用完整固件：{0}' -f [IO.Path]::GetFileName($choices[0]))
        return $choices[0]
    }
    Write-NordicInstruction 'Available Full firmware:' '可选的完整固件：'
    for($i=0;$i -lt $choices.Count;$i++){Write-Host ('{0}. {1}' -f ($i+1),[IO.Path]::GetFileName($choices[$i]))}
    Write-NordicInstruction 'Firmware number (empty cancels)' '请选择固件编号（直接回车取消）'
    $answer=[string](Read-Host);if(!$answer.Trim()){return ''}
    $number=0
    if(![int]::TryParse($answer.Trim(),[ref]$number) -or $number -lt 1 -or $number -gt $choices.Count){throw 'Invalid firmware number; select a listed Full package.'}
    return $choices[$number-1]
}
function Select-NordicInitializationBoard {
    $boards=@()
    $directory=Join-Path $PSScriptRoot 'configs'
    if(!(Test-Path -LiteralPath $directory -PathType Container)){throw "Board configuration directory missing: $directory"}
    foreach($file in @(Get-ChildItem -LiteralPath $directory -File | Sort-Object Name)) {
        if($file.Extension -notin @('.toml','.json')){continue}
        try {$null=Read-NordicNativeConfiguration $file.FullName} catch {Write-NordicInstruction ("Invalid board file {0}: {1}" -f $file.Name,$_.Exception.Message) ("主板配置无效 {0}：{1}" -f $file.Name,$_.Exception.Message);continue}
        # The native configuration validator admits this exact PCB marking.
        $boards+=@{path=$file.FullName;name=('PriceETag V1.7 ({0})' -f $file.BaseName)}
    }
    if(!$boards.Count){throw 'No supported board configurations supplied; use -Config with a supported TOML/JSON file.'}
    Write-NordicInstruction 'Available boards:' '可选的主板：'
    for($i=0;$i -lt $boards.Count;$i++){Write-Host ('{0}. {1}' -f ($i+1),$boards[$i].name)}
    Write-NordicInstruction 'Board number (empty cancels)' '请选择主板编号（直接回车取消）'
    $answer=[string](Read-Host)
    if(!$answer.Trim()){return ''}
    $number=0
    if(![int]::TryParse($answer.Trim(),[ref]$number) -or $number -lt 1 -or $number -gt $boards.Count){throw 'Select a listed board number.'}
    return $boards[$number-1].path
}
try {
    if($InteractiveLaunch) {
        Write-NordicInstruction 'InkTag nRF52832 Full flashing' 'InkTag nRF52832 完整刷机'
        Write-NordicInstruction 'Install SEGGER J-Link 7.88j. Connect J-Link USB, GND, VCC reference, SWDIO and SWCLK to the powered board.' '请安装 SEGGER J-Link 7.88j；连接 USB，并接好已供电主板的 GND、VCC 参考电压、SWDIO、SWCLK。'
        Write-NordicInstruction 'The tool uses the sole local Full firmware or asks you to select one, then asks for the matching board. Review and confirm complete flashing before waiting for the device. User data is cleared; only verified same-device Pro is retained. HEX programming is followed by independent readback.' '只有一份可用的完整固件时直接采用，否则提示选择，再选匹配主板；复核并确认完整刷机后才等待设备。将清除用户数据，仅保留验证通过的本机 Pro，使用 HEX 刷写并独立读回校验。'
        Write-NordicInstruction 'Cancel a selection with an empty answer/Cancel; Q or Ctrl+C cancels waiting. Programming errors are never automatically retried.' '选择时直接回车或点取消退出；等待时按 Q 或 Ctrl+C 取消。刷写失败不会自动重试。'
    }
    if($Help){Write-Output 'flash.bat -Firmware app-full.zip -Config board.toml [-JLinkSerial SERIAL] [-Yes] (recommended)';Write-Output 'No arguments: use the only local Full or choose one, select the board, then review and confirm. Press a key to close afterward.';Write-Output 'Flashing waits for the J-Link/target by default; Q or Ctrl+C cancels. -NoWait detects once; -TimeoutSeconds bounds readiness. Parameterized BAT calls never pause.';Write-Output 'generate-hex.bat -Firmware app-full.zip -Config board.toml -Output DIR';Write-Output 'flash-hex.bat -ImageDirectory DIR [-JLinkSerial SERIAL] [-Yes]';exit 0}
    . (Join-Path $PSScriptRoot 'internal/native-planner.ps1')
    $interactive=(!$Yes -and !$DryRun)
    if($Action -eq 'flash-hex') {
        if(!$ImageDirectory -and $interactive){Write-Host 'Initialization directory (empty cancels)';$ImageDirectory=([string](Read-Host)).Trim().Trim('"')}
        if(!$ImageDirectory){if($interactive){Write-Output 'Cancelled';exit 0};throw 'ImageDirectory is required'}
        $ImageDirectory=[IO.Path]::GetFullPath($ImageDirectory)
        $manifest=Read-NordicJson ([IO.File]::ReadAllBytes((Join-Path $ImageDirectory 'initialization.json')))
        Assert-NordicKeys $manifest @('schemaVersion','buildTarget','layout','initializationMode','sourceProfile','configurationFile','mainFlashSha256','files') 'initialization manifest'
        if($manifest.schemaVersion -ne 2 -or $manifest.buildTarget -cne 'nrf52832_priceetag' -or $manifest.layout -cne 'N3V1' -or $manifest.initializationMode -cne 'clean' -or $manifest.sourceProfile -notin @('base','pro') -or $manifest.configurationFile -notin @('board.toml','board.json')){throw 'Initialization target/layout/profile mismatch'}
        $names=@('source-full.zip',$manifest.configurationFile,'main-flash.hex','addresses.json')
        if(@($manifest.files).Count -ne $names.Count){throw 'Initialization file set mismatch'}
        $seen=@{}
        foreach($entry in $manifest.files) {
            Assert-NordicKeys $entry @('path','bytes','sha256') 'initialization file'
            if($names -cnotcontains $entry.path -or $seen.ContainsKey($entry.path)){throw 'Unknown/duplicate initialization file'}
            $seen[$entry.path]=$true;$p=Join-Path $ImageDirectory $entry.path
            if(!(Test-Path -LiteralPath $p -PathType Leaf) -or ((Get-Item -LiteralPath $p).Attributes -band [IO.FileAttributes]::ReparsePoint) -or (Get-Item -LiteralPath $p).Length -ne $entry.bytes -or [InkTagNordicImage]::Hash([IO.File]::ReadAllBytes($p)) -cne $entry.sha256){throw 'Initialization files missing/changed/linked'}
        }
        $Firmware=Join-Path $ImageDirectory 'source-full.zip';$Config=Join-Path $ImageDirectory $manifest.configurationFile
        $SourceProfile=$manifest.sourceProfile
        $configured=Get-NordicNativeConfigured $Firmware $Config $SourceProfile
        $expectedHex=[Text.Encoding]::ASCII.GetBytes([InkTagNordicImage]::WriteHex($configured.image))
        if($manifest.mainFlashSha256 -cne [InkTagNordicImage]::Hash($configured.image) -or ![InkTagNordicImage]::Equal($expectedHex,[IO.File]::ReadAllBytes((Join-Path $ImageDirectory 'main-flash.hex')))){throw 'Initialization HEX differs from verified package/config'}
    } else {
        if(!$Firmware -and $interactive){$Firmware=Select-NordicInitializationFirmware}
        if(!$Firmware){if($interactive){Write-Output 'Cancelled';exit 0};throw 'Firmware is required'}
        $Firmware=[IO.Path]::GetFullPath($Firmware)
        $null=Read-NordicNativeFirmware $Firmware $SourceProfile
        if(!$Config){
            if(!$interactive){throw 'Config is required for unattended generation'}
            $Config=Select-NordicInitializationBoard
            if(!$Config){Write-Output 'Cancelled';exit 0}
        }
        $Firmware=[IO.Path]::GetFullPath($Firmware);$Config=[IO.Path]::GetFullPath($Config)
        $configured=Get-NordicNativeConfigured $Firmware $Config $SourceProfile
        $SourceProfile=$configured.profile
    }
    if($Action -eq 'generate') {
        if(!$Output -and $interactive){Write-Host 'New output directory (empty cancels)';$Output=([string](Read-Host)).Trim().Trim('"');if(!$Output){Write-Output 'Cancelled';exit 0}}
        if(!$Output){throw 'Output is required'}
        $Output=[IO.Path]::GetFullPath($Output)
        if(Test-Path -LiteralPath $Output){throw 'Output must be a new directory; existing material is preserved'}
        if($DryRun){@{deviceAccess=$false;pythonRequired=$false;sourceProfile=$SourceProfile;bytes=524288;address=0;output=$Output}|ConvertTo-Json;exit 0}
        [void][IO.Directory]::CreateDirectory($Output)
        [IO.File]::WriteAllText((Join-Path $Output 'main-flash.hex'),[InkTagNordicImage]::WriteHex($configured.image),[Text.Encoding]::ASCII)
        # Keep only the verified App/DAT and selected neutral defaults, never
        # archive another unit's QR/name/ordinary records as an output dependency.
        [IO.File]::WriteAllBytes((Join-Path $Output 'source-full.zip'),(Get-NordicNeutralSource $configured))
        $configName='board'+[IO.Path]::GetExtension($Config).ToLowerInvariant();[IO.File]::Copy($Config,(Join-Path $Output $configName))
        $hash=[InkTagNordicImage]::Hash($configured.image)
        $addresses=@{schemaVersion=1;buildTarget='nrf52832_priceetag';layout='N3V1';nativeFirmwareVersion=[InkTagNordicImage]::U32($configured.image,0x5f008);compatibilityEpoch=1;initializationMode='clean';deviceReadRequiredForGeneration=$false;pythonRequired=$false;ranges=@(@{space='internal-flash';file='main-flash.hex';address=0;bytes=524288;sha256=[InkTagNordicImage]::Hash([IO.File]::ReadAllBytes((Join-Path $Output 'main-flash.hex')))});skipRanges=@(@{space='FICR';address=0x10000000;bytes=4096},@{space='UICR';address=0x10001000;bytes=4096});externalFlash='not accessed';identity='Hardware FICR/SDK address at startup; no foreign unit identity or Pro';qr='Not precomputed; import through WebUI'}
        Write-NordicNativeJson (Join-Path $Output 'addresses.json') $addresses
        $files=@();foreach($name in @('source-full.zip',$configName,'main-flash.hex','addresses.json')){$p=Join-Path $Output $name;$files+=@{path=$name;bytes=(Get-Item -LiteralPath $p).Length;sha256=[InkTagNordicImage]::Hash([IO.File]::ReadAllBytes($p))}}
        Write-NordicNativeJson (Join-Path $Output 'initialization.json') @{schemaVersion=2;buildTarget='nrf52832_priceetag';layout='N3V1';initializationMode='clean';sourceProfile=$SourceProfile;configurationFile=$configName;mainFlashSha256=$hash;files=$files}
        @{generated=$true;writtenToDevice=$false;output=$Output;mainFlashBytes=524288;mainFlashSha256=$hash;pythonRequired=$false}|ConvertTo-Json;exit 0
    }
    $mode=if($Action -eq 'flash-hex'){'clean'}else{'preserve'}
    $flashArguments=@{Firmware=$Firmware;Config=$Config;SourceProfile=$SourceProfile;NativeInitialization=$true;InitializationMode=$mode;JLinkSerial=$JLinkSerial;JLinkExe=$JLinkExe;FrequencyHz=$FrequencyHz;OperationTimeoutSeconds=$OperationTimeoutSeconds;Yes=$Yes;DryRun=$DryRun;Verbose=([bool]$PSBoundParameters['Verbose'])}
    $flashArguments.RetryDetect=(!$NoWait);$flashArguments.NoWait=$NoWait
    $flashArguments.IntervalSeconds=$IntervalSeconds;$flashArguments.TimeoutSeconds=$TimeoutSeconds
    $flashArguments.Language=$Language
    if($Action -eq 'flash-hex'){$flashArguments.ExpectedInitializationSha256=$manifest.mainFlashSha256}
    & (Join-Path $PSScriptRoot 'flash.ps1') @flashArguments
    exit $global:LASTEXITCODE
} catch {
    Write-NordicInstruction 'Flashing stopped. See the error below; no automatic retry.' '刷机已停止。具体错误如下，不会自动重试。'
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
