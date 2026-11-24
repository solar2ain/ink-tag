# Closed HW56 host format adapter. User J-Link paths use only Windows/.NET.
# Python remains a separate development oracle and is never launched here.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.Runtime.Serialization
if (-not ('InkTagNordicImage' -as [type])) {
    if($PSVersionTable.PSEdition -eq 'Desktop') {
        Add-Type -Path (Join-Path $PSScriptRoot 'nordic-image.cs') -ReferencedAssemblies @(
            'System.dll','System.Core.dll','System.Xml.dll','System.Runtime.Serialization.dll',
            'System.IO.Compression.dll','System.IO.Compression.FileSystem.dll')
    } else {Add-Type -Path (Join-Path $PSScriptRoot 'nordic-image.cs')}
}
$script:NordicNativeDirectory = $PSScriptRoot
function Read-NordicJson([byte[]]$Bytes) {
    if($Bytes.Length -ge 3 -and $Bytes[0] -eq 239 -and $Bytes[1] -eq 187 -and $Bytes[2] -eq 191){$Bytes=[InkTagNordicImage]::Part($Bytes,3,$Bytes.Length-3)}
    [void][InkTagNordicImage]::Json($Bytes)
    return ([Text.Encoding]::UTF8.GetString($Bytes) | ConvertFrom-Json)
}
function Assert-NordicKeys($Value,[string[]]$Keys,[string]$Name) {
    $actual=@($Value.PSObject.Properties.Name)
    if ($actual.Count -ne $Keys.Count -or @($actual | Where-Object { $Keys -cnotcontains $_ }).Count) {
        throw "Invalid $Name fields"
    }
}
function Assert-NordicNativeScreen([byte[]]$wire) {
    if($wire.Length -ne 17){throw 'Native screen size'}
    if(@($wire|Where-Object {$_ -ne 0}).Count -eq 0){return}
    if($wire[0] -ne 1 -or ($wire[4] -shr 4) -gt 5){throw 'Native screen version/scale'}
    $registry=Read-NordicJson ([IO.File]::ReadAllBytes((Join-Path $script:NordicNativeDirectory 'nordic-screen-rules.json')))
    $id=[int]$wire[1];$colorId=[int]$wire[3]
    $s=[pscustomobject]@{recipe=[int]$wire[2];width=[InkTagNordicImage]::U16($wire,5);height=[InkTagNordicImage]::U16($wire,7);visible_x=[InkTagNordicImage]::U16($wire,9);visible_y=[InkTagNordicImage]::U16($wire,11);visible_width=[InkTagNordicImage]::U16($wire,13);visible_height=[InkTagNordicImage]::U16($wire,15)}
    $rule=@($registry.rules | Where-Object {$_.driver -eq $id -and $_.recipe -eq $s.recipe})
    if($rule.Count -ne 1 -or @($rule[0].colors) -notcontains $colorId){throw 'Unsupported screen recipe/color'}
    $rule=$rule[0]
    if($s.width -lt 1 -or $s.height -lt 1 -or $s.width -gt $rule.max[0] -or $s.height -gt $rule.max[1] -or $s.width % $rule.align -or $s.visible_width -lt 1 -or $s.visible_height -lt 1 -or $s.visible_x+$s.visible_width -gt $s.width -or $s.visible_y+$s.visible_height -gt $s.height){throw 'Invalid screen dimensions/alignment/viewport'}
    if($rule.full_viewport -and ($s.visible_x -or $s.visible_y -or $s.visible_width -ne $s.width -or $s.visible_height -ne $s.height)){throw 'Recipe needs full viewport'}
    $sizeOK=$false
    foreach($size in @($rule.fixed_sizes)){if($size[0] -eq $s.width -and $size[1] -eq $s.height){$sizeOK=$true}}
    if(@($rule.fixed_sizes).Count -and !$sizeOK){throw 'Recipe dimensions mismatch'}
    $sizes=$rule.color_fixed_sizes.PSObject.Properties | Where-Object {$_.Name -ceq [string]$colorId}
    if($sizes){$sizeOK=$false;foreach($size in @($sizes.Value)){if($size[0] -eq $s.width -and $size[1] -eq $s.height){$sizeOK=$true}};if(!$sizeOK){throw 'Color dimensions mismatch'}}
}
function Read-NordicNativeConfiguration([string]$Path) {
    $raw=[IO.File]::ReadAllBytes($Path)
    if($raw.Length -gt 65536){throw 'Board configuration exceeds bound'}
    if($raw.Length -ge 3 -and $raw[0] -eq 239 -and $raw[1] -eq 187 -and $raw[2] -eq 191){$raw=[InkTagNordicImage]::Part($raw,3,$raw.Length-3)}
    if ([IO.Path]::GetExtension($Path) -ieq '.json') { $config=Read-NordicJson $raw }
    elseif ([IO.Path]::GetExtension($Path) -ieq '.toml') {
        # The existing shipped board format: bounded scalar tables, no code/eval.
        $tree=@{};$section=$tree
        foreach($line in ([Text.Encoding]::UTF8.GetString($raw) -split '\r?\n')) {
            $text=$line.Trim()
            if (!$text -or $text.StartsWith('#')) { continue }
            if ($text -match '^\[([a-z_.]+)\]$') {
                $section=$tree
                foreach($part in $Matches[1].Split('.')) {
                    if(!$section.ContainsKey($part)){$section[$part]=@{}}
                    if($section[$part] -isnot [hashtable]){throw 'TOML table collides with scalar'}
                    $section=$section[$part]
                }
                continue
            }
            if($text -notmatch '^([a-z_]+)\s*=\s*("[^"\r\n]*"|true|false|-?\d+|\[\])\s*(?:#.*)?$'){throw 'Unsupported TOML scalar; use supplied TOML or JSON (legacy YAML remains in the legacy entry)'}
            $key=$Matches[1];$value=$Matches[2]
            if($section.ContainsKey($key)){throw 'Duplicate TOML key'}
            if($value -ceq '[]'){$section[$key]=@()}else{$section[$key]=($value | ConvertFrom-Json)}
        }
        $config=($tree | ConvertTo-Json -Depth 12 | ConvertFrom-Json)
    } else {throw 'Native initialization supports the supplied TOML and JSON; YAML remains available through the legacy tool'}
    Assert-NordicKeys $config @('format_version','board_marking','screen','gpio') 'configuration'
    if($config.format_version -cne 1 -or $config.board_marking -cne 'PriceETag V1.7'){throw 'Exact PriceETag V1.7 configuration required'}
    Assert-NordicKeys $config.gpio @('display','led') 'gpio'
    Assert-NordicKeys $config.gpio.display @('mosi','clock','cs','dc','reset','busy','enable','active_low') 'display GPIO'
    $pins=@{mosi='P0.08';clock='P0.09';cs='P0.10';dc='P0.11';reset='P0.12';busy='P0.13';enable='NONE'}
    foreach($key in $pins.Keys){if($config.gpio.display.$key -cne $pins[$key]){throw 'Unqualified Nordic GPIO route'}}
    if($config.gpio.display.active_low -isnot [array] -or $config.gpio.display.active_low.Count){throw 'Unqualified display polarity'}
    Assert-NordicKeys $config.gpio.led @('red','green','blue','active_low') 'LED'
    foreach($key in @('red','green','blue')){if($config.gpio.led.$key -cne 'NONE'){throw 'No fitted Nordic RGB backend'}}
    if($config.gpio.led.active_low -isnot [array] -or $config.gpio.led.active_low.Count){throw 'No fitted LED polarity backend'}
    $s=$config.screen
    $geometry=@('width','height','visible_x','visible_y','visible_width','visible_height')
    Assert-NordicKeys $s (@('ic','recipe','color','rotation','mirror_x','mirror_y','native_ui_scale')+$geometry) 'screen'
    foreach($key in (@('recipe','rotation','native_ui_scale')+$geometry)) {
        if(($s.$key -isnot [int] -and $s.$key -isnot [long]) -or $s.$key -lt 0 -or $s.$key -gt 65535){throw 'Screen fields must be bounded integers'}
    }
    if($s.mirror_x -isnot [bool] -or $s.mirror_y -isnot [bool] -or @(0,90,180,270) -notcontains $s.rotation -or $s.native_ui_scale -gt 5){throw 'Invalid screen orientation/scale'}
    $registry=Read-NordicJson ([IO.File]::ReadAllBytes((Join-Path $script:NordicNativeDirectory 'nordic-screen-rules.json')))
    $controller=@($registry.controllers.PSObject.Properties | Where-Object {$_.Name -ceq $s.ic})
    $color=@($registry.colors.PSObject.Properties | Where-Object {$_.Name -ceq $s.color})
    if($controller.Count -ne 1 -or $color.Count -ne 1){throw 'Unknown screen controller/color'}
    $id=[int]$controller[0].Value;$colorId=[int]$color[0].Value
    [byte[]]$wire=New-Object byte[] 17
    $wire[0]=1;$wire[1]=$id;$wire[2]=$s.recipe;$wire[3]=$colorId
    $wire[4]=($s.rotation/90) -bor $(if($s.mirror_x){4}else{0}) -bor $(if($s.mirror_y){8}else{0}) -bor ($s.native_ui_scale -shl 4)
    for($i=0;$i -lt 6;$i++){[InkTagNordicImage]::Put16($wire,5+$i*2,[uint16]$s.($geometry[$i]))}
    Assert-NordicNativeScreen $wire
    return ,$wire
}
function Read-NordicNativeFirmware([string]$Path,[string]$SourcePolicy='auto') {
    $bytes=[IO.File]::ReadAllBytes($Path)
    $files=[InkTagNordicImage]::Zip($bytes,$true)
    $base=Read-NordicJson $files['base.json']
    Assert-NordicKeys $base @('format_version','mcu','firmware_family','backend_ids','hw_version','compatibility_epoch','config_schema','baseline','layout_version','layout_marker','images') 'Full manifest'
    if($base.format_version -ne 1 -or $base.mcu -cne 'nrf52832' -or $base.firmware_family -cne 'nrf52832_priceetag' -or @($base.backend_ids).Count -ne 1 -or $base.backend_ids[0] -cne 'nrf52832_priceetag' -or $base.hw_version -ne 56 -or $base.compatibility_epoch -ne 1 -or $base.config_schema -ne 1 -or $base.baseline -cne 'inktag-release-v1' -or $base.layout_version -ne 1 -or $base.layout_marker -cne 'N3V1'){throw 'Full MCU/board/epoch/layout mismatch'}
    Assert-NordicKeys $base.images @('full.hex','app-ota.zip') 'Full images'
    foreach($name in @('full.hex','app-ota.zip')) {
        $entry=$base.images.$name;Assert-NordicKeys $entry @('bytes','sha256') 'Full member'
        if($entry.bytes -ne $files[$name].Length -or $entry.sha256 -cne [InkTagNordicImage]::Hash($files[$name])){throw 'Full member size/hash mismatch'}
    }
    $image=[InkTagNordicImage]::ReadHex($files['full.hex'])
    [InkTagNordicImage]::VerifyNative($image)
    $signed=[InkTagNordicImage]::SignedOta($files['app-ota.zip'])
    if($SourcePolicy -eq 'auto'){$SourcePolicy=if($signed){'pro'}else{'base'}}
    if(($SourcePolicy -eq 'pro') -ne $signed){throw 'Explicit source profile differs from native OTA signature policy'}
    [void][InkTagNordicImage]::VerifyOta($files['app-ota.zip'],$image,($SourcePolicy -eq 'pro'))
    $records=[InkTagNordicImage]::Journal([InkTagNordicImage]::Part($image,0x56000,8192),$false)
    if(!$records.ContainsKey(1) -or !$records.ContainsKey(12)){throw 'Native factory/boot records missing'}
    $sourceWire=[InkTagNordicImage]::ValidateTemplate($records,$image)
    Assert-NordicNativeScreen $sourceWire
    return @{image=$image;records=$records;ota=$files['app-ota.zip'];profile=$SourcePolicy;firmwareSha256=[InkTagNordicImage]::Hash($bytes);appBytes=[InkTagNordicImage]::U32($image,0x5f018);appCrc32=[InkTagNordicImage]::U32($image,0x5f01c)}
}
