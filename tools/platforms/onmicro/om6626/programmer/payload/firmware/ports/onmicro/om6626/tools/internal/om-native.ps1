# Reuse the maintained OM J-Link/J-Flash scheduler, contact gates and logs.
# Only ordinary format preparation is replaced; no Python process is launched.
function New-OmNativeLoaders([string]$Pack,[string]$Output) {
    foreach($file in @('Devices/Onmicro/OM662XX_nombr.elf','Devices/Onmicro/OM662XX.elf','JLinkDevices.xml')){
        if(!(Test-Path -LiteralPath (Join-Path $Pack $file) -PathType Leaf)){
            throw (Get-OmFlashText "OM6626 device pack is missing $file in '$Pack'. Update the complete InkTag checkout." "仓库中的 OM6626 设备包缺少 $file。路径为 '$Pack'。请更新完整的 InkTag 代码库。")
        }
    }
    $raw=[IO.File]::ReadAllBytes((Join-Path $Pack 'Devices/Onmicro/OM662XX_nombr.elf'));$mbr=[IO.File]::ReadAllBytes((Join-Path $Pack 'Devices/Onmicro/OM662XX.elf'));$xmlBytes=[IO.File]::ReadAllBytes((Join-Path $Pack 'JLinkDevices.xml'))
    if([InkTag.OmCodec]::Sha($raw) -cne '9a53c01b9363371e8eff44a7a9de80e2942e96025aede3ade8053875ab6ad640' -or [InkTag.OmCodec]::Sha($mbr) -cne '8523643fc138b84601c850571499068d36da954b60e7a3aa0be68386ad8e13af' -or [InkTag.OmCodec]::Sha($xmlBytes) -cne '12f821b52aabddca30300bd10710ecae3be313d08d3b70da90481abf36f9ec60'){throw 'Maintained external native loader pack differs'}
    $last=[byte[]]$raw.Clone();foreach($p in @(@(0x19c8,'a1f58051'),@(0x1fe2,'a3f58053'))){$at=0xa0+$p[0];if(![InkTag.OmCodec]::Equal([InkTag.OmCodec]::Slice($last,$at,4),(Get-OmBytes $p[1]))){throw 'Last-page loader instruction binding'};[Array]::Copy((Get-OmBytes 'aff30080'),0,$last,$at,4)}
    $null=New-Item -ItemType Directory -Path $Output -ErrorAction Stop;$namespace=[Guid]::NewGuid().ToString('N');$names=@('INKTAG_DEV_OM_NOMBR','INKTAG_DEV_OM_MBR','INKTAG_DEV_OM_LAST')|ForEach-Object {$_+'_'+$namespace}
    [xml]$source=[Text.Encoding]::UTF8.GetString($xmlBytes);$database=[xml]'<DataBase />';$hashes=@{}
    for($i=0;$i -lt 3;$i++){$sourceName='OM662XX_NOMBR';if($i -eq 1){$sourceName='OM662XX'};$nodes=@($source.DataBase.Device|Where-Object {$_.ChipInfo.Name -ceq $sourceName});if($nodes.Count -ne 1 -or @($nodes[0].FlashBankInfo).Count -ne 1){throw 'Native loader XML shape'};$node=$database.ImportNode($nodes[0],$true);$node.ChipInfo.SetAttribute('Name',$names[$i]);$path=Join-Path $Output ($names[$i]+'.elf');$node.FlashBankInfo.SetAttribute('Loader',$path);$null=$database.DocumentElement.AppendChild($node);$bytes=$raw;if($i -eq 1){$bytes=$mbr};if($i -eq 2){$bytes=$last};[IO.File]::WriteAllBytes($path,$bytes);$hashes[$path]=[InkTag.OmCodec]::Sha($bytes)}
    $xmlPath=Join-Path $Output 'JLinkDevices.xml';[IO.File]::WriteAllText($xmlPath,$database.OuterXml,[Text.UTF8Encoding]::new($false));$hashes[$xmlPath]=[InkTag.OmCodec]::Sha([IO.File]::ReadAllBytes($xmlPath))
    return [pscustomobject]@{directory=$Output;namespace=$namespace;devices=$names;files=[pscustomobject]$hashes;sourceXMLSHA256=[InkTag.OmCodec]::Sha($xmlBytes);originalNoMBRSHA256=[InkTag.OmCodec]::Sha($raw);originalMBRSHA256=[InkTag.OmCodec]::Sha($mbr)}
}
function New-OmIdentityReader([string]$Directory,[string]$Loader) {
    $null=New-Item -ItemType Directory -Path $Directory -ErrorAction Stop;$raw=[IO.File]::ReadAllBytes($Loader)
    if([InkTag.OmCodec]::Sha($raw) -cne '9a53c01b9363371e8eff44a7a9de80e2942e96025aede3ade8053875ab6ad640'){throw 'Identity reader loader binding'}
    $code=[InkTag.OmCodec]::Slice($raw,0xa0,12572);if([InkTag.OmCodec]::Sha($code) -cne 'a25f95c0742d591abb460281d4f213d9c1a0d1c557ab044af1fd7381c22d91a4'){throw 'Identity code binding'}
    $trampoline=Get-OmBytes '72b61d4c4ff0a2461c4d30460021a847606030460021a847a060000c1027852808d0eb2806d0c82804d00827e02801d068281fd1e76086b04ff096400090002001900290049028208df80800adf8107004f1100003900a4d304600216a46a84704f120000390304600216a46a84706b00448206000befde70030012035140020d92700204f494431'
    if([InkTag.OmCodec]::Sha($trampoline) -cne 'cd3063fc7f7095d3aa9a5929e1af94ce150275a135a5a87b8d3aca32f1d6c0c7'){throw 'Identity trampoline binding'}
    $request=[byte[]]::new(64);$nonce=[byte[]]::new(16);$rng=[Security.Cryptography.RandomNumberGenerator]::Create();try{$rng.GetBytes($nonce)}finally{$rng.Dispose()};[Array]::Copy($nonce,0,$request,48,16)
    foreach($x in @(@('identity-loader-RAM.bin',$code),@('identity-trampoline-RAM.bin',$trampoline),@('identity-request.bin',$request))){[IO.File]::WriteAllBytes((Join-Path $Directory $x[0]),$x[1])}
}
function Test-OmPhysicalIdentity([string]$Directory) {
    $result=[IO.File]::ReadAllBytes((Join-Path $Directory 'identity-before.bin'))
    $request=[IO.File]::ReadAllBytes((Join-Path $Directory 'identity-request.bin'))
    if($result.Length -ne 64 -or $request.Length -ne 64 -or
        ![InkTag.OmCodec]::Equal([InkTag.OmCodec]::Slice($request,48,16),[InkTag.OmCodec]::Slice($result,48,16))){
        throw 'Invalid Flash identity response; no write'
    }
    $id=[BitConverter]::ToUInt32($result,4)
    if([BitConverter]::ToUInt32($result,0) -ne 0x3144494f -or $id -ne [BitConverter]::ToUInt32($result,8) -or
        ($id -band 255) -ne 20 -or ($id -shr 16) -notin @(0x85,0xeb,0xc8,0xe0,0x68)){
        throw 'Actual Flash identity/capacity mismatch; 1 MiB required'
    }
    # The RAM reader already samples the manufacturer UID twice.
    $n=16;if(($id -shr 16) -in @(0xe0,0x68)){$n=8}
    $uid=[InkTag.OmCodec]::Slice($result,16,16)
    $sample=[InkTag.OmCodec]::Slice($uid,0,$n)
    $valid=[BitConverter]::ToUInt32($result,12) -eq $n -and
        [InkTag.OmCodec]::Equal($uid,[InkTag.OmCodec]::Slice($result,32,16)) -and
        @($sample|Where-Object {$_ -ne 0}).Count -gt 0 -and @($sample|Where-Object {$_ -ne 255}).Count -gt 0
    if($n -eq 8){$valid=$valid -and [InkTag.OmCodec]::Equal([InkTag.OmCodec]::Slice($uid,8,8),[byte[]]::new(8))}
    return [pscustomobject]@{jedec=$id;uid=$uid;uidValid=$valid}
}
function Invoke-OmNativeFlash($PackageInfo,$BoardConfig,[hashtable]$Options) {
    $tools=Split-Path -Parent $PSScriptRoot
    # Maintained process runner, attach/maintenance gates, loader validation and
    # raw logs are shared with the existing backend. Its Python paths stay unused.
    . (Join-Path $PSScriptRoot 'native-operations.ps1') -JLinkSerial $Options.JLinkSerial -JLinkExe $Options.JLinkExe -FrequencyHz $Options.FrequencyHz -OperationTimeoutSeconds $Options.OperationTimeoutSeconds -WaitForTarget:$Options.RetryDetect
    . (Join-Path $root 'tools/powershell-cli.ps1')
    Remove-Variable -Name registrationDirectory,registrationFile,registrationSHA256,registrationValidated -Scope Local -ErrorAction SilentlyContinue
    $script:registrationDirectory='';$script:registrationFile='';$script:registrationSHA256='';$script:registrationValidated=$false
    $locked=$false;$lock=$null;$writeStarted=$false;$targetAccessStarted=$false;$maintenanceStarted=$false;$ramCaptureStarted=$false
    try {
        if($env:OS -ne 'Windows_NT'){throw 'OM native writer requires Windows'}
        if($JLinkSerial -and $JLinkSerial -notmatch '^[0-9]{4,16}$'){throw 'An exact numeric J-Link serial is required'}
        $JLinkExe=Resolve-InkTagJLinkTool -ToolName JLink.exe -ExplicitPath $JLinkExe -RepoRoot $root;Assert-InkTagJLink788j -JLinkExe $JLinkExe
        $jflashExe=Resolve-InkTagJLinkTool -ToolName JFlash.exe -ExplicitPath (Join-Path (Split-Path -Parent $JLinkExe) 'JFlash.exe') -RepoRoot $root
        $pack=Join-Path $root 'device-pack';$rawLoader=Join-Path $pack 'Devices/Onmicro/OM662XX_nombr.elf'
        $logRoot=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'InkTag/flash-logs/om6626';$operation=Join-Path $logRoot ([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'-'+[Guid]::NewGuid().ToString('N'));$null=New-Item -ItemType Directory -Path $operation
        if($Options.InteractiveFlash){
            Write-InkTagTerminalMessage ((Get-OmFlashText 'Log directory: ' '日志目录：')+$operation)
        }
        $loaderInfo=New-OmNativeLoaders $pack (Join-Path $operation 'loaders');Register-ControlledLoaders
        if($Options.RetryDetect){
            Write-InkTagTerminalMessage (Get-OmFlashText 'Waiting for USB J-Link and OM6626 SWD target. Power-cycle the board if needed. Press Esc to cancel.' '正在等待 USB J-Link 和 OM6626 主板。必要时重新给主板上电，按 Esc 取消。')
        }
        $readDeviceXml=Join-Path $pack 'JLinkDevices.xml';$readProject=Join-Path $operation 'readonly.jflash';$readInputs=New-ControlledReadProject -ToolsDirectory $tools;$readInputs[$readDeviceXml]=$loaderInfo.sourceXMLSHA256.ToUpperInvariant();$readInputs[$rawLoader]=$loaderInfo.originalNoMBRSHA256.ToUpperInvariant()
        $JLinkSerial=Resolve-ControlledProbe $JLinkSerial
        if(!$JLinkSerial){return}
        $lock=[Threading.Mutex]::new($false,('Local\InkTagOMControlled-'+$JLinkSerial));$locked=$lock.WaitOne(0);if(!$locked){throw 'Exact probe is busy'}
        $targetAccessStarted=$true;$detected=Get-ControlledDetection
        if(!$detected){return}
        $null=Get-ControlledBootState $detected
        if($Options.RetryDetect){
            if(Test-ControlledWaitCanceled){
                Write-InkTagTerminalMessage (Get-OmFlashText 'Canceled after SWD contact. Power-cycle the board to resume normal operation.' '已在 SWD 连上后取消。请重新给主板上电以恢复正常运行。')
                return
            }
            Write-InkTagTerminalMessage (Get-OmFlashText 'OM6626 detected. Checking the target before programming.' '已识别 OM6626，正在核验目标设备。')
        }
        $maintenanceStarted=$true
        if($Options.RetryDetect){Enter-ControlledMaintenance $detected}else{Enter-ControlledMaintenance}
        $ramCaptureStarted=$true;$capture=Join-Path $operation 'capture';New-OmIdentityReader $capture $rawLoader;Read-ControlledIdentity $capture 'source-capture' 'before'
        $identity=Test-OmPhysicalIdentity $capture
        $bootFile=Join-Path $operation 'protected-boot.bin';$mbrFile=Join-Path $operation 'runtime-mbr.bin';Read-ControlledRange 'protected-boot' 0x400000 0x403fff $bootFile;Read-ControlledRange 'runtime-mbr' 0x480000 0x481fff $mbrFile
        $beforeBoot=[IO.File]::ReadAllBytes($bootFile);$beforeMbr=[IO.File]::ReadAllBytes($mbrFile)
        $base=0xfd000;if($BoardConfig.board.boardId -eq 4){$base=0xfe000}
        $pair=Read-OmOptionalPair $base 'native-current';$found=[InkTag.OmCodec]::ReadPair($pair)
        if(!$found.Count){$legacy=0xfc000;if($BoardConfig.board.boardId -eq 4){$legacy=0xf9000};$candidate=Read-OmOptionalPair $legacy 'native-legacy';if([InkTag.OmCodec]::ReadPair($candidate).Count){$pair=$candidate}}
        # J-Link reconnects between reads and programming, even on the same probe.
        $confirm=Join-Path $operation 'pre-write';New-OmIdentityReader $confirm $rawLoader;Read-ControlledIdentity $confirm 'pre-write' 'before'
        $finalIdentity=Test-OmPhysicalIdentity $confirm
        if($identity.jedec -ne $finalIdentity.jedec -or ($identity.uidValid -and $finalIdentity.uidValid -and ![InkTag.OmCodec]::Equal($identity.uid,$finalIdentity.uid))){throw 'Actual target changed before write'}
        $selected=Select-OmPreservedRecords $pair $finalIdentity $BoardConfig $PackageInfo
        foreach($warning in $selected.warnings){Write-InkTagTerminalMessage $warning -Level WARNING}
        if(![InkTag.OmCodec]::Address($selected.records[1]) -and $BoardConfig.board.boardId -eq 4 -and $identity.uidValid){$hash=[Security.Cryptography.SHA256]::Create();try{$seed=$hash.ComputeHash([byte[]](72,83)+$identity.uid)}finally{$hash.Dispose()};$selected.records[1]=[InkTag.OmCodec]::Slice($seed,0,5)+[byte[]](210);Write-InkTagTerminalMessage 'Missing old BLE address: existing HS hardware-root address initialized.' -Level WARNING}
        if($BoardConfig.board.boardId -ne 4 -and $PackageInfo.identityInitialization -and
            $finalIdentity.uidValid -and ![InkTag.OmCodec]::Address($selected.records[1])) {
            $selected.records[1]=[InkTag.OmCodec]::ZkcUidAddress($finalIdentity.jedec,$finalIdentity.uid)
        }
        if(![InkTag.OmCodec]::Address($selected.records[1])) {
            # Full must persist the address before deriving its managed links.
            $address=[byte[]]::new(6);$rng=[Security.Cryptography.RandomNumberGenerator]::Create();try{for($attempt=0;$attempt -lt 4;$attempt++){$rng.GetBytes($address);$address[5]=$address[5] -bor 192;if([InkTag.OmCodec]::Address($address)){break}}}finally{$rng.Dispose()};if(![InkTag.OmCodec]::Address($address)){throw 'SDK random address generation failed'};$selected.records[1]=$address
            Write-InkTagTerminalMessage 'Missing old BLE address: a fresh persistent static-random address is initialized for this installation.' -Level WARNING
        }
        $address=$selected.records[1]
        $link='https://solar2ain.github.io/ink-tag/?ble=IKT_OM_{0:X2}{1:X2}' -f $address[1],$address[0]
        $settings=New-OmSettings $BoardConfig $selected.records $link -SeedNfc:(!$PackageInfo.baseProfile)
        $stages=New-OmStageImages $PackageInfo $BoardConfig $settings (Join-Path $operation 'prepared')
        $expectedBoot=[InkTag.OmCodec]::ExpectedBoot($beforeBoot,$PackageInfo.recovery,$PackageInfo.application)
        $latch=Join-Path $operation 'WRITE-STARTED-NO-REPLAY.json';$stream=[IO.File]::Open($latch,'CreateNew','Write');try{$b=[Text.Encoding]::UTF8.GetBytes('{"status":"unknown","automaticReplay":false}');$stream.Write($b,0,$b.Length);$stream.Flush($true)}finally{$stream.Dispose()};$writeStarted=$true
        $names=@('factory-state','factory-code','factory-device-info')
        for($i=0;$i -lt 3;$i++){$name=$names[$i];$stage=$stages[$name];if([InkTag.OmCodec]::Sha([IO.File]::ReadAllBytes($stage.path)) -cne $stage.sha256){throw 'Prepared stage changed; no replay'};foreach($v in $loaderInfo.files.PSObject.Properties){if((Get-FileHash -LiteralPath $v.Name -Algorithm SHA256).Hash -ne $v.Value){throw 'Native loader changed; no replay'}};$commands=@(Get-ControlledConnection $loaderInfo.devices[$i]);$commands+='speed '+[int]($FrequencyHz/1000);if($i -eq 1){$commands+='erase 0x00404000 0x00477FFF'};$commands+='loadfile "'+$stage.path+'"';$written=Invoke-ControlledNative $name $commands $loaderInfo.devices[$i] -Programming;if($written -notmatch '(?im)^J-Link: Flash download:|flash contents already match'){throw 'Missing native programming completion; no success/replay'}}
        $expectedCode=[InkTag.OmCodec]::Fill(0x74000);[Array]::Copy($PackageInfo.recovery,0,$expectedCode,0,$PackageInfo.recovery.Length);[Array]::Copy($PackageInfo.application,0,$expectedCode,0x26000,$PackageInfo.application.Length)
        $expectedState=[InkTag.OmCodec]::Fill($base+8192-0x82000);[Array]::Copy([InkTag.OmCodec]::NativeMarker($PackageInfo.layout),0,$expectedState,0,32);[Array]::Copy($settings,0,$expectedState,$base-0x82000,8192)
        $checks=@(@('code',0x404000,$expectedCode),@('fonts',0x478000,[InkTag.OmCodec]::Fill(32768)),@('state',0x482000,$expectedState),@('protected-boot',0x400000,$expectedBoot),@('runtime-mbr',0x480000,$beforeMbr))
        foreach($check in $checks){$actual=Join-Path $operation ('verify-'+$check[0]+'.bin');Read-ControlledRange ('verify-'+$check[0]) $check[1] ($check[1]+$check[2].Length-1) $actual -WholeRead;if(![InkTag.OmCodec]::Equal([IO.File]::ReadAllBytes($actual),$check[2])){throw ('Readback mismatch: '+$check[0]+'; no success/replay')}}
        Clear-ControlledHistoryMailbox ([pscustomobject]@{address=0x20001fc0;bytes=44})
        $result=@{status='write-verified';generated=$true;written=$true;readbackMatched=$true;mode='preserve';firmwareSha256=$PackageInfo.sha256;board=$BoardConfig.marking;scope='mcu/main-flash';protectedConfSkipped=($base -eq 0xfd000);externalFlashAccessed=$false;externalImagesRetained=($BoardConfig.board.boardId -eq 2);proPreserved=$selected.proPreserved;addressPreserved=$selected.addressPreserved;oldOptionalWarnings=$selected.warnings;managedLink=$link;nfcLinkInitialized=(!$PackageInfo.baseProfile);automaticReplay=$false;opticalQualified=$false;operation=$operation}
        $result|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $operation 'WRITE-VERIFIED.json') -Encoding UTF8
        if($selected.proPreserved){
            Write-InkTagTerminalMessage (Get-OmFlashText 'Saved Pro record retained. The Pro App verifies activation at startup.' '已保留原 Pro 授权记录。Pro 固件会在启动后验证授权。')
        }
        Write-InkTagTerminalMessage (Get-OmFlashText 'Flash verified. Fully power-cycle the board.' '刷机和回读校验完成，请将主板完全断电后重新上电。') -Level SUCCESS
        return [pscustomobject]$result
    } catch {
        if($operation){@{status='failed';written=$writeStarted;readbackMatched=$false;automaticReplay=$false;error=$_.Exception.Message}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $operation 'FAILED.json') -Encoding UTF8}
        if($writeStarted){Write-InkTagTerminalMessage 'A write was attempted; outcome may be partial. No automatic replay, resume or rollback.' -Level WARNING}
        throw
    } finally {Remove-ControlledRegistration;if($locked){$lock.ReleaseMutex()};if($lock){$lock.Dispose()};Restore-InkTagTerminalColour $terminalState}
}
function Read-OmOptionalPair([int]$Address,[string]$Stem) {
    $path=Join-Path $operation ($Stem+'.bin')
    try {Read-ControlledRange $Stem (0x400000+$Address) (0x400000+$Address+8191) $path;$bytes=[IO.File]::ReadAllBytes($path);if($bytes.Length -ne 8192){throw 'Short optional native pair'};return ,$bytes}
    catch {Write-InkTagTerminalMessage ('Optional old native records unavailable: '+$_.Exception.Message) -Level WARNING;return $null}
}
function Select-OmPreservedRecords($Pair,$Identity,$Config,$Firmware) {
    $warnings=[Collections.Generic.List[string]]::new();$records=@{};$old=[InkTag.OmCodec]::ReadPair($Pair)
    if(!$old.Count){if($null -eq $Pair){$warnings.Add('Old optional native records unavailable; defaults selected')}elseif(@($Pair|Where-Object {$_ -ne 255}).Count -eq 0){$warnings.Add('Old native pair is blank; defaults selected')}else{$warnings.Add('Old native framing is invalid; defaults selected')}}
    foreach($tag in $old.Keys){$v=$old[$tag];if($null -eq $v){$warnings.Add("Invalid/deleted old tag $tag treated as absent");continue};if($tag -eq 1){if([InkTag.OmCodec]::Address($v)){$records[1]=$v}else{$warnings.Add('Invalid old BLE address; Full will initialize a new address')}}elseif($tag -ge 3 -and $tag -le 8){if($v.Length -eq 1){$records[$tag]=$v}else{$warnings.Add("Invalid ordinary SDK parameter $tag; SDK default used")}}elseif($tag -lt 100 -and $tag -notin @(2,9)){$records[$tag]=$v}}
    if($old.ContainsKey(111) -and $old[111]) {
        $v=$old[111];try {
            if(!$Firmware.baseProfile -and !(Test-OmContains $Firmware.raw (Get-OmBytes '6cef365a284c497a548f93eee2397fe43673a2499c2494a52241122dbd17944278341110bb317e5ba5d7f49e71d71bcfb14731327643156e5a23ec9261d24b3b'))){throw 'Selected Pro App does not bind the existing activation public key'}
            if($v.Length -ne 108 -or ![InkTag.OmCodec]::Equal([InkTag.OmCodec]::Slice($v,0,8),(Get-OmBytes '4941433101010000')) -or [BitConverter]::ToUInt32($v,104) -ne [InkTag.OmCodec]::Crc32([InkTag.OmCodec]::Slice($v,0,104))){throw 'Old Pro native record is damaged'}
            # ZK's Pro App checks the signed record against its own NFC UID.
            $null=Test-InkTagActivationToken -Token ([InkTag.OmCodec]::Slice($v,8,96))
            $records[111]=$v
        }catch{$warnings.Add($_.Exception.Message)}
    }
    return [pscustomobject]@{records=$records;warnings=$warnings.ToArray();proPreserved=($null -ne $records[111]);addressPreserved=[InkTag.OmCodec]::Address($records[1])}
}
