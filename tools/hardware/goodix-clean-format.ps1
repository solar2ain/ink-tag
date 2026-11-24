# Pure GR Full/configuration codecs; no probe, CLI, Python or signing calls.
# SDK/format-specific rules are checked against the maintained Python oracle.
if (-not ('InkTag.GoodixBytes' -as [type])) { Add-Type -Path (Join-Path $PSScriptRoot 'goodix-clean-bytes.cs') }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$script:GrCleanRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
function Assert-Gr { param([bool]$Ok,[string]$Message) [InkTag.GoodixBytes]::Require($Ok,$Message) }
function Get-GrJson {
    param([byte[]]$Bytes,[switch]$AllowBom)
    $text=(New-Object Text.UTF8Encoding($false,$true)).GetString($Bytes)
    if($AllowBom -and $text.Length -and $text[0] -eq [char]0xfeff){$text=$text.Substring(1)}
    [InkTag.GoodixBytes]::UniqueJsonKeys($text)
    $text|ConvertFrom-Json
}
function Get-GrRecord { param([byte[]]$Bytes) @{bytes=$Bytes.Length;sha256=[InkTag.GoodixBytes]::Sha($Bytes)} }
function Assert-GrRecord { param([byte[]]$Bytes,$Record) Assert-Gr ($Bytes.Length -eq $Record.bytes -and [InkTag.GoodixBytes]::Sha($Bytes) -ceq $Record.sha256) 'Payload hash/size mismatch' }
function Assert-GrKeys {
    param($Value,[string[]]$Keys)
    $actual=if($Value -is [Collections.IDictionary]){@($Value.Keys)}else{@($Value.PSObject.Properties.Name)}
    Assert-Gr (($actual.Count -eq $Keys.Count) -and !(Compare-Object ($actual|Sort-Object) ($Keys|Sort-Object) -CaseSensitive)) 'Unexpected/missing fields'
}
function ConvertTo-GrCanonicalJson {
    param($Value)
    if($null -eq $Value){return 'null'}
    if($Value -is [string]){return [InkTag.GoodixBytes]::JsonString($Value)}
    if($Value -is [bool]){if($Value){return 'true'}else{return 'false'}}
    if($Value -is [int] -or $Value -is [long]){return $Value.ToString([Globalization.CultureInfo]::InvariantCulture)}
    if($Value -is [array]){$parts=@(foreach($item in $Value){ConvertTo-GrCanonicalJson $item});return '['+($parts -join ',')+']'}
    Assert-Gr ($Value -is [pscustomobject]) 'Unsupported authenticated JSON value'
    [string[]]$keys=@($Value.PSObject.Properties.Name);[Array]::Sort($keys,[StringComparer]::Ordinal)
    $parts=@(foreach($key in $keys){([InkTag.GoodixBytes]::JsonString($key))+':'+(ConvertTo-GrCanonicalJson $Value.$key)})
    return '{'+($parts -join ',')+'}'
}
function Read-GrArchive {
    param([byte[]]$Bytes,[hashtable]$Limits)
    Assert-Gr ($Bytes.Length -gt 0 -and $Bytes.Length -le 2097152) 'Full archive exceeds fixed limit'
    $stream=New-Object IO.MemoryStream(,$Bytes)
    $archive=New-Object IO.Compression.ZipArchive($stream,[IO.Compression.ZipArchiveMode]::Read)
    try {
        Assert-Gr ($archive.Entries.Count -eq $Limits.Count) 'Duplicate/missing/extra ZIP members'
        $result=@{}
        foreach($entry in $archive.Entries) {
            $name=$entry.FullName
            Assert-Gr (@($Limits.Keys|Where-Object{$_ -ceq $name}).Count -eq 1 -and !$result.ContainsKey($name) -and $entry.Length -gt 0 -and $entry.Length -le $Limits[$name] -and $entry.CompressedLength -le 2097152) 'Invalid ZIP member/extent'
            $memberStream=$entry.Open();$output=New-Object IO.MemoryStream
            try {
                $buffer=New-Object byte[] 4096
                while(($count=$memberStream.Read($buffer,0,$buffer.Length)) -gt 0) {
                    Assert-Gr ($output.Length+$count -le $Limits[$name]) 'Expanded ZIP exceeds fixed limit'
                    $output.Write($buffer,0,$count)
                }
                $data=$output.ToArray();Assert-Gr ($data.Length -eq $entry.Length) 'Truncated ZIP member';$result[$name]=$data
            } finally {$memberStream.Dispose();$output.Dispose()}
        }
        return $result
    } finally {$archive.Dispose();$stream.Dispose()}
}
function Get-GrGeometry {
    param([string]$Family,[string]$Chip)
    Assert-Gr ($Chip -cin @('GR5331','GR5513','GR5515')) 'Unknown exact chip'
    $small=$Chip -ceq 'GR5331';$internal=$Family -ceq 'gr5513_zkel_internal'
    Assert-Gr (($small -and $Family -ceq 'gr5331_zkc21v') -or (!$small -and $Family -ceq 'gr551x_zkc42v') -or ($internal -and $Chip -ceq 'GR5513')) 'Mixed chip/executable family'
    $font=if($small){0x60000}elseif($internal){0x64000}else{0x72000}
    $nvds=if($small -or $internal){0x7e000}elseif($Chip -ceq 'GR5515'){0xfc000}else{0x7c000}
    @{base=$(if($small){0x200000}else{0x1000000});capacity=$(if($Chip -ceq 'GR5515'){0x100000}else{0x80000});
      recovery=0x4000;app=0x20000;appEnd=$font;gui=$font;guiBytes=0x8000;journal=$font+0x8000;journalBytes=8192;nvds=$nvds;nvdsBytes=$(if($small -or $internal){8192}else{16384})}
}
function Get-GrLayout { param([string]$Family)
    switch -CaseSensitive ($Family) {'gr5331_zkc21v'{0x31563347} 'gr551x_zkc42v'{0x31563547} 'gr5513_zkel_internal'{0x31564947} default{throw 'Unknown executable layout'}}
}
function Assert-GrGui {
    param([byte[]]$Gui)
    Assert-Gr ($Gui.Length -ge 88 -and $Gui.Length -le 32768 -and [InkTag.GoodixBytes]::HexText([InkTag.GoodixBytes]::Slice($Gui,0,8)) -ceq '494b464101005800') 'Invalid GUI header/size'
    $candidate=[byte[]]$Gui.Clone();[InkTag.GoodixBytes]::W32($candidate,16,0)
    Assert-Gr ([InkTag.GoodixBytes]::Crc($candidate) -eq [InkTag.GoodixBytes]::U32($Gui,16) -and [InkTag.GoodixBytes]::U32($Gui,12) -eq $Gui.Length -and [InkTag.GoodixBytes]::U32($Gui,20) -eq 4) 'Invalid GUI CRC/table'
    $end=88
    for($i=0;$i -lt 4;$i++) {
        $at=24+16*$i;$offset=[InkTag.GoodixBytes]::U32($Gui,$at+4);$length=[InkTag.GoodixBytes]::U32($Gui,$at+8)
        Assert-Gr ($Gui[$at] -eq 1 -and $Gui[$at+1] -eq $i -and $Gui[$at+2] -eq 1 -and $Gui[$at+3] -eq 0 -and $offset -eq $end -and $length -ge 31 -and $length -le $Gui.Length-$offset) 'Invalid GUI face extent'
        Assert-Gr ([InkTag.GoodixBytes]::Crc([InkTag.GoodixBytes]::Slice($Gui,$offset,$length)) -eq [InkTag.GoodixBytes]::U32($Gui,$at+12)) 'Invalid GUI face CRC'
        $end+=$length
    }
    Assert-Gr ($end -eq $Gui.Length) 'Trailing GUI bytes'
}
function Get-GrDefaultGui {
    $t=Get-GrJson ([IO.File]::ReadAllBytes((Join-Path $script:GrCleanRoot 'firmware/factory/native-font-template.json')))
    $raw=[Convert]::FromBase64String($t.package_base64)
    Assert-Gr ([InkTag.GoodixBytes]::Sha($raw) -ceq $t.package_sha256) 'Default GUI template hash changed'
    Assert-GrGui $raw
    [InkTag.GoodixBytes]::W32($raw,8,1);[InkTag.GoodixBytes]::W32($raw,16,0);[InkTag.GoodixBytes]::W32($raw,16,[InkTag.GoodixBytes]::Crc($raw))
    return ,$raw
}
function Read-GrConfig {
    param([string]$Path)
    $raw=[InkTag.GoodixBytes]::ReadFile($Path,65536);Assert-Gr ($raw.Length -gt 0 -and $raw.Length -le 65536) 'Invalid config size'
    if([IO.Path]::GetExtension($Path) -ieq '.json') {$data=Get-GrJson $raw}
    elseif([IO.Path]::GetExtension($Path) -ieq '.toml') {
        $data=@{};$block=$data
        foreach($line in ((New-Object Text.UTF8Encoding($false,$true)).GetString($raw) -split '\r?\n')) {
            $line=$line.Trim();if(!$line -or $line.StartsWith('#')){continue}
            if($line -cmatch '^\[(screen|gpio\.display|gpio\.led)\]$') {
                $parts=$Matches[1].Split('.');$block=$data
                foreach($part in $parts){if(!$block.ContainsKey($part)){$block[$part]=@{}};$block=$block[$part]};continue
            }
            Assert-Gr ($line -cmatch '^([a-z_]+)\s*=\s*(.*?)\s*(?:\s+#.*)?$') 'Unsupported TOML syntax; use the maintained preset or JSON'
            $key=$Matches[1];$value=$Matches[2].Trim();Assert-Gr (!$block.ContainsKey($key)) 'Duplicate config field'
            if($value -cmatch '^"([^"\\]*)"$'){$block[$key]=$Matches[1]}
            elseif($value -ceq 'true'){$block[$key]=$true}elseif($value -ceq 'false'){$block[$key]=$false}
            elseif($value -cmatch '^(0|[1-9][0-9]*)$'){$block[$key]=[int]::Parse($value,[Globalization.CultureInfo]::InvariantCulture)}
            elseif($value -cmatch '^\[(?:\s*"[a-z_]+"\s*(?:,\s*"[a-z_]+"\s*)*)?\]$') {$block[$key]=@([regex]::Matches($value,'"([a-z_]+)"')|ForEach-Object{$_.Groups[1].Value})}
            else{throw 'Unsupported TOML value; use maintained preset or JSON'}
        }
    } else {throw 'Native entry accepts TOML or JSON; existing Python entry retains YAML support'}
    Assert-GrKeys $data @('format_version','board_marking','screen','gpio')
    Assert-Gr ($data.format_version -is [int] -and $data.format_version -eq 1 -and $data.board_marking -is [string]) 'Invalid config version/marking'
    $facts=(Get-GrJson ([IO.File]::ReadAllBytes((Join-Path $script:GrCleanRoot 'firmware/ports/goodix/common/src/goodix-pin-backends.json')))).backends
    $match=@($facts.PSObject.Properties|Where-Object{$_.Value.board_marking -ceq $data.board_marking});Assert-Gr ($match.Count -eq 1) 'Unregistered/ambiguous exact board marking'
    $backend=$match[0].Name;$f=$match[0].Value;$screen=$data.screen
    $geometry=@('width','height','visible_x','visible_y','visible_width','visible_height')
    Assert-GrKeys $screen (@('ic','recipe','color','rotation','mirror_x','mirror_y','native_ui_scale')+$geometry)
    $rules=Get-GrJson ([IO.File]::ReadAllBytes((Join-Path $PSScriptRoot 'goodix-clean-screen-rules.json')))
    $driver=$rules.controllers.PSObject.Properties[$screen.ic];$color=$rules.colors.PSObject.Properties[$screen.color]
    Assert-Gr ($null -ne $driver -and $null -ne $color -and $driver.Name -ceq $screen.ic -and $color.Name -ceq $screen.color -and $screen.recipe -is [int] -and $screen.recipe -ge 0 -and $screen.recipe -le 255) 'Unknown screen IC/recipe/color'
    $rule=$rules.rules.PSObject.Properties[([string]($driver.Value*256+$screen.recipe))]
    Assert-Gr ($null -ne $rule -and $color.Value -in $rule.Value.colors) 'Incompatible screen IC/recipe/color'
    $rule=$rule.Value
    foreach($key in $geometry){Assert-Gr ($screen.$key -is [int] -and $screen.$key -ge 0 -and $screen.$key -le 65535) 'Invalid screen geometry'}
    Assert-Gr ($screen.width -ge 1 -and $screen.height -ge 1 -and $screen.width -le $rule.max[0] -and $screen.height -le $rule.max[1] -and $screen.width%$rule.align -eq 0 -and $screen.visible_width -ge 1 -and $screen.visible_height -ge 1 -and $screen.visible_x+$screen.visible_width -le $screen.width -and $screen.visible_y+$screen.visible_height -le $screen.height) 'Invalid screen extent/alignment/viewport'
    if($rule.fixed_sizes.Count){Assert-Gr (@($rule.fixed_sizes|Where-Object{$_[0] -eq $screen.width -and $_[1] -eq $screen.height}).Count -gt 0) 'Wrong fixed panel size'}
    $sizes=$rule.color_fixed_sizes.PSObject.Properties[[string]$color.Value]
    if($null -ne $sizes -and $sizes.Value.Count){Assert-Gr (@($sizes.Value|Where-Object{$_[0] -eq $screen.width -and $_[1] -eq $screen.height}).Count -gt 0) 'Wrong color-specific panel size'}
    if($rule.PSObject.Properties['full_viewport'] -and $rule.full_viewport){Assert-Gr ($screen.visible_x -eq 0 -and $screen.visible_y -eq 0 -and $screen.visible_width -eq $screen.width -and $screen.visible_height -eq $screen.height) 'Recipe requires full viewport'}
    Assert-Gr ($screen.rotation -is [int] -and $screen.rotation -in @(0,90,180,270) -and $screen.mirror_x -is [bool] -and $screen.mirror_y -is [bool] -and $screen.native_ui_scale -is [int] -and $screen.native_ui_scale -ge 0 -and $screen.native_ui_scale -le 5) 'Invalid transform/UI scale'
    $wire=New-Object byte[] 17;$wire[0]=1;$wire[1]=$driver.Value;$wire[2]=$screen.recipe;$wire[3]=$color.Value;$wire[4]=($screen.rotation/90) -bor $(if($screen.mirror_x){4}else{0}) -bor $(if($screen.mirror_y){8}else{0}) -bor ($screen.native_ui_scale -shl 4)
    for($i=0;$i -lt 6;$i++){[InkTag.GoodixBytes]::W16($wire,5+2*$i,$screen.($geometry[$i]))}
    Assert-Gr ($null -ne $rules.busy.PSObject.Properties[([string]($driver.Value*256+$screen.recipe))]) 'Unknown recipe BUSY semantics'
    Assert-GrKeys $data.gpio @('display','led');$roles=@('mosi','clock','cs','dc','reset','busy','enable','red','green','blue')
    Assert-GrKeys $data.gpio.display ($roles[0..6]+@('enable_active_low'));Assert-GrKeys $data.gpio.led ($roles[7..9]+@('active_low'))
    Assert-Gr ($data.gpio.display.enable_active_low -is [bool]) 'Invalid rail polarity'
    $low=@($data.gpio.led.active_low);Assert-Gr ($low.Count -eq @($low|Select-Object -Unique).Count -and !@($low|Where-Object{$_ -cnotin @('red','green','blue')}).Count) 'Invalid LED polarity'
    $pins=[InkTag.GoodixBytes]::Fill(16,255);$pins[0]=1;$pins[1]=0;$pins[2]=0;$pins[3]=0;$polarity=0;$used=@{}
    for($i=0;$i -lt 10;$i++) {
        $role=$roles[$i];$group=if($i -lt 7){$data.gpio.display}else{$data.gpio.led};$name=$group.$role
        Assert-Gr ($name -is [string]) 'Pin must use GPIO/AON/MSIO name or NONE'
        if($name -ceq 'NONE'){Assert-Gr ($role -cnotin $low) 'Disconnected pin cannot have polarity';continue}
        Assert-Gr ($name -cmatch '^(GPIO|AON|MSIO)(0|[1-9][0-9]?)$') 'Invalid pin name'
        $bank=[array]::IndexOf(@('GPIO','AON','MSIO'),$Matches[1]);$index=[int]$Matches[2];$pin=$bank*64+$index
        Assert-Gr ($index -lt 32 -and $index -in $f.usable[$bank] -and $pin -notin $f.aux -and !$used.ContainsKey($pin) -and (($role -ceq 'enable') -eq ($pin -eq $f.rail))) 'Protected/unqualified/overlapping pin'
        $used[$pin]=$true;$pins[4+$i]=$pin
        if(($i -eq 6 -and $data.gpio.display.enable_active_low) -or $role -cin $low){$polarity=$polarity -bor (1 -shl $i)}
    }
    $count=@($pins[4..9]|Where-Object{$_ -ne 255}).Count
    Assert-Gr (($count -eq 6 -or ($count -eq 0 -and $pins[10] -eq 255)) -and ($polarity -band (-bnot 960)) -eq 0) 'Incomplete display routing/polarity'
    [InkTag.GoodixBytes]::W16($pins,2,$polarity)
    $selector=New-Object byte[] 12;[InkTag.GoodixBytes]::Put($selector,0,[Text.Encoding]::ASCII.GetBytes('IBD1'));$selector[4]=1;$selector[5]=$f.family;$selector[6]=$f.board_id;$selector[7]=1;[InkTag.GoodixBytes]::W32($selector,8,[InkTag.GoodixBytes]::Fnv([InkTag.GoodixBytes]::Slice($selector,0,8)))
    $profile=New-Object byte[] 64;[InkTag.GoodixBytes]::Put($profile,0,[Text.Encoding]::ASCII.GetBytes('GPF1'));$profile[4]=1;$profile[5]=$f.family;$profile[6]=$f.board_id;$profile[7]=$f.mcu;[InkTag.GoodixBytes]::Put($profile,8,$pins);[InkTag.GoodixBytes]::Put($profile,24,$wire);[InkTag.GoodixBytes]::W32($profile,56,[InkTag.GoodixBytes]::Fnv([InkTag.GoodixBytes]::Slice($profile,0,56)));[InkTag.GoodixBytes]::Put($profile,60,[Text.Encoding]::ASCII.GetBytes('GPF!'))
    @{backend=$backend;facts=$f;chip=$(@{49='GR5331';19='GR5513';21='GR5515'}[[int]$f.mcu]);selector=$selector;profile=$profile;screen=$wire;raw=$raw}
}
function Read-GrApp {
    param([byte[]]$Raw,[bool]$Base)
    # Native producers use application.bin. Other names cannot select a path.
    $members=Read-GrArchive $Raw @{'manifest.json'=16384;'application.bin'=1048576}
    $m=Get-GrJson $members['manifest.json'];$a=$m.application;$app=$members['application.bin'];$family=$m.board
    $chip=if($family -ceq 'gr5331_zkc21v'){'GR5331'}else{'GR5513'};$g=Get-GrGeometry $family $chip;$layout=Get-GrLayout $family
    Assert-Gr ($m.format -ceq 'inktag-ota-v1' -and $m.transport -ceq 'goodix-otas-v2' -and $m.layout -ceq 'layout-v1' -and $m.compatibilityEpoch -eq 1 -and $m.release -ceq '0.2.2') 'Wrong App container/version/epoch/layout'
    Assert-Gr ($a.file -ceq 'application.bin' -and $a.size -is [int] -and $a.size -gt 40 -and $a.size -le $g.appEnd-$g.app-904 -and $a.transfer_size -eq $app.Length -and $app.Length -le $g.appEnd-$g.app -and $a.active_address -eq $g.base+$g.app -and $a.slot_size -eq $g.appEnd-$g.app) 'Wrong native App size/address'
    Assert-Gr ([InkTag.GoodixBytes]::Sha($app) -ceq $a.sha256) 'App SHA mismatch'
    $n=$a.size;$body=[InkTag.GoodixBytes]::Slice($app,0,$n);$info=[InkTag.GoodixBytes]::Slice($app,$n,48)
    Assert-Gr ([InkTag.GoodixBytes]::U16($info,0) -eq 0x4744 -and [InkTag.GoodixBytes]::U16($info,2) -eq 1 -and [InkTag.GoodixBytes]::U32($info,4) -eq $n -and [InkTag.GoodixBytes]::U32($info,8) -eq $a.checksum -and [InkTag.GoodixBytes]::Sum($body) -eq $a.checksum -and [InkTag.GoodixBytes]::U32($info,12) -eq $g.base+$g.app -and [InkTag.GoodixBytes]::U32($info,16) -eq $g.base+$g.app -and [InkTag.GoodixBytes]::U32($info,40) -eq $layout -and [InkTag.GoodixBytes]::U32($info,44) -eq ([uint32]::MaxValue -bxor $layout)) 'Native App descriptor mismatch'
    $public=$null;$rsa=$family -cne 'gr5331_zkc21v'
    if($Base) {
        Assert-GrKeys $m.signature @('required','algorithm','policy')
        Assert-Gr ($m.sourceProfile -ceq 'base' -and $app.Length -eq $n+48 -and $m.signature.required -eq $false -and $m.signature.algorithm -ceq 'none' -and $m.signature.policy -ceq 'crc-only') 'Explicit CRC-only Base package required'
        $comment=New-Object byte[] 12;$text=switch -CaseSensitive ($family){'gr5331_zkc21v'{'inktag5331'} 'gr551x_zkc42v'{'inktag551x'} default{'inktag5513i'}};[InkTag.GoodixBytes]::Put($comment,0,[Text.Encoding]::ASCII.GetBytes($text))
        Assert-Gr ([InkTag.GoodixBytes]::U32($info,20) -eq 3 -and [InkTag.GoodixBytes]::U32($info,24) -eq 0x744 -and [InkTag.GoodixBytes]::Equal([InkTag.GoodixBytes]::Slice($info,28,12),$comment)) 'Base native flags/comment mismatch'
        $r=[InkTag.GoodixBytes]::Slice($body,$n-40,40);$linked=[InkTag.GoodixBytes]::Slice($body,0,$n-40)
        Assert-Gr ([InkTag.GoodixBytes]::HexText([InkTag.GoodixBytes]::Slice($r,0,8)) -ceq '494b434901280000' -and [InkTag.GoodixBytes]::U32($r,8) -eq $linked.Length -and [InkTag.GoodixBytes]::U32($r,12) -eq [InkTag.GoodixBytes]::Crc($linked) -and [InkTag.GoodixBytes]::U32($r,16) -eq $(if($rsa){0}else{2}) -and [InkTag.GoodixBytes]::U32($r,20) -eq $layout -and [InkTag.GoodixBytes]::U32($r,24) -eq 1 -and [InkTag.GoodixBytes]::U32($r,28) -eq 202 -and [InkTag.GoodixBytes]::U32($r,32) -eq 0 -and [InkTag.GoodixBytes]::U32($r,36) -eq [InkTag.GoodixBytes]::Crc([InkTag.GoodixBytes]::Slice($r,0,36))) 'Invalid Base IKCI'
        Assert-Gr ($a.linked_size -eq $linked.Length -and $a.linked_sha256 -ceq [InkTag.GoodixBytes]::Sha($linked) -and [InkTag.GoodixBytes]::Contains($linked,[Text.Encoding]::ASCII.GetBytes($(switch -CaseSensitive ($family){'gr5331_zkc21v'{"gr5331_zkel`0"} 'gr551x_zkc42v'{"gr551x_zkel`0"} default{"gr5513_zkel_internal`0"}})))) 'Base linked family mismatch'
    } else {
        $s=$m.signature;$expected=if($rsa){'a410a4f0ce51dd2c89ba8cca1dade9b9'}else{'3d96ce5cf20e58cca5f5e291ca2bfd81'}
        Assert-Gr ($app.Length -eq $n+904 -and $s.required -eq $true -and $s.trust_profile -ceq 'production' -and $s.algorithm -ceq $(if($rsa){'rsa-pss-sha256'}else{'ecdsa-p256-sha256'}) -and $s.key_id -ceq $(if($rsa){'inktag-gr551x-ota-v1'}else{'inktag-gr5331-ota-v1'}) -and $s.public_key_hash -ceq $expected -and $s.signature_extension_size -eq 856 -and $s.signed_trailer_size -eq 904) 'Wrong Pro publisher policy'
        Assert-Gr ($m.chip -ceq $(if($rsa){'GR551x'}else{'GR5331'}) -and $m.releaseVersion -eq 202 -and $m.protocol -ge 0 -and $m.protocol -le 255 -and [InkTag.GoodixBytes]::Contains($body,[byte[]]@($m.protocol,0,2,2))) 'Pro native release mismatch'
        Assert-Gr ([InkTag.GoodixBytes]::U32($app,$n+48) -eq 3735928559L -and [Text.Encoding]::ASCII.GetString($app,$n+120,4) -ceq 'SIGN') 'Not a sign-only container'
        $public=[InkTag.GoodixBytes]::Slice($app,$n+128,$(if($rsa){520}else{65}))
        Assert-Gr ([InkTag.GoodixBytes]::Sha($public).Substring(0,32) -ceq $expected) 'Untrusted embedded publisher key'
        [InkTag.GoodixBytes]::VerifySignature($public,[InkTag.GoodixBytes]::Slice($app,$n+648,$(if($rsa){256}else{64})),[InkTag.GoodixBytes]::Slice($app,0,$n+648),$rsa)
        $journal=$g.base+$g.journal;$store=$journal+8192;$storeBytes=if($family -ceq 'gr5331_zkc21v'){81920}elseif($family -ceq 'gr5513_zkel_internal'){65536}else{524288}
        Assert-Gr ($a.image_store_start -eq $store -and $a.image_store_max_size -eq $storeBytes -and @($a.recovery_state_addresses).Count -eq 2 -and $a.recovery_state_addresses[0] -eq $journal -and $a.recovery_state_addresses[1] -eq $journal+4096) 'Pro storage/journal contract mismatch'
    }
    @{manifest=$m;app=$app;info=$info;body=$body;family=$family;public=$public;rsa=$rsa}
}
function Assert-GrRecipe {
    param($Recipe,[string]$Chip)
    Assert-GrKeys $Recipe @('codec','sdkSha256','offHex')
    $sdk=if($Chip -ceq 'GR5331'){'c0d6655832fc8eef95dffb8ee90926ef4365f305c2dd3aed6137f05abed63575'}else{'f12c3f5229edc48db98e5fcb20d74da8f50a1d924f2d40350a6a31f615b4692d'}
    Assert-Gr ($Recipe.codec -ceq 'goodix-pristine-nvds-v1' -and $Recipe.sdkSha256 -ceq $sdk -and $Recipe.offHex -is [string] -and $Recipe.offHex.Length -le 840) 'Wrong native NVDS recipe'
    $off=[InkTag.GoodixBytes]::Hex($Recipe.offHex);Assert-Gr ([InkTag.GoodixBytes]::HexText($off) -ceq $Recipe.offHex -and $off.Length -gt 9 -and $off[0] -eq 1 -and $off[1] -eq 0 -and $off[2] -in @(21,25,29,33,37,41,45,49,53,57)) 'Invalid OFF/default QR recipe'
    Assert-Gr ($off.Length -eq 9+[Math]::Ceiling($off[2]*$off[2]/8.0) -and [InkTag.GoodixBytes]::U16($off,3) -eq $off.Length-9 -and [InkTag.GoodixBytes]::U32($off,5) -eq [InkTag.GoodixBytes]::Crc([InkTag.GoodixBytes]::Slice($off,9,$off.Length-9))) 'Invalid OFF QR size/CRC'
    $padding=($off.Length-9)*8-$off[2]*$off[2];Assert-Gr (!$padding -or ($off[-1] -band ((1 -shl $padding)-1)) -eq 0) 'Invalid OFF QR padding'
    return ,$off
}
function Read-GrFirmware {
    param([string]$Path,[string]$Chip)
    $raw=[InkTag.GoodixBytes]::ReadFile($Path,2097152)
    # Read the bounded directory first, without extraction or selected paths.
    $stream=New-Object IO.MemoryStream(,$raw);$zip=New-Object IO.Compression.ZipArchive($stream,[IO.Compression.ZipArchiveMode]::Read)
    try{$isBase=@($zip.Entries|Where-Object{$_.FullName -ceq 'full.json'}).Count -eq 1;$familyPackage=@($zip.Entries|Where-Object{$_.FullName -ceq 'factory-init-GR5513.ikfi'}).Count -eq 1}finally{$zip.Dispose();$stream.Dispose()}
    $limits=if($isBase){@{'full.json'=16384;'app-ota.zip'=1048576;'recovery.bin'=114688;'gui-font.bin'=32768}}elseif($familyPackage){@{'base.json'=4096;'app-ota.zip'=1048576;'recovery.bin'=114688;'factory-init-GR5513.ikfi'=32768;'factory-init-GR5515.ikfi'=32768}}else{@{'base.json'=4096;'app-ota.zip'=1048576;'recovery.bin'=114688;'factory-init.ikfi'=32768}}
    $data=Read-GrArchive $raw $limits;$app=Read-GrApp $data['app-ota.zip'] $isBase;$g=Get-GrGeometry $app.family $Chip;$rec=$data['recovery.bin']
    Assert-Gr ($rec.Length -ge 8 -and $rec.Length -le 114688 -and [InkTag.GoodixBytes]::Contains($rec,[byte[]]@(73,75,82,86,1,0,1,1)) -and [InkTag.GoodixBytes]::Contains($rec,[byte[]]@(73,75,83,80,1,2)) -and ![InkTag.GoodixBytes]::Contains($rec,[byte[]]@(73,75,83,80,1,0)) -and ![InkTag.GoodixBytes]::Contains($rec,[byte[]]@(73,75,83,80,1,1))) 'Common Recovery1.0.1/CRC-only policy required'
    if($isBase) {
        $m=Get-GrJson $data['full.json'];$chips=if($app.family -ceq 'gr551x_zkc42v'){@('GR5513','GR5515')}elseif($Chip -ceq 'GR5331'){@('GR5331')}else{@('GR5513')}
        Assert-Gr ($m.schema -ceq 'inktag-gr-base-full-v1' -and $m.sourceProfile -ceq 'base' -and $m.family -ceq $app.family -and $m.layout -ceq 'layout-v1' -and $m.compatibilityEpoch -eq 1 -and $m.release -ceq '0.2.2' -and $m.integrityPolicy -ceq 'crc-only' -and $m.publisherSignature -eq $false -and ![bool](Compare-Object @($m.chips) $chips -CaseSensitive)) 'Invalid Base Full descriptor'
        Assert-GrKeys $m.files @('app-ota.zip','recovery.bin','gui-font.bin');foreach($name in $m.files.PSObject.Properties.Name){Assert-GrRecord $data[$name] $m.files.$name}
        Assert-GrRecord $data['gui-font.bin'] $m.gui;Assert-GrGui $data['gui-font.bin']
        Assert-Gr ($m.recovery.version -ceq '1.0.1' -and $m.recovery.epoch -eq 1 -and [InkTag.GoodixBytes]::Equal([InkTag.GoodixBytes]::Slice($rec,$m.recoveryPolicyOffset,6),[byte[]]@(73,75,83,80,1,2))) 'Recovery metadata/policy mismatch'
        Assert-GrKeys $m.hostNvds $chips;foreach($part in $chips){$null=Assert-GrRecipe $m.hostNvds.$part $part};$recipe=$m.hostNvds.$Chip;$gui=$data['gui-font.bin']
    } else {
        $m=Get-GrJson $data['base.json'];$chips=if($familyPackage){@('GR5513','GR5515')}else{@($Chip)}
        Assert-Gr ($m.layout -ceq 'layout-v1' -and (($familyPackage -and $m.schema -ceq 'inktag-gr-full-family-v1' -and $m.family -ceq 'GR551x' -and ![bool](Compare-Object @($m.chips) $chips -CaseSensitive)) -or (!$familyPackage -and $m.schema -ceq 'inktag-gr-full-base-v1' -and $m.chip -ceq $Chip))) 'Wrong Pro Full family'
        Assert-GrKeys $m.files @($limits.Keys|Where-Object{$_ -cne 'base.json'})
        foreach($name in $m.files.PSObject.Properties.Name){Assert-Gr ([InkTag.GoodixBytes]::Sha($data[$name]) -ceq $m.files.$name) 'Full payload SHA mismatch'}
        foreach($part in $chips) {
            $blob=$data[$(if($familyPackage){'factory-init-'+$part+'.ikfi'}else{'factory-init.ikfi'})]
            Assert-Gr ([Text.Encoding]::ASCII.GetString($blob,0,4) -ceq 'IKFI' -and [InkTag.GoodixBytes]::U16($blob,4) -eq 1 -and [InkTag.GoodixBytes]::U16($blob,6) -eq 0) 'Invalid authenticated Full binding'
            $count=[InkTag.GoodixBytes]::U32($blob,8);$length=[InkTag.GoodixBytes]::U32($blob,12);$sigBytes=if($app.rsa){256}else{64}
            Assert-Gr ($count -gt 0 -and $count -le 4096 -and $length -gt 64 -and $length -le 16384 -and $blob.Length -eq 16+$count+$length+$sigBytes) 'Invalid IKFI extent'
            $bound=Get-GrJson ([InkTag.GoodixBytes]::Slice($blob,16,$count));$helper=[InkTag.GoodixBytes]::Slice($blob,16+$count,$length)
            Assert-Gr ([InkTag.GoodixBytes]::Equal([Text.Encoding]::ASCII.GetBytes((ConvertTo-GrCanonicalJson $bound)),[InkTag.GoodixBytes]::Slice($blob,16,$count))) 'Noncanonical authenticated Full metadata'
            [byte[]]$message=[Text.Encoding]::ASCII.GetBytes("InkTag factory-init`0")+[InkTag.GoodixBytes]::Slice($blob,0,$blob.Length-$sigBytes)
            [InkTag.GoodixBytes]::VerifySignature($app.public,[InkTag.GoodixBytes]::Slice($blob,$blob.Length-$sigBytes,$sigBytes),$message,$app.rsa)
            $packages=if($part -ceq 'GR5331'){@(5331)}elseif($part -ceq 'GR5515'){@(4)}elseif($app.family -ceq 'gr5513_zkel_internal'){@(7)}else{@(5,7)}
            Assert-Gr ($bound.purpose -ceq 'InkTag factory-init' -and $bound.version -eq 1 -and $bound.chip -ceq $part -and $bound.layout -ceq 'layout-v1' -and $bound.layoutMarker -eq (Get-GrLayout $app.family) -and $bound.compatibilityEpoch -eq 1 -and $bound.algorithm -ceq $app.manifest.signature.algorithm -and ![bool](Compare-Object @($bound.packages) $packages) -and $bound.releaseBaseline -ceq 'inktag-release-v1' -and $bound.configSchema -eq 1) 'Authenticated Full context mismatch'
            Assert-Gr ($bound.appSha256 -ceq [InkTag.GoodixBytes]::Sha($app.body) -and $bound.signedAppSha256 -ceq [InkTag.GoodixBytes]::Sha($app.app) -and $bound.recoverySha256 -ceq [InkTag.GoodixBytes]::Sha($rec) -and $bound.helperSha256 -ceq [InkTag.GoodixBytes]::Sha($helper) -and $bound.helperBytes -eq $helper.Length -and [InkTag.GoodixBytes]::Contains($app.body,[Text.Encoding]::ASCII.GetBytes("INKTAG-GR-RELEASE1`0"))) 'Authenticated executable binding mismatch'
            $fields=@('purpose','version','chip','packages','layout','layoutMarker','compatibilityEpoch','algorithm','ramAbi','operationProfile','appSha256','signedAppSha256','recoverySha256','helperSha256','helperBytes','loadAddress','entry','stack','bssStart','bssEnd','mailbox','mailboxBytes','buildReceiptSha256','releaseBaseline','hostNvds','configSchema')
            foreach($field in @('appBootStageAddress','appReadyContext')){if($bound.PSObject.Properties[$field]){$fields+=$field}}
            Assert-GrKeys $bound $fields
            foreach($field in @('ramAbi','helperBytes','loadAddress','entry','bssStart','bssEnd','mailbox','mailboxBytes')){Assert-Gr ($bound.$field -is [int] -or $bound.$field -is [long]) 'Non-integer RAM ABI field'}
            foreach($field in @('appSha256','signedAppSha256','recoverySha256','helperSha256','buildReceiptSha256')){Assert-Gr ($bound.$field -is [string] -and $bound.$field -cmatch '^[0-9a-f]{64}$' -and $bound.$field -cne ('0'*64)) 'Invalid authenticated digest'}
            $small=$part -ceq 'GR5331';$load=if($small){0x2000af00}else{0x30018000};$limit=if($small){0x2000d400}else{0x3001c000};$bottom=if($small){0x2000d500}else{0x3001c000};$top=if($small){0x2000de80}else{0x3001e000};$mailbox=if($small){0x2000d400}else{0x30017c00}
            Assert-Gr (($bound.ramAbi -eq 1 -and $bound.operationProfile -ceq 'goodix-first-off-v1' -and $bound.mailboxBytes -eq 72) -or ($bound.ramAbi -eq 2 -and $bound.operationProfile -ceq 'goodix-identity-off-v2' -and $bound.mailboxBytes -eq 84)) 'Unknown authenticated RAM ABI'
            Assert-Gr ($helper.Length -lt $limit-$load -and $bound.loadAddress -eq $load -and $bound.entry -eq $load+65 -and @($bound.stack).Count -eq 2 -and $bound.stack[0] -eq $bottom -and $bound.stack[1] -eq $top -and $bound.mailbox -eq $mailbox -and $bound.bssStart -eq ([int]([Math]::Ceiling(($load+$helper.Length)/8.0))*8) -and $bound.bssStart -le $bound.bssEnd -and $bound.bssEnd -le $limit) 'Unreviewed helper RAM/entry/stack/BSS'
            $handler=[InkTag.GoodixBytes]::U32($helper,8)
            Assert-Gr ([InkTag.GoodixBytes]::U32($helper,0) -eq $top -and [InkTag.GoodixBytes]::U32($helper,4) -eq $bound.entry -and ($handler -band 1) -eq 1 -and ($handler -band (-bnot 1)) -ge $load+64 -and ($handler -band (-bnot 1)) -lt $load+$helper.Length) 'Invalid helper vectors'
            foreach($word in 3..6){Assert-Gr ([InkTag.GoodixBytes]::U32($helper,4*$word) -eq $handler) 'Invalid helper fault vectors'}
            Assert-Gr ([InkTag.GoodixBytes]::Uniform([InkTag.GoodixBytes]::Slice($helper,28,36),0)) 'Invalid helper reserved vectors'
            if($bound.PSObject.Properties['appBootStageAddress']) {
                $start=if($small){0x20000000}else{0x30000000};$address=$bound.appBootStageAddress
                Assert-Gr ($bound.ramAbi -eq 2 -and ($address -is [int] -or $address -is [long]) -and $address%4 -eq 0 -and $address -ge $start -and $address+4 -le [Math]::Min($load,$mailbox)) 'Invalid authenticated App boot-stage binding'
            }
            if($bound.PSObject.Properties['appReadyContext']) {
                Assert-Gr ($null -ne $bound.PSObject.Properties['appBootStageAddress']) 'Ready context requires boot-stage binding'
                $ready=$bound.appReadyContext;Assert-GrKeys $ready @('kind','pc','stack');$ramEnd=if($small){0x20018000}else{0x30020000};$base=$g.base+$g.app;$pc=$ready.pc
                Assert-Gr ($ready.kind -ceq 'post-init-main-wfe-v1' -and ($pc -is [int] -or $pc -is [long]) -and $pc%2 -eq 0 -and $pc -ge $base+2 -and $pc -lt $g.base+$g.appEnd -and $pc-$base -lt $app.body.Length -and [InkTag.GoodixBytes]::Equal([InkTag.GoodixBytes]::Slice($app.body,$pc-$base-2,2),[byte[]]@(32,191))) 'Invalid authenticated App ready PC'
                Assert-Gr (@($ready.stack).Count -eq 2 -and $ready.stack[0] -ge $top -and $ready.stack[0] -lt $ready.stack[1] -and $ready.stack[1] -le $ramEnd -and $ready.stack[1]-$ready.stack[0] -le 8192 -and $ready.stack[0]%8 -eq 0 -and $ready.stack[1]%8 -eq 0) 'Invalid authenticated App main stack'
            }
            $null=Assert-GrRecipe $bound.hostNvds $part;if($part -ceq $Chip){$recipe=$bound.hostNvds}
        }
        $gui=Get-GrDefaultGui
    }
    $sp=[InkTag.GoodixBytes]::U32($rec,0);$entry=[InkTag.GoodixBytes]::U32($rec,4);$ram=if($Chip -ceq 'GR5331'){0x20000000}else{0x30000000};$ramBytes=if($Chip -ceq 'GR5331'){0x18000}else{0x20000}
    Assert-Gr ($sp -gt $ram -and $sp -le $ram+$ramBytes -and $sp%8 -eq 0 -and ($entry -band 1) -eq 1 -and ($entry -band (-bnot 1)) -ge $g.base+$g.recovery -and ($entry -band (-bnot 1)) -lt $g.base+$g.recovery+$rec.Length) 'Invalid Recovery vectors'
    @{raw=$raw;app=$app.app;info=$app.info;body=$app.body;recovery=$rec;gui=$gui;recipe=$recipe;family=$app.family;geometry=$g;sourceProfile=$(if($isBase){'base'}else{'pro'})}
}
function Read-GrLinearNvds {
    param([byte[]]$Bytes,[int]$Alignment)
    $limit=$Bytes.Length-4096;$cursor=8;$records=@{}
    Assert-Gr ([Text.Encoding]::ASCII.GetString($Bytes,0,8) -ceq 'GDX_NVDS' -and [InkTag.GoodixBytes]::Uniform([InkTag.GoodixBytes]::Slice($Bytes,$limit,4096),255)) 'Unrecognized/active-GC native NVDS'
    while($cursor -lt $limit) {
        if([InkTag.GoodixBytes]::Uniform([InkTag.GoodixBytes]::Slice($Bytes,$cursor,$limit-$cursor),255)){break}
        Assert-Gr ($cursor+8 -le $limit) 'Truncated old NVDS header'
        [int]$tag=[InkTag.GoodixBytes]::U16($Bytes,$cursor);$n=[InkTag.GoodixBytes]::U16($Bytes,$cursor+2)
        Assert-Gr ($n -gt 0 -and $n -le 4080 -and $cursor+8+$n -le $limit -and [InkTag.GoodixBytes]::U16($Bytes,$cursor+6) -eq 0) 'Invalid old NVDS extent'
        $p=[InkTag.GoodixBytes]::Slice($Bytes,$cursor+8,$n)
        Assert-Gr (([InkTag.GoodixBytes]::Sum($p) -band 255) -eq $Bytes[$cursor+5]) 'Old NVDS value checksum failed'
        if($tag) {Assert-Gr ((($tag+$n) -band 255) -eq $Bytes[$cursor+4] -and !$records.ContainsKey($tag)) 'Invalid/duplicate live old NVDS tag';$records[$tag]=$p}
        $end=$cursor+8+$n;$cursor=[int]([Math]::Ceiling($end/[double]$Alignment)*$Alignment)
        Assert-Gr ([InkTag.GoodixBytes]::Uniform([InkTag.GoodixBytes]::Slice($Bytes,$end,$cursor-$end),0)) 'Invalid old NVDS padding'
    }
    return $records
}
function Read-GrOldNvds {
    param([byte[]]$Bytes,[string]$Chip,[string]$Family)
    $warning='Optional old NVDS unavailable; initializing SDK defaults/unactivated.'
    $records=@{}
    if($null -ne $Bytes -and $Bytes.Length) {
        try {
            $g=Get-GrGeometry $Family $Chip
            Assert-Gr ($Bytes.Length -eq $g.nvdsBytes -or ($Family -ceq 'gr5513_zkel_internal' -and $Bytes.Length -eq 16384)) 'Incomplete optional NVDS'
            if($Family -ceq 'gr5513_zkel_internal' -and $Bytes.Length -eq 16384 -and [Text.Encoding]::ASCII.GetString($Bytes,0,8) -cne 'GDX_NVDS') {
                $single=-1
                for($at=0;$at -lt 16384;$at+=4096) {
                    if([Text.Encoding]::ASCII.GetString($Bytes,$at,8) -ceq 'GDX_NVDS' -and [InkTag.GoodixBytes]::Uniform([InkTag.GoodixBytes]::Slice($Bytes,0,$at),255) -and [InkTag.GoodixBytes]::Uniform([InkTag.GoodixBytes]::Slice($Bytes,$at+4096,16384-$at-4096),255)){$single=$at}
                }
                $Bytes=if($single -ge 0){[byte[]]([InkTag.GoodixBytes]::Slice($Bytes,$single,4096)+[InkTag.GoodixBytes]::Fill(4096,255))}else{[InkTag.GoodixBytes]::Slice($Bytes,8192,8192)}
            }
            if(![InkTag.GoodixBytes]::Uniform($Bytes,255)) {
                if([Text.Encoding]::ASCII.GetString($Bytes,0,8) -cne 'GDX_NVDS') {
                    $single=-1
                    for($at=0;$at -lt $Bytes.Length;$at+=4096) {
                        if([Text.Encoding]::ASCII.GetString($Bytes,$at,8) -ceq 'GDX_NVDS' -and [InkTag.GoodixBytes]::Uniform([InkTag.GoodixBytes]::Slice($Bytes,0,$at),255) -and [InkTag.GoodixBytes]::Uniform([InkTag.GoodixBytes]::Slice($Bytes,$at+4096,$Bytes.Length-$at-4096),255)){$single=$at}
                    }
                    Assert-Gr ($single -ge 0) 'Unrecognized old NVDS format'
                    $Bytes=[byte[]]([InkTag.GoodixBytes]::Slice($Bytes,$single,4096)+[InkTag.GoodixBytes]::Fill($Bytes.Length-4096,255))
                } elseif($Family -ceq 'gr5513_zkel_internal' -and $Bytes.Length -eq 16384) {
                    Assert-Gr ([Text.Encoding]::ASCII.GetString($Bytes,8192,8) -cne 'GDX_NVDS') 'Conflicting old/new NVDS logs'
                }
                $candidates=@();foreach($alignment in $(if($Chip -ceq 'GR5331'){@(1)}else{@(1,4)})) {try{$candidates+=,(Read-GrLinearNvds $Bytes $alignment)}catch{}}
                Assert-Gr ($candidates.Count -gt 0) 'Old NVDS cannot be fully decoded'
                $records=$candidates[0]
                foreach($other in $candidates) {
                    Assert-Gr ($records.Count -eq $other.Count) 'Ambiguous old NVDS framing'
                    foreach($tag in $records.Keys){Assert-Gr ($other.ContainsKey($tag) -and [InkTag.GoodixBytes]::Equal($records[$tag],$other[$tag])) 'Ambiguous old NVDS framing'}
                }
            }
            $warning=$null
        } catch {$records=@{};$warning='Old NVDS invalid/unreadable; initializing SDK defaults/unactivated: '+$_.Exception.Message}
    }
    $warnings=@();if($warning){$warnings+=,$warning};$identity=$null
    try {
        if($records.ContainsKey(0x5212)) {
            $p=$records[0x5212];Assert-Gr ($p.Length -eq 11 -and $p[0] -in @(16,17) -and [InkTag.GoodixBytes]::U32($p,7) -eq [InkTag.GoodixBytes]::Crc([InkTag.GoodixBytes]::Slice($p,0,7))) 'Invalid old typed identity'
            $address=[InkTag.GoodixBytes]::Slice($p,1,6);$kind=$p[0] -band 1
            Assert-Gr ([InkTag.GoodixBytes]::Address($address,$kind) -and [InkTag.GoodixBytes]::HexText($address) -cne 'eacb3ecf000f') 'Invalid old typed identity'
            $identity=@{address=$address;kind=$kind}
        }
    } catch {$warnings+=,'Old typed address not retained: '+$_.Exception.Message}
    try {
        if($null -eq $identity -and $records.ContainsKey(0xc001)) {
            $address=[InkTag.GoodixBytes]::Reverse($records[0xc001]);Assert-Gr ([InkTag.GoodixBytes]::Address($address,0)) 'Invalid legacy SDK public identity'
            if([InkTag.GoodixBytes]::HexText($address) -cne 'eacb3ecf000f'){$identity=@{address=$address;kind=0}}
        }
    } catch {$warnings+=,'Old address not retained: '+$_.Exception.Message}
    $vendor=@{};foreach($tag in $records.Keys){if(($tag -lt 0x5201 -or $tag -gt 0x5212) -and $tag -ne 0xc001 -and $records[$tag].Length -le 4076){$vendor[$tag]=$records[$tag]}}
    @{identity=$identity;vendor=$vendor;activation=$records[0x5206];warnings=$warnings}
}
function Get-GrRetainedPro {
    param([byte[]]$Record,[byte[]]$Uid,$Firmware,[string]$Chip)
    Assert-Gr ($Firmware.sourceProfile -cin @('base','pro')) 'Unknown App source profile'
    Assert-Gr ($Record.Length -eq 108 -and [InkTag.GoodixBytes]::U32($Record,0) -eq 0x31434149 -and $Record[4] -eq 1 -and $Record[5] -eq 1 -and [InkTag.GoodixBytes]::U32($Record,104) -eq [InkTag.GoodixBytes]::Crc([InkTag.GoodixBytes]::Slice($Record,0,104))) 'Invalid native Pro wrapper'
    Assert-Gr ($null -ne $Uid -and $Uid.Length -eq 16 -and ![InkTag.GoodixBytes]::Uniform($Uid,0) -and ![InkTag.GoodixBytes]::Uniform($Uid,255)) 'No current immutable UID; Pro cannot be bound'
    # No configuration/public entry can override the production key or key ID.
    $public=[InkTag.GoodixBytes]::Hex('6cef365a284c497a548f93eee2397fe43673a2499c2494a52241122dbd17944278341110bb317e5ba5d7f49e71d71bcfb14731327643156e5a23ec9261d24b3b')
    if($Firmware.sourceProfile -ceq 'pro') {
        Assert-Gr ([InkTag.GoodixBytes]::Count($Firmware.body,$public) -eq 1) 'Candidate App does not bind the activation public key'
    }
    . (Join-Path $PSScriptRoot 'activation-preservation.ps1')
    $platform=if($Chip -ceq 'GR5331'){0x0102}else{0x0101}
    $id=Get-InkTagActivationDeviceId -PlatformId $platform -ProductId 1 -RawUid $Uid
    $verified=Test-InkTagActivationToken -Token ([InkTag.GoodixBytes]::Slice($Record,8,96)) -DeviceId $id
    Assert-Gr ($verified.signatureVerified -eq $true -and $verified.hostHardwareBindingVerified -eq $true) 'Same-unit Pro signature/binding not established'
    return ,$Record
}
function New-GrCleanImage {
    param($Firmware,$Config,[byte[]]$OldNvds=$null,[byte[]]$Uid=$null,[byte[]]$Gui=$null)
    $g=Get-GrGeometry $Firmware.family $Config.chip;$capability=[byte[]]@(73,75,71,65,1,18,82,0)
    Assert-Gr ([InkTag.GoodixBytes]::Count($Firmware.body,$capability) -eq 1 -and [InkTag.GoodixBytes]::Count($Firmware.recovery,$capability) -eq 1) 'Clean initialization requires actual App and Common Recovery address-persistence capability'
    Assert-Gr (($Firmware.family -ceq 'gr5513_zkel_internal') -eq ($Config.backend -ceq 'gr5513_zkc21v')) 'Wrong internal executable/PCB combination'
    if($Firmware.sourceProfile -ceq 'base'){Assert-Gr ($Config.screen[1] -notin @(50,64,65)) 'Controller not compiled in GR Base'}
    if($null -eq $Gui -or !$Gui.Length){$Gui=$Firmware.gui};Assert-GrGui $Gui
    Assert-Gr ($Firmware.app.Length -le $g.appEnd-$g.app -and $Firmware.recovery.Length -le 0x1c000 -and $Gui.Length -le $g.guiBytes -and $g.journal+$g.journalBytes -le $g.nvds) 'Overlapping native payloads'
    $image=[InkTag.GoodixBytes]::Fill($g.capacity,255);$header=New-Object byte[] 24
    [InkTag.GoodixBytes]::W32($header,0,$Firmware.recovery.Length);[InkTag.GoodixBytes]::W32($header,4,[InkTag.GoodixBytes]::Sum($Firmware.recovery));[InkTag.GoodixBytes]::W32($header,8,$g.base+$g.recovery);[InkTag.GoodixBytes]::W32($header,12,$g.base+$g.recovery);[InkTag.GoodixBytes]::W32($header,16,3);[InkTag.GoodixBytes]::W32($header,20,0x744)
    [InkTag.GoodixBytes]::Put($image,0,$header);$descriptor=New-Object byte[] $(if($Config.chip -ceq 'GR5331'){48}else{40});[InkTag.GoodixBytes]::W16($descriptor,0,0x4744);[InkTag.GoodixBytes]::W16($descriptor,2,1);[InkTag.GoodixBytes]::Put($descriptor,4,$header);[InkTag.GoodixBytes]::Put($descriptor,28,[Text.Encoding]::ASCII.GetBytes('inktag_boot'));[InkTag.GoodixBytes]::Put($image,0x40,$descriptor)
    [InkTag.GoodixBytes]::Put($image,0xf00,$Config.selector);[InkTag.GoodixBytes]::Put($image,0xf20,$Config.profile);[InkTag.GoodixBytes]::Put($image,0x2000,$Firmware.info);[InkTag.GoodixBytes]::W32($image,0x3ff8,0x31444649);[InkTag.GoodixBytes]::W32($image,0x3ffc,([uint32]::MaxValue -bxor 0x31444649))
    [InkTag.GoodixBytes]::Put($image,$g.recovery,$Firmware.recovery);[InkTag.GoodixBytes]::Put($image,$g.app,$Firmware.app);[InkTag.GoodixBytes]::Put($image,$g.gui,$Gui)
    $journal=[InkTag.GoodixBytes]::Fill(8192,255);$body=New-Object byte[] 28
    [InkTag.GoodixBytes]::W32($body,0,0x314a5249);[InkTag.GoodixBytes]::W32($body,4,(Get-GrLayout $Firmware.family));[InkTag.GoodixBytes]::W32($body,8,1);[InkTag.GoodixBytes]::W32($body,12,1);$body[16]=3;$body[17]=1;$body[18]=255;$body[19]=255;[InkTag.GoodixBytes]::W32($body,20,1);[InkTag.GoodixBytes]::W32($body,24,[uint32]::MaxValue)
    [InkTag.GoodixBytes]::Put($journal,0,$body);[InkTag.GoodixBytes]::W32($journal,28,[InkTag.GoodixBytes]::Crc($body));[InkTag.GoodixBytes]::Put($image,$g.journal,$journal)
    $old=Read-GrOldNvds $OldNvds $Config.chip $Firmware.family;$warnings=@($old.warnings);$entitlement=$null
    if($null -ne $old.activation){try{$entitlement=Get-GrRetainedPro $old.activation $Uid $Firmware $Config.chip}catch{$warnings+=,'Pro not retained: '+$_.Exception.Message}}
    $records=New-Object IO.MemoryStream
    try {
        $magic=[Text.Encoding]::ASCII.GetBytes('GDX_NVDS');$records.Write($magic,0,$magic.Length)
        $items=@()
        if($null -ne $old.identity){$items+=,@(0xc001,[InkTag.GoodixBytes]::Reverse($old.identity.address));$items+=,@(0x5212,[InkTag.GoodixBytes]::AddressRecord($old.identity.address,$old.identity.kind))}
        $items+=,@(0x520d,(Assert-GrRecipe $Firmware.recipe $Config.chip))
        # App and Recovery admit the boot pin profile only with its matching NVDS copy.
        $items+=,@(0x5210,[byte[]]($Config.selector+$Config.profile))
        if($null -ne $entitlement){$items+=,@(0x5206,$entitlement)}
        $required=8;foreach($item in $items){$required+=8+$item[1].Length}
        $optional=0;foreach($tag in $old.vendor.Keys){$optional+=8+$old.vendor[$tag].Length}
        if($required+$optional -gt $g.nvdsBytes-4096-$(if($Config.chip -ceq 'GR5331'){12}else{0})) {
            $warnings+=,'Optional SDK overrides do not fit the target NVDS; initializing SDK defaults.'
            $old.vendor=@{}
        }
        foreach($tag in ($old.vendor.Keys|Sort-Object)){$items+=,@($tag,$old.vendor[$tag])}
        foreach($item in $items){$bytes=[InkTag.GoodixBytes]::NvdsItem($item[0],$item[1]);$records.Write($bytes,0,$bytes.Length)}
        $prefix=$records.ToArray();Assert-Gr ($prefix.Length -le $g.nvdsBytes-4096-$(if($Config.chip -ceq 'GR5331'){12}else{0})) 'Retained NVDS exceeds native data area'
        [InkTag.GoodixBytes]::Put($image,$g.nvds,$prefix)
    } finally {$records.Dispose()}
    $report=@{schema='inktag-gr-clean-bin-v1';sourceProfile=$Firmware.sourceProfile;chip=$Config.chip;family=$Firmware.family;backend=$Config.backend;geometry=$g;package=(Get-GrRecord $Firmware.raw);config=(Get-GrRecord $Config.raw);gui=(Get-GrRecord $Gui);image=(Get-GrRecord $image);addressRetained=($null -ne $old.identity);proRetained=($null -ne $entitlement);vendorRecordsRetained=$old.vendor.Count;warnings=$warnings;writeRanges=@(@{space='mcu-flash';address=$g.base;bytes=$g.capacity});skippedSpaces=@('OTP','eFuse','Flash security registers','external SPI image Flash');hardwareQualified=$false;hardwareAccessed=$false}
    @{report=$report;image=$image;gui=$Gui}
}
