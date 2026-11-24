. (Join-Path $PSScriptRoot 'native-image.ps1')
. (Join-Path $PSScriptRoot 'activation-preservation.ps1')
function Write-NordicNativeJson([string]$Path,$Value) {
    [IO.File]::WriteAllText($Path,($Value | ConvertTo-Json -Depth 18)+"`n",(New-Object Text.UTF8Encoding($false)))
}
function Get-NordicNativeConfigured([string]$Firmware,[string]$Config,[string]$SourcePolicy) {
    $f=Read-NordicNativeFirmware $Firmware $SourcePolicy
    $wire=Read-NordicNativeConfiguration $Config
    [byte[]]$board=New-Object byte[] 53
    [Buffer]::BlockCopy([Text.Encoding]::ASCII.GetBytes('NFP1'),0,$board,0,4)
    [byte[]]$pins=1,0,0,0,8,9,10,11,12,13,255,255,255,255,255,255
    [byte[]]$aux=1,2,3,255,0,255,0,0,255,255,208,7
    [Buffer]::BlockCopy($pins,0,$board,4,16);[Buffer]::BlockCopy($aux,0,$board,20,12);[Buffer]::BlockCopy($wire,0,$board,32,17)
    [InkTagNordicImage]::Put32($board,49,[InkTagNordicImage]::Fnv([InkTagNordicImage]::Part($board,0,49)))
    [byte[]]$screen=New-Object byte[] 28;$screen[0]=83;$screen[1]=80;$screen[2]=1;$screen[3]=17
    [Buffer]::BlockCopy($wire,0,$screen,4,17);[InkTagNordicImage]::Put32($screen,24,[InkTagNordicImage]::Fnv([InkTagNordicImage]::Part($screen,0,24)))
    [byte[]]$settings=New-Object byte[] 112
    [Buffer]::BlockCopy([Text.Encoding]::ASCII.GetBytes('NUS1'),0,$settings,0,4);$settings[4]=1
    [byte[]]$buttons=1,0,10,0,9,0,9,2,1,1,0,0,0,0,0,0
    [Buffer]::BlockCopy($buttons,0,$settings,50,16);[Buffer]::BlockCopy($aux,0,$settings,96,12)
    [InkTagNordicImage]::Put32($settings,108,[InkTagNordicImage]::Fnv([InkTagNordicImage]::Part($settings,0,108)))
    [byte[]]$boot=New-Object byte[] 16
    [InkTagNordicImage]::Put32($boot,0,0x3153424e);$boot[4]=1;$boot[5]=1
    [InkTagNordicImage]::Put32($boot,8,$f.appBytes);[InkTagNordicImage]::Put32($boot,12,$f.appCrc32)
    $records=New-Object 'System.Collections.Generic.Dictionary[int,byte[]]'
    $records.Add(1,$board);$records.Add(3,$screen);$records.Add(7,$settings);$records.Add(12,$boot)
    [byte[]]$image=$f.image.Clone()
    # No unit QR, name, password, Find, Pro or old user resources in generic BIN.
    [Buffer]::BlockCopy([InkTagNordicImage]::Fill(32768,255),0,$image,0x4e000,32768)
    [Buffer]::BlockCopy([InkTagNordicImage]::Fill(0x20000,255),0,$image,0x60000,0x20000)
    [Buffer]::BlockCopy([InkTagNordicImage]::EncodeJournal($records,1),0,$image,0x56000,8192)
    [InkTagNordicImage]::VerifyNative($image)
    return @{image=$image;records=$records;ota=$f.ota;profile=$f.profile;appBytes=$f.appBytes;appCrc32=$f.appCrc32;firmwareSha256=$f.firmwareSha256;configurationSha256=[InkTagNordicImage]::Hash([IO.File]::ReadAllBytes($Config))}
}
function Get-NordicNeutralSource($Configured) {
    $hex=[Text.Encoding]::ASCII.GetBytes([InkTagNordicImage]::TemplateHex($Configured.image))
    $metadata=@{format_version=1;mcu='nrf52832';firmware_family='nrf52832_priceetag';backend_ids=@('nrf52832_priceetag');hw_version=56;compatibility_epoch=1;config_schema=1;baseline='inktag-release-v1';layout_version=1;layout_marker='N3V1';images=@{'full.hex'=@{bytes=$hex.Length;sha256=[InkTagNordicImage]::Hash($hex)};'app-ota.zip'=@{bytes=$Configured.ota.Length;sha256=[InkTagNordicImage]::Hash($Configured.ota)}}}
    $json=[Text.Encoding]::UTF8.GetBytes(($metadata|ConvertTo-Json -Depth 8))
    return ,([InkTagNordicImage]::FullZip($json,$hex,$Configured.ota))
}
function Get-NordicNativeRanges($Configured,[string]$Mode) {
    if($Mode -eq 'clean'){return @()}
    $ranges=@(@{address=0x56000;bytes=8192;file='native-activation-journal.bin';optional=$true})
    $start=0x1000+[InkTagNordicImage]::U32($Configured.image,0x5f034)
    if($start -lt 0x26000){$ranges=@(@{address=$start;bytes=0x26000-$start;file='low-flash-gap.bin';optional=$true})+$ranges}
    return $ranges
}
function Assert-NordicNativeChip([byte[]]$Ficr,[byte[]]$Uicr) {
    if($Ficr.Length -ne 4096 -or $Uicr.Length -ne 4096 -or [InkTagNordicImage]::U32($Ficr,0x100) -ne 0x52832 -or [InkTagNordicImage]::U32($Ficr,0x10) -ne 4096 -or [InkTagNordicImage]::U32($Ficr,0x14) -ne 128 -or [InkTagNordicImage]::U32($Ficr,0x10c) -ne 64 -or [InkTagNordicImage]::U32($Ficr,0x110) -ne 512){throw 'Exact nRF52832 xxAA/512KiB/64KiB hardware required'}
    # The raw ID is a connection comparison, not authority for Pro when blank.
    return ('{0:X8}{1:X8}' -f [InkTagNordicImage]::U32($Ficr,0x64),[InkTagNordicImage]::U32($Ficr,0x60))
}
function Get-NordicNativeCapture([string]$Directory,$Configured,[string]$Mode) {
    $ficr=[IO.File]::ReadAllBytes((Join-Path $Directory 'nrf52832-priceetag-ficr.bin'))
    $uicr=[IO.File]::ReadAllBytes((Join-Path $Directory 'nrf52832-priceetag-uicr.bin'))
    $id=Assert-NordicNativeChip $ficr $uicr
    $probePath=Join-Path $Directory 'probe.json';$probe=Read-NordicJson ([IO.File]::ReadAllBytes($probePath))
    if($probe.kind -cne 'jlink' -or [string]$probe.serial -notmatch '^\d+$' -or $probe.sourceFirmwareSha256 -cne $Configured.firmwareSha256){throw 'Exact probe/source capture mismatch'}
    $files=@(@{file='nrf52832-priceetag-ficr.bin';bytes=4096;sha256=[InkTagNordicImage]::Hash($ficr)},@{file='nrf52832-priceetag-uicr.bin';bytes=4096;sha256=[InkTagNordicImage]::Hash($uicr)})
    $available=@();$missing=@()
    foreach($part in @(Get-NordicNativeRanges $Configured $Mode)) {
        $p=Join-Path $Directory $part.file
        if((Test-Path -LiteralPath $p -PathType Leaf) -and (Get-Item -LiteralPath $p).Length -eq $part.bytes) {
            $files+=@{file=$part.file;bytes=$part.bytes;sha256=[InkTagNordicImage]::Hash([IO.File]::ReadAllBytes($p))};$available+=,$part
        } else {$missing+=,$part.file}
    }
    return @{ficr=$ficr;uicr=$uicr;available=$available;missing=$missing;info=@{schemaVersion=1;kind='nrf52832-native-preservation';initializationMode=$Mode;deviceId=$id;probeSerial=[string]$probe.serial;probeBindingSha256=[InkTagNordicImage]::Hash([IO.File]::ReadAllBytes($probePath));sourceFirmwareSha256=$Configured.firmwareSha256;sourceConfigSha256=$Configured.configurationSha256;files=$files;optionalUnavailable=$missing;wholeFlashPreRead=$false}}
}
function Get-NordicNativePlan($Configured,$Capture,[string]$Directory,[string]$Mode) {
    [byte[]]$image=$Configured.image.Clone();$ranges=@();$warnings=@();$authority=@{state='base';authority='clean-initialization'}
    $records=New-Object 'System.Collections.Generic.Dictionary[int,byte[]]'
    foreach($k in $Configured.records.Keys){$records.Add($k,$Configured.records[$k])}
    if($Mode -eq 'preserve') {
        $authority=@{state='base';authority='no-valid-same-device-Pro'}
        foreach($part in @($Capture.available)) {
            [byte[]]$old=[IO.File]::ReadAllBytes((Join-Path $Directory $part.file));$ranges+=,$part
            if($part.file -ceq 'low-flash-gap.bin'){[Buffer]::BlockCopy($old,0,$image,$part.address,$old.Length)}
            elseif($part.file -ceq 'native-activation-journal.bin') {
                $oldRecords=[InkTagNordicImage]::Journal($old,$true)
                $uid=[InkTagNordicImage]::Part($Capture.ficr,0x60,8)
                $hasUid=([BitConverter]::ToString($uid) -notin @('00-00-00-00-00-00-00-00','FF-FF-FF-FF-FF-FF-FF-FF'))
                if($oldRecords.ContainsKey(8) -and $hasUid) {
                    try {
                        $deviceId=Get-InkTagActivationDeviceId -PlatformId 0x0301 -ProductId 1 -RawUid $uid
                        # Fixed production trust root: no configuration key/ID override.
                        $grant=Test-InkTagActivationToken -Token $oldRecords[8] -ProductId 1 -FeatureMask 1 -SecurityEpoch 0 -DeviceId $deviceId
                        if(!$grant.signatureVerified -or !$grant.hostHardwareBindingVerified){throw 'Same-device authorization not established'}
                        $records.Add(8,$oldRecords[8]);$authority=@{state='pro';authority='verified-current-journal';sequence=$grant.sequence;hostHardwareBindingVerified=$true}
                    } catch {$warnings+='Old Pro unavailable/invalid/foreign; initialized without Pro.'}
                } elseif(!$hasUid){$warnings+='Immutable UID unavailable; no Pro retained.'}
            }
        }
        foreach($name in @($Capture.missing)){$warnings+=('Optional '+$name+' unreadable/missing; defaults used.')}
        [Buffer]::BlockCopy([InkTagNordicImage]::EncodeJournal($records,1),0,$image,0x56000,8192)
    }
    [InkTagNordicImage]::VerifyNative($image)
    $pages=@(0..127 | ForEach-Object {$_*4096})
    $plan=@{schemaVersion=1;board='nrf52832_priceetag';deviceId=$Capture.info.deviceId;initializationMode=$Mode;sourceProfile=$Configured.profile;sourceFirmwareSha256=$Configured.firmwareSha256;sourceConfigSha256=$Configured.configurationSha256;expectedFlashSha256=[InkTagNordicImage]::Hash($image);appBytes=$Configured.appBytes;appCrc32=('{0:X8}' -f $Configured.appCrc32);preservationRanges=$ranges;wholeFlashPreRead=$false;writePages=$pages;readbackBytes=524288;warnings=$warnings;activationRetention=$authority;retainedRecordIds=@($records.Keys | Where-Object {$_ -eq 8});ficrSha256=[InkTagNordicImage]::Hash($Capture.ficr);uicrSha256=[InkTagNordicImage]::Hash($Capture.uicr);generatedAppName='hardware/SDK default (derived on startup)';qr='not-provisioned; import through WebUI';externalFlash='not accessed or erased';resetOnlyAfterReadback=$true;diagnosticCleanup=@{ramAddress=0x20000000;ramBytes=65536;resetReasonAddress=0x40000400;resetReasonKnownMask=0x000f000f;algorithm='nrf-swd-final-reset-direct-debug-v1';noTargetWorkerAfterCleanup=$true;resetOnlyAfterReadback=$true}}
    return @{image=$image;plan=$plan}
}
function Invoke-NordicNativeCommand {
    param([Parameter(ValueFromRemainingArguments=$true)][object[]]$Arguments)
    $scriptPath=[string]$Arguments[0];$a=@{};$action=''
    $i=1
    if([IO.Path]::GetFileName($scriptPath) -ceq 'nordic-full-settings.py'){$action=[string]$Arguments[1];$i=2}
    while($i -lt $Arguments.Count) {
        $key=[string]$Arguments[$i]
        if($key -notmatch '^--[a-z-]+$' -or $a.ContainsKey($key) -or $i+1 -ge $Arguments.Count){throw 'Native planner arguments'}
        $a[$key]=[string]$Arguments[$i+1];$i+=2
    }
    $mode=if($script:InitializationMode){$script:InitializationMode}else{'preserve'}
    $fw=if($a.ContainsKey('--firmware')){$a['--firmware']}else{$a['--base']}
    $sourcePolicy=if($a.ContainsKey('--source-profile')){$a['--source-profile']}else{'auto'}
    $configured=Get-NordicNativeConfigured $fw $a['--config'] $sourcePolicy
    if([IO.Path]::GetFileName($scriptPath) -ceq 'nordic_full_config.py') {
        $warning=if($mode -eq 'clean'){'Clean initialization clears activation and ordinary user data. FICR/UICR/calibration are not programmed.'}else{'Preserving initialization clears ordinary user data and retains only verified same-device Pro. FICR/UICR/calibration are not programmed.'}
        return (@{board_marking='PriceETag V1.7';sourceProfile=$configured.profile;initializationMode=$mode;base_sha256=$configured.firmwareSha256;configuration_sha256=$configured.configurationSha256;app_bytes=$configured.appBytes;app_crc32=('{0:X8}' -f $configured.appCrc32);warning=$warning;recovery_trust='Matching Full member integrity; no independent Recovery signature'}|ConvertTo-Json -Depth 8)
    }
    if([IO.Path]::GetFileName($scriptPath) -cne 'nordic-full-settings.py'){throw 'Unsupported native planner entry'}
    if($action -eq 'describe') {
        $ranges=@(Get-NordicNativeRanges $configured $mode);$total=0;foreach($p in $ranges){$total+=$p.bytes}
        return (@{wholeFlashPreRead=$false;ranges=$ranges;mainFlashPreReadBytes=$total;ficrBytes=4096;uicrBytes=4096;sharedActivationVerifierAvailable=$true}|ConvertTo-Json -Depth 8)
    }
    $capture=Get-NordicNativeCapture $a['--capture'] $configured $mode
    $metadata=Join-Path $a['--capture'] 'preservation-info.json'
    if($action -eq 'capture') {
        if(Test-Path -LiteralPath $metadata){throw 'Preserve existing capture evidence'}
        Write-NordicNativeJson $metadata $capture.info;return
    }
    $old=Read-NordicJson ([IO.File]::ReadAllBytes($metadata))
    foreach($key in @('deviceId','probeSerial','probeBindingSha256','sourceFirmwareSha256','sourceConfigSha256','initializationMode')) {if($old.$key -cne $capture.info[$key]){throw 'Current capture binding changed'}}
    if(@($old.files).Count -ne @($capture.info.files).Count){throw 'Preservation file set changed'}
    for($j=0;$j -lt $old.files.Count;$j++){foreach($key in @('file','bytes','sha256')){if($old.files[$j].$key -cne $capture.info.files[$j][$key]){throw 'Preservation bytes changed'}}}
    $prepared=Get-NordicNativePlan $configured $capture $a['--capture'] $mode
    $directory=$a['--output'];$planPath=Join-Path $directory 'plan.json'
    if($action -eq 'prepare') {
        if(Test-Path -LiteralPath $directory){throw 'Plan output must be new'}
        [void][IO.Directory]::CreateDirectory($directory)
        [IO.File]::WriteAllBytes((Join-Path $directory 'expected-flash.bin'),$prepared.image)
        [IO.File]::WriteAllText((Join-Path $directory 'prepared.hex'),[InkTagNordicImage]::WriteHex($prepared.image),[Text.Encoding]::ASCII)
        $prepared.plan.preparedHexSha256=[InkTagNordicImage]::Hash([IO.File]::ReadAllBytes((Join-Path $directory 'prepared.hex')))
        Write-NordicNativeJson $planPath $prepared.plan;return
    }
    $plan=Read-NordicJson ([IO.File]::ReadAllBytes($planPath));$expected=[IO.File]::ReadAllBytes((Join-Path $directory 'expected-flash.bin'))
    $prepared.plan.preparedHexSha256=[InkTagNordicImage]::Hash([Text.Encoding]::ASCII.GetBytes([InkTagNordicImage]::WriteHex($prepared.image)))
    if(![InkTagNordicImage]::SameJson([IO.File]::ReadAllBytes($planPath),[Text.Encoding]::UTF8.GetBytes(($prepared.plan|ConvertTo-Json -Depth 18)))){throw 'Native write ranges/cleanup/context changed'}
    foreach($key in @('deviceId','initializationMode','sourceProfile','sourceFirmwareSha256','sourceConfigSha256','expectedFlashSha256','ficrSha256','uicrSha256')) {if($plan.$key -cne $prepared.plan[$key]){throw 'Native plan context changed'}}
    if(![InkTagNordicImage]::Equal($expected,$prepared.image) -or $plan.preparedHexSha256 -cne [InkTagNordicImage]::Hash([IO.File]::ReadAllBytes((Join-Path $directory 'prepared.hex')))){throw 'Prepared image changed'}
    if($action -eq 'check'){return 'Unit-bound native Full plan verified offline'}
    $readback=$a['--readback']
    if($action -eq 'cleanup-observe') {
        $bits=[InkTagNordicImage]::U32([IO.File]::ReadAllBytes((Join-Path $readback 'reset-before.bin')),0)
        if($bits -band 0xfff0fff0){throw 'Unknown reset-cause bits'};return [string]$bits
    }
    if($action -eq 'cleanup-verify') {
        $ram=[IO.File]::ReadAllBytes((Join-Path $readback 'ram-after.bin'))
        if($ram.Length -ne 65536 -or ![InkTagNordicImage]::Equal($ram,(New-Object byte[] 65536)) -or [InkTagNordicImage]::U32([IO.File]::ReadAllBytes((Join-Path $readback 'reset-after.bin')),0) -ne 0){throw 'Native SRAM/reset cleanup readback mismatch'}
        $pairs=@(@('cleanup-ficr.bin',$capture.ficr),@('cleanup-uicr.bin',$capture.uicr),@('cleanup-boot.bin',[InkTagNordicImage]::Part($expected,0x5e000,8192)))
    } elseif($action -eq 'verify') {$pairs=@(@('flash.bin',$expected),@('ficr.bin',$capture.ficr),@('uicr.bin',$capture.uicr))}
    else {throw 'Unknown native planner action'}
    foreach($pair in $pairs){if(![InkTagNordicImage]::Equal([IO.File]::ReadAllBytes((Join-Path $readback $pair[0])),$pair[1])){throw ('Independent readback mismatch: '+$pair[0])}}
    return 'Native readback and identity verified; no reset replay'
}
