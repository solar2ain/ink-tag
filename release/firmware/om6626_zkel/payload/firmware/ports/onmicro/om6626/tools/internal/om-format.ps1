. (Join-Path $PSScriptRoot 'om-codec.ps1')
Add-Type -AssemblyName System.IO.Compression
$script:OmContract=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'om-contract.json') -Raw | ConvertFrom-Json
function Get-OmBytes([string]$Hex) {
    if($Hex.Length%2 -or $Hex -match '[^0-9a-fA-F]'){throw 'Invalid hex'}
    $b=[byte[]]::new($Hex.Length/2);for($i=0;$i -lt $b.Length;$i++){$b[$i]=[Convert]::ToByte($Hex.Substring(2*$i,2),16)};return ,$b
}
function Assert-OmFields($Object,[string[]]$Names) {
    if($Object -is [Collections.IDictionary]){$keys=@($Object.Keys)}else{$keys=@($Object.PSObject.Properties.Name)}
    if(($keys|Sort-Object) -join ',' -cne (($Names|Sort-Object) -join ',')){throw ('Unexpected configuration fields: '+($keys -join ','))}
}
function Convert-OmMap($Object) {
    if($null -eq $Object){return $null}
    if($Object -is [Management.Automation.PSCustomObject]){$h=@{};foreach($p in $Object.PSObject.Properties){$h[$p.Name]=Convert-OmMap $p.Value};return $h}
    if($Object -is [array]){return ,@($Object|ForEach-Object {Convert-OmMap $_})};return $Object
}
function Read-OmConfig([string]$Path) {
    $file=Get-Item -LiteralPath $Path -ErrorAction Stop;if($file.Length -gt 65536){throw 'Configuration exceeds 64 KiB'}
    $text=[IO.File]::ReadAllText($file.FullName);$data=@{}
    if($file.Extension -eq '.json'){$data=Convert-OmMap ($text|ConvertFrom-Json)}
    elseif($file.Extension -eq '.toml') {
        $scope=$data
        foreach($line in ($text -split '\r?\n')) {
            $line=($line -replace '\s*#.*$','').Trim();if(!$line){continue}
            if($line -match '^\[([a-z_]+(?:\.[a-z_]+)*)\]$'){$scope=$data;foreach($key in ($Matches[1] -split '\.')){if(!$scope.ContainsKey($key)){$scope[$key]=@{}};if($scope[$key] -isnot [hashtable]){throw 'Invalid TOML section'};$scope=$scope[$key]};continue}
            if($line -notmatch '^([a-z_][a-z0-9_]*)\s*=\s*(.+)$'){throw 'Unsupported TOML syntax; use the maintained preset or JSON'}
            $key=$Matches[1];$value=$Matches[2].Trim();if($scope.ContainsKey($key)){throw 'Duplicate TOML key'}
            if($value -match '^"([^"\\]*)"$'){$v=$Matches[1]}
            elseif($value -match '^(true|false)$'){$v=$value -eq 'true'}
            elseif($value -match '^(0|[1-9][0-9]*)$'){$v=[long]::Parse($value)}
            elseif($value -match '^\[(?:\s*"[a-z_]+"\s*(?:,\s*"[a-z_]+"\s*)*)?\]$'){$v=@([regex]::Matches($value,'"([a-z_]+)"')|ForEach-Object {$_.Groups[1].Value})}
            else {throw 'Unsupported TOML value; use JSON for other escaped values'}
            $scope[$key]=$v
        }
    } else {throw 'Native no-Python entry accepts TOML/JSON. The existing Python backend retains YAML support.'}
    Assert-OmFields $data @('format_version','baseline','board_marking','screen','gpio')
    if($data.format_version -is [bool] -or ($data.format_version -isnot [int] -and $data.format_version -isnot [long]) -or $data.format_version -ne 1 -or $data.baseline -cne 'inktag-release-v1'){throw 'Unsupported configuration schema'}
    $board=$script:OmContract.boards.PSObject.Properties[$data.board_marking].Value;if(!$board){throw 'Explicit maintained PCB marking required'}
    $s=$data.screen;Assert-OmFields $s @('ic','recipe','color','rotation','mirror_x','mirror_y','native_ui_scale','width','height','visible_x','visible_y','visible_width','visible_height')
    $driver=$script:OmContract.controllers.PSObject.Properties[$s.ic].Value;$color=$script:OmContract.colors.PSObject.Properties[$s.color].Value
    if($null -eq $driver -or $null -eq $color){throw 'Unknown controller/color'}
    foreach($n in @('recipe','rotation','native_ui_scale','width','height','visible_x','visible_y','visible_width','visible_height')){if($s[$n] -is [bool] -or ($s[$n] -isnot [int] -and $s[$n] -isnot [long]) -or $s[$n] -lt 0 -or $s[$n] -gt 65535){throw 'Screen fields must be unsigned integers'}}
    if($s.recipe -gt 255 -or $s.native_ui_scale -gt 5 -or $s.rotation -notin @(0,90,180,270) -or $s.mirror_x -isnot [bool] -or $s.mirror_y -isnot [bool]){throw 'Invalid recipe/transform/scale'}
    $ruleKey=[string]([int]$driver*256+[int]$s.recipe);$rule=$script:OmContract.rules.PSObject.Properties[$ruleKey].Value
    if(!$rule -or $color -notin $rule.colors -or $s.width -lt 1 -or $s.height -lt 1 -or $s.width -gt $rule.max[0] -or $s.height -gt $rule.max[1] -or $s.width%$rule.align){throw 'Screen controller/geometry admission failed'}
    if($s.visible_width -lt 1 -or $s.visible_height -lt 1 -or $s.visible_x+$s.visible_width -gt $s.width -or $s.visible_y+$s.visible_height -gt $s.height){throw 'Invalid visible viewport'}
    if($rule.full_viewport -and ($s.visible_x -or $s.visible_y -or $s.visible_width -ne $s.width -or $s.visible_height -ne $s.height)){throw 'Recipe requires full viewport'}
    foreach($sizes in @($rule.fixed_sizes,$rule.color_fixed_sizes.PSObject.Properties[[string]$color].Value)) {
        if($sizes -and !(@($sizes|Where-Object {$_[0] -eq $s.width -and $_[1] -eq $s.height}).Count)){throw 'Recipe dimensions mismatch'}
    }
    if($null -eq $script:OmContract.busy.PSObject.Properties[$ruleKey]){throw 'Unknown BUSY recipe semantics'}
    $wire=[byte[]]::new(17);$wire[0]=1;$wire[1]=$driver;$wire[2]=$s.recipe;$wire[3]=$color
    $wire[4]=[int]($s.rotation/90) -bor $(if($s.mirror_x){4}else{0}) -bor $(if($s.mirror_y){8}else{0}) -bor ([int]$s.native_ui_scale -shl 4)
    $i=5;foreach($n in @('width','height','visible_x','visible_y','visible_width','visible_height')){[Array]::Copy([BitConverter]::GetBytes([UInt16]$s[$n]),0,$wire,$i,2);$i+=2}
    $g=$data.gpio;Assert-OmFields $g @('display','led','aux');Assert-OmFields $g.display @('mosi','clock','cs','dc','reset','busy','enable','enable_active_low');Assert-OmFields $g.led @('red','green','blue','active_low');Assert-OmFields $g.aux @('txd','rxd','active_low')
    $defaults=@(21,15,22,20,16,3,7,4,18,17,255,255);if($board.boardId -eq 4){$defaults=@(21,15,22,2,16,20,7,8,9,10,255,255)}
    $pins=[byte[]]::new(16);$pins[0]=1;$index=0;$low=0;$used=@{}
    foreach($group in @(@('display',@('mosi','clock','cs','dc','reset','busy','enable')),@('led',@('red','green','blue')),@('aux',@('txd','rxd')))) {
        $block=$g[$group[0]];$roles=$group[1]
        if($group[0] -eq 'display'){if($block.enable_active_low -isnot [bool]){throw 'enable_active_low must be boolean'};$polarity=@();if($block.enable_active_low){$polarity=@('enable')}}
        else {$polarity=@($block.active_low);if(($polarity|Select-Object -Unique).Count -ne $polarity.Count -or @($polarity|Where-Object {$_ -notin $roles}).Count){throw 'Invalid active_low list'}}
        foreach($role in $roles) {
            $value=$block[$role];if($value -ceq 'disabled'){$pin=255}elseif($value -cmatch '^GPIO([0-9]|1[0-9]|2[0-5])$'){$pin=[int]$Matches[1]}else{throw 'Invalid GPIO value'}
            $allowed=@($defaults[$index]);if($index -in @(3,5)){$allowed=@($defaults[3],$defaults[5])};if($index -in @(7,8)){$allowed=@($defaults[7],$defaults[8],255)};if($index -eq 9){$allowed=@($defaults[9],255)}
            if($pin -notin $allowed -or ($pin -ne 255 -and $used.ContainsKey($pin))){throw 'PCB routing role/admission mismatch'}
            if($pin -ne 255){$used[$pin]=$true};if($role -in $polarity){$low=$low -bor (1 -shl $index);if($pin -eq 255){throw 'Disconnected role polarity'}}
            $pins[4+$index]=$pin;$index++
        }
    }
    if($low -band (-bnot 0x3c0)){throw 'PCB polarity mask'};[Array]::Copy([BitConverter]::GetBytes([UInt16]$low),0,$pins,2,2)
    return [pscustomobject]@{board=$board;marking=$data.board_marking;screen=$wire;pins=$pins;configSha256=[InkTag.OmCodec]::Sha([IO.File]::ReadAllBytes($file.FullName))}
}
function Read-OmArchive([byte[]]$Bytes,[hashtable]$Limits) {
    if($Bytes.Length -gt 2097152){throw 'Archive too large'}
    $memory=[IO.MemoryStream]::new($Bytes,$false);$archive=[IO.Compression.ZipArchive]::new($memory,[IO.Compression.ZipArchiveMode]::Read);$files=@{}
    try {
        if($archive.Entries.Count -ne $Limits.Count){throw 'Archive member count'}
        foreach($e in $archive.Entries){$name=$e.FullName;if($name -cnotin @($Limits.Keys) -or $files.ContainsKey($name) -or $e.Length -le 0 -or $e.Length -gt $Limits[$name]){throw 'Archive member name/size/duplicate'}
            $stream=$e.Open();$out=[IO.MemoryStream]::new();try{$buffer=[byte[]]::new(8192);while(($n=$stream.Read($buffer,0,$buffer.Length)) -gt 0){$out.Write($buffer,0,$n);if($out.Length -gt $Limits[$name]){throw 'Expanded archive exceeds bound'}};if($out.Length -ne $e.Length){throw 'Archive length mismatch'};$files[$name]=$out.ToArray()}finally{$stream.Dispose();$out.Dispose()}
        }
    } finally {$archive.Dispose();$memory.Dispose()};return $files
}
function Convert-OmJson([byte[]]$Bytes){return ([Text.UTF8Encoding]::new($false,$true).GetString($Bytes)|ConvertFrom-Json)}
function Test-OmContains([byte[]]$Bytes,[byte[]]$Pattern){return [InkTag.OmCodec]::Find($Bytes,$Pattern) -ge 0}
function Read-OmFirmware([string]$Path,[byte[]]$FixturePublicKey=$null) {
    $file=Get-Item -LiteralPath $Path -ErrorAction Stop;if($file.Length -gt 2097152){throw 'Full package too large'}
    $full=[IO.File]::ReadAllBytes($file.FullName);$components=$file.Extension -ceq '.json';$inputFiles=@{'source-full.zip'=$file.FullName}
    if($components) {
        if($full.Length -gt 8192){throw 'Component manifest too large'};$c=Convert-OmJson $full
        Assert-OmFields $c @('format','sourceProfile','family','layout','epoch','recoveryVersion','images')
        if($c.format -cne 'inktag-om-components-v1' -or $c.sourceProfile -cne 'base' -or $c.layout -cne 'layout-v1' -or $c.epoch -ne 1){throw 'Self-built Basic component schema mismatch'}
        Assert-OmFields $c.images @('app-ota.zip','recovery.bin');$parts=@{};$inputFiles=@{'components.json'=$file.FullName}
        foreach($name in @('app-ota.zip','recovery.bin')){$r=$c.images.PSObject.Properties[$name].Value;if($r.file -isnot [string] -or $r.file -notmatch '^[A-Za-z0-9_.-]+$' -or $r.file -in @('.','..','components.json')){throw 'Component filename must be a local basename'};$limit=155648;if($name -ceq 'app-ota.zip'){$limit=524288};if($r.bytes -lt 1 -or $r.bytes -gt $limit){throw 'Component bounds'};$path=Join-Path $file.DirectoryName $r.file;$bytes=[IO.File]::ReadAllBytes($path);if($bytes.Length -ne $r.bytes -or [InkTag.OmCodec]::Sha($bytes) -cne $r.sha256){throw 'Component hash/length mismatch'};$parts[$name]=$bytes;$inputFiles[$r.file]=$path}
        $meta=[pscustomobject]@{format='inktag-om-full-base-v1';baseline='inktag-release-v1';mcu='om6626';family=$c.family;layout=$c.layout;epoch=$c.epoch;config_schema=1;execution_qualified=$false;images=[pscustomobject]@{}}
        $full=$full+$parts['app-ota.zip']+$parts['recovery.bin']
    } else {$parts=Read-OmArchive $full @{'base.json'=8192;'app-ota.zip'=524288;'recovery.bin'=155648;'factory-init.omfi'=336000};$meta=Convert-OmJson $parts['base.json']}
    Assert-OmFields $meta @('format','baseline','mcu','family','layout','epoch','config_schema','images','execution_qualified')
    if($meta.format -notin @('inktag-om-full-base-v1','inktag-om-full-v1') -or $meta.baseline -cne 'inktag-release-v1' -or $meta.mcu -cne 'om6626' -or $meta.layout -cne 'layout-v1' -or $meta.epoch -ne 1 -or $meta.config_schema -ne 1 -or $meta.execution_qualified -ne $false -or $meta.family -cne 'om6626_zkc'){throw 'Full schema/family/epoch mismatch'}
    if(!$components){Assert-OmFields $meta.images @('app-ota.zip','recovery.bin','factory-init.omfi')
    foreach($name in @('app-ota.zip','recovery.bin','factory-init.omfi')){$binding=$meta.images.PSObject.Properties[$name].Value;if($binding.bytes -ne $parts[$name].Length -or $binding.sha256 -cne [InkTag.OmCodec]::Sha($parts[$name])){throw 'Full image binding mismatch'}}}
    $ota=Read-OmArchive $parts['app-ota.zip'] @{'manifest.json'=8192;'application.bin'=319488};$m=Convert-OmJson $ota['manifest.json'];$app=$ota['application.bin'];$rec=$parts['recovery.bin'];$base=$meta.format -ceq 'inktag-om-full-base-v1'
    $board=0x42584d4f;$layout=0x31565a4f;$boardName='om6626_zkcxxb'
    $bodySize=$app.Length;if(!$base){$bodySize-=96}
    if($m.format -cne 'inktag-ota-v1' -or $m.transport -cne 'onmicro-dfu-v3' -or $m.chip -cne 'OM6626' -or $m.board -cne $boardName -or $m.layout -cne 'layout-v1' -or $m.compatibilityEpoch -ne 1 -or $m.application.file -cne 'application.bin' -or $m.application.size -ne $bodySize -or $m.application.transfer_size -ne $app.Length -or $m.application.sha256 -cne [InkTag.OmCodec]::Sha($app) -or $m.application.image_type -ne 16 -or $m.application.active_address -ne 0x2a000 -or $m.application.slot_size -ne 319488 -or ($m.application.recovery_state_addresses -join ',') -cne '532480,536576' -or $m.application.recovery_request_storage -cne 'transaction-journal'){throw 'App manifest/context mismatch'}
    if($base){if($m.signature.required -ne $false -or $m.signature.algorithm -cne 'none' -or $m.signature.policy -cne 'crc-only' -or $m.sourceProfile -cne 'base'){throw 'Basic signature policy mismatch'};$body=$app}
    else {
        $sig=$m.signature;$pub=Get-OmBytes $sig.public_key
        if($FixturePublicKey){$pub=$FixturePublicKey}elseif($sig.public_key_hash -cne 'fc0e79e61ce05df76dcff00bf122e86e' -or ([InkTag.OmCodec]::Sha($pub)).Substring(0,32) -cne $sig.public_key_hash){throw 'App publisher trust root mismatch'}
        if($sig.required -ne $true -or $sig.algorithm -cne 'ecdsa-p256-sha256' -or $sig.key_id -cne 'inktag-om6626-ota-v1' -or $sig.trust_profile -cne 'production' -or $sig.signed_context_size -ne 16 -or $sig.signed_prefix_size -ne 32 -or $sig.signed_trailer_size -ne 96 -or $sig.signed_board -cne $boardName -or $sig.signed_layout -cne 'layout-v1' -or $sig.board_signature -ne $board -or $sig.layout_signature -ne $layout -or $sig.verifier_signature -ne 0x314b4d4f -or $app.Length -lt 404){throw 'App signature profile mismatch'}
        $at=$app.Length-96
        if([BitConverter]::ToUInt32($app,$at) -ne 0x31534b49 -or $app[$at+4] -ne 1 -or $app[$at+5] -ne 1 -or [BitConverter]::ToUInt16($app,$at+6) -ne 96 -or [BitConverter]::ToUInt32($app,$at+8) -ne $at -or [BitConverter]::ToUInt32($app,$at+12) -ne 1 -or [BitConverter]::ToUInt32($app,$at+16) -ne $board -or [BitConverter]::ToUInt32($app,$at+20) -ne $layout -or [BitConverter]::ToUInt32($app,$at+24) -ne 0x314b4d4f -or [BitConverter]::ToUInt32($app,$at+28) -ne $m.releaseVersion -or ![InkTag.OmCodec]::P256($pub,[InkTag.OmCodec]::Slice($app,0,$at+32),[InkTag.OmCodec]::Slice($app,$at+32,64))){throw 'App publisher signature/context verification failed'}
        $body=[InkTag.OmCodec]::Slice($app,0,$at)
    }
    $at=$body.Length-40
    if(![InkTag.OmCodec]::Integrity($body,$board,$layout,$m.releaseVersion)){throw 'IKCI integrity/context/ambiguity mismatch'}
    if([BitConverter]::ToUInt32($body,4) -ne 0x404101 -or [BitConverter]::ToUInt32($body,256) -ne 0x314c4d4f -or [BitConverter]::ToUInt32($body,264) -ne 0xb007a55aL){throw 'Native App entry routing mismatch'}
    $raw=[InkTag.OmCodec]::Slice($body,0,$at);[Array]::Copy($raw,260,$raw,4,4)
    $capacity=[byte[]](Get-OmBytes '494b525001001400')+[BitConverter]::GetBytes([UInt32]$layout)+[BitConverter]::GetBytes([UInt32]319392)+[BitConverter]::GetBytes([UInt32]319488)+(Get-OmBytes '494b53500102')
    $dfu=[BitConverter]::GetBytes([UInt32]16)+[BitConverter]::GetBytes([UInt32]0x2a000)+[BitConverter]::GetBytes([UInt32]0x2a000)+[BitConverter]::GetBytes([UInt32]0x4e000)
    $releaseRoute=[Text.Encoding]::ASCII.GetBytes("INKTAG-OM-RELEASE-V1`0")
    if($rec.Length -lt 268 -or $rec.Length -gt 155648 -or [BitConverter]::ToUInt32($rec,4) -lt 0x404001 -or [BitConverter]::ToUInt32($rec,4) -ge 0x42a000 -or [InkTag.OmCodec]::Count($rec,$capacity) -ne 1 -or [InkTag.OmCodec]::Count($rec,$dfu) -ne 1 -or !(Test-OmContains $raw $releaseRoute) -or !(Test-OmContains $rec $releaseRoute)){throw 'Common Recovery native routing/capacity/CRC policy mismatch'}
    if(!$components){$capsule=$parts['factory-init.omfi'];$n=[BitConverter]::ToUInt32($capsule,8);$size=[BitConverter]::ToUInt32($capsule,12);$tail=64;if($base){$tail=4}
    if($n -lt 1 -or $n -gt 8192 -or $size -lt 1 -or $size -gt 319392 -or $capsule.Length -ne 16+$n+$size+$tail){throw 'Factory capsule bounds'}
    if($base){if(![InkTag.OmCodec]::Equal([InkTag.OmCodec]::Slice($capsule,0,8),(Get-OmBytes '494b4f4d46553100')) -or [BitConverter]::ToUInt32($capsule,$capsule.Length-4) -ne [InkTag.OmCodec]::Crc32([InkTag.OmCodec]::Slice($capsule,0,$capsule.Length-4))){throw 'Basic capsule integrity'}}
    else {if(![InkTag.OmCodec]::Equal([InkTag.OmCodec]::Slice($capsule,0,8),(Get-OmBytes '494b4f4d46493100')) -or ![InkTag.OmCodec]::CanonicalSignature([InkTag.OmCodec]::Slice($capsule,$capsule.Length-64,64)) -or ![InkTag.OmCodec]::P256($pub,([Text.Encoding]::ASCII.GetBytes("InkTag OM factory-init host-replay v1`0")+[InkTag.OmCodec]::Slice($capsule,0,$capsule.Length-64)),[InkTag.OmCodec]::Slice($capsule,$capsule.Length-64,64))){throw 'Factory purpose signature failed'}}
    $cm=Convert-OmJson ([InkTag.OmCodec]::Slice($capsule,16,$n));$purpose='InkTag factory-init host-replay';if($base){$purpose='InkTag base factory-init host-replay'}
    if($cm.purpose -cne $purpose -or $cm.version -ne 1 -or $cm.mcu -cne 'om6626' -or $cm.family -cne $meta.family -or $cm.layout -cne 'layout-v1' -or $cm.epoch -ne 1 -or $cm.layout_signature -ne $layout -or $cm.app_raw_sha256 -cne [InkTag.OmCodec]::Sha($raw) -or $cm.app_signed_sha256 -cne [InkTag.OmCodec]::Sha($app) -or $cm.recovery_sha256 -cne [InkTag.OmCodec]::Sha($rec) -or $cm.code_sha256 -cne [InkTag.OmCodec]::Sha([InkTag.OmCodec]::Slice($capsule,16+$n,$size))){throw 'Factory capsule native pairing mismatch'}
    }
    return [pscustomobject]@{meta=$meta;manifest=$m;raw=$raw;application=$app;recovery=$rec;layout=$layout;baseProfile=$base;sha256=[InkTag.OmCodec]::Sha($full);inputFiles=$inputFiles;components=$components;identityInitialization=((Test-OmContains $raw (Get-OmBytes '494b494401010000')) -and (Test-OmContains $rec (Get-OmBytes '494b494401010000')))}
}
function New-OmSettings($Config,[Collections.IDictionary]$Preserved,[string]$Link,[switch]$SeedNfc) {
    $tags=[Collections.Generic.List[int]]::new();$values=[Collections.Generic.List[byte[]]]::new()
    foreach($tag in @($Preserved.Keys|Sort-Object)){if($null -ne $Preserved[$tag] -and (($tag -lt 100 -and $tag -notin @(2,9)) -or $tag -eq 111)){$tags.Add([int]$tag);$values.Add([byte[]]$Preserved[$tag])}}
    foreach($r in @(@(108,[InkTag.OmCodec]::FactoryRecord($Config.board.boardId,$Config.pins)),@(104,(Get-OmBytes $script:OmContract.defaultPreferences)),@(100,[InkTag.OmCodec]::LegacyPins($Config.pins)),@(101,[InkTag.OmCodec]::ScreenRecord($Config.screen)))){$tags.Add($r[0]);$values.Add([byte[]]$r[1])}
    # SDK NVDS_TAG_USER_BEGIN is 100. ZKC uses +3 for the NFC binding and +6 for OQR1.
    if($SeedNfc){$tags.Add(103);$values.Add([InkTag.OmCodec]::NfcLinkRecord($Link))}
    $tags.Add(106);$values.Add([InkTag.OmCodec]::DeviceQrRecord($Link))
    return ,([InkTag.OmCodec]::Pair($tags.ToArray(),$values.ToArray()))
}
function Write-OmHex([string]$Path,$Segments) {
    $lines=[Collections.Generic.List[string]]::new();$upper=-1
    foreach($s in $Segments) {
        [long]$address=$s.address;[byte[]]$data=$s.data
        for($i=0;$i -lt $data.Length;$i+=16) {
            $at=$address+$i;$hi=[int]($at -shr 16)
            if($hi -ne $upper){$record=[byte[]]@(2,0,0,4,($hi -shr 8),($hi -band 255));$sum=0;foreach($b in $record){$sum+=$b};$lines.Add(':'+([BitConverter]::ToString($record).Replace('-',''))+('{0:X2}' -f ((256-($sum%256))%256)));$upper=$hi}
            $n=[Math]::Min(16,$data.Length-$i);$record=[byte[]]@($n,(($at -shr 8)-band 255),($at-band 255),0)+[InkTag.OmCodec]::Slice($data,$i,$n);$sum=0;foreach($b in $record){$sum+=$b}
            $lines.Add(':'+([BitConverter]::ToString($record).Replace('-',''))+('{0:X2}' -f ((256-($sum%256))%256)))
        }
    };$lines.Add(':00000001FF');[IO.File]::WriteAllLines($Path,$lines,[Text.Encoding]::ASCII)
}
function New-OmStageImages($Firmware,$Config,[byte[]]$Settings,[string]$Output) {
    if($Settings.Length -ne 8192){throw 'Native settings pair length'}
    $base=0xfd000;if($Config.board.boardId -eq 4){$base=0xfe000};$root=[IO.Path]::GetFullPath($Output);$null=New-Item -ItemType Directory -Path $root -ErrorAction Stop
    $marker=[InkTag.OmCodec]::Fill(8192);[Array]::Copy([InkTag.OmCodec]::NativeMarker($Firmware.layout),0,$marker,0,32)
    $stages=@(@{name='factory-state';segments=@(@{address=0x472000;data=[InkTag.OmCodec]::Fill(0xe000)},@{address=0x482000;data=[InkTag.OmCodec]::Fill($base-0x82000)})},@{name='factory-code';segments=@(@{address=0x404000;data=$Firmware.recovery},@{address=0x42a000;data=$Firmware.application})},@{name='factory-device-info';segments=@(@{address=0x482000;data=$marker},@{address=0x400000+$base;data=$Settings})})
    $out=@{};foreach($stage in $stages){$path=Join-Path $root ($stage.name+'.hex');Write-OmHex $path $stage.segments;$out[$stage.name]=@{path=$path;sha256=[InkTag.OmCodec]::Sha([IO.File]::ReadAllBytes($path));segments=$stage.segments}}
    return $out
}
