# Fixed nRF52811 HW52 format and clean defaults. No Python or transport.
if (-not ('NrfInitBytes' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'initialization-bytes.cs') -ReferencedAssemblies System.dll,System.Core.dll,System.IO.Compression.dll
}

function Assert-InitCondition([bool]$Condition,[string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Get-InitKeys($Object) {
    if ($Object -is [Collections.IDictionary]) { return @($Object.Keys) }
    Assert-InitCondition ($Object -is [pscustomobject]) 'Expected a JSON object'
    return @($Object.PSObject.Properties.Name)
}
function Assert-InitKeys($Object,[string[]]$Names) {
    $keys=@(Get-InitKeys $Object)
    Assert-InitCondition ($keys.Count -eq $Names.Count) 'Unexpected or missing fields'
    foreach($key in $keys) { Assert-InitCondition ($Names -ccontains $key) "Unknown field: $key" }
}
function ConvertFrom-InitJson([byte[]]$Bytes) {
    $text=[Text.UTF8Encoding]::new($false,$true).GetString($Bytes)
    [NrfInitBytes]::CheckJson($text)
    return ConvertFrom-Json -InputObject $text
}
function Read-InitFile([string]$Path,[int]$Maximum) {
    $file=Get-Item -LiteralPath $Path -ErrorAction Stop
    Assert-InitCondition (-not $file.PSIsContainer -and -not ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -and $file.Length -gt 0 -and $file.Length -le $Maximum) "Invalid input: $Path"
    return ,([IO.File]::ReadAllBytes($file.FullName))
}
function Get-InitFull([string]$Firmware) {
    $raw=Read-InitFile $Firmware 2097152
    $files=[NrfInitBytes]::Zip($raw,$true)
    $manifest=ConvertFrom-InitJson $files['base.json']
    $expected=@{format_version=1;mcu='nrf52811';firmware_family='aesl22_nrf';hw_version=52;config_schema=1;baseline='inktag-release-v1';layout_version=1;layout_marker='N1V1';factory_format='N1P1';user_format='N1R1'}
    $extra=@('backend_ids','images','compatibility_epoch')
    $self=$manifest.PSObject.Properties.Name -ccontains 'container_kind'
    $allowUnsigned=$false
    if ($self) {
        $extra+=@('container_kind','recovery_signature_policy','minimum_tools_version','app_gui_crc32')
        Assert-InitCondition ($manifest.container_kind -ceq 'inktag-nrf52811-self-built-full-v1') 'Unknown self-built Full'
        $policy=$manifest.recovery_signature_policy
        Assert-InitCondition (@('crc-only','optional','required') -ccontains $policy) 'Invalid Recovery policy'
        $minimum=if($policy -ceq 'crc-only'){'0.2.1.2'}else{'0.2.1.0'}
        Assert-InitCondition ($manifest.minimum_tools_version -ceq $minimum) 'Wrong minimum tools binding'
        $allowUnsigned=$policy -cne 'required'
    }
    Assert-InitKeys $manifest (@($expected.Keys)+$extra)
    foreach($key in $expected.Keys) {
        Assert-InitCondition ($manifest.$key.GetType() -eq $expected[$key].GetType() -and $manifest.$key -ceq $expected[$key]) "Wrong Full $key"
    }
    Assert-InitCondition (@($manifest.backend_ids).Count -eq 1 -and $manifest.backend_ids[0] -ceq 'nrf52811_asel22') 'Wrong native board family'
    Assert-InitKeys $manifest.images @('full.hex','app-ota.zip','gui-assets.bin')
    foreach($name in @('full.hex','app-ota.zip','gui-assets.bin')) {
        $row=$manifest.images.$name;Assert-InitKeys $row @('bytes','sha256')
        Assert-InitCondition ($row.bytes -eq $files[$name].Length -and $row.sha256 -ceq [NrfInitBytes]::Hash($files[$name])) "Full member differs: $name"
    }
    $image=[NrfInitBytes]::IntelHex($files['full.hex'])
    [NrfInitBytes]::Boot($image,$files['gui-assets.bin'])
    if ($self) { Assert-InitCondition ($manifest.app_gui_crc32 -eq [NrfInitBytes]::U32($files['gui-assets.bin'],12)) 'App/GUI binding differs' }
    $ota=[NrfInitBytes]::Zip($files['app-ota.zip'],$false)
    $otamanifest=ConvertFrom-InitJson $ota['manifest.json']
    Assert-InitKeys $otamanifest @('manifest');Assert-InitKeys $otamanifest.manifest @('application')
    $application=$otamanifest.manifest.application;Assert-InitKeys $application @('bin_file','dat_file')
    $bin=$application.bin_file;$dat=$application.dat_file
    Assert-InitCondition ($bin -is [string] -and $dat -is [string] -and $bin -cne $dat -and $bin.EndsWith('.bin') -and $dat.EndsWith('.dat') -and $ota.ContainsKey($bin) -and $ota.ContainsKey($dat)) 'OTA member mapping'
    $version=[NrfInitBytes]::CheckApp($ota[$bin],$ota[$dat],$allowUnsigned)
    Assert-InitCondition ($version -eq $image.Version -and ($version -shr 16) -eq 1 -and $manifest.compatibility_epoch -eq 1) 'Full/App epoch or version differs'
    Assert-InitCondition ([NrfInitBytes]::Equal($ota[$bin],[NrfInitBytes]::Slice($image.Bytes,0x19000,$image.AppBytes))) 'Full/OTA App differs'
    $marker=[Text.Encoding]::ASCII.GetString($ota[$bin])
    Assert-InitCondition ([regex]::Matches($marker,"N11PINCFG1`0").Count -eq 1) 'Missing pin-contract App'
    return [pscustomobject]@{Raw=$raw;Image=$image;Assets=$files['gui-assets.bin'];PackageSha=[NrfInitBytes]::Hash($raw);AppSha=[NrfInitBytes]::Hash($ota[$bin]);Manifest=$manifest}
}

function Join-InitBytes([object[]]$Parts) {
    $stream=[IO.MemoryStream]::new()
    try { foreach($part in $Parts){$b=[byte[]]$part;$stream.Write($b,0,$b.Length)};return ,$stream.ToArray() } finally {$stream.Dispose()}
}
function New-InitRecord([int]$Kind,[byte[]]$Payload) {
    $key=New-Object byte[] 4
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint16]$Kind),0,$key,0,2)
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint16]$Payload.Length),0,$key,2,2)
    $header=New-Object byte[] 12
    [NrfInitBytes]::Put($header,0,0x31525349)
    [Buffer]::BlockCopy($key,0,$header,4,4)
    [NrfInitBytes]::Put($header,8,[NrfInitBytes]::Fnv((Join-InitBytes @($key,$Payload))))
    return ,(Join-InitBytes @($header,$Payload,[NrfInitBytes]::Filled((4-$Payload.Length%4)%4,255),[Text.Encoding]::ASCII.GetBytes('ISC1')))
}
function New-InitJournal($Full,$Config) {
    $factory=Join-InitBytes @([Text.Encoding]::ASCII.GetBytes('N1P1'),$Config.NativePins,$Config.Screen,[BitConverter]::GetBytes([uint16]$Config.ActiveLow))
    $factory=Join-InitBytes @($factory,[BitConverter]::GetBytes([NrfInitBytes]::Fnv($factory)))
    $screen=Join-InitBytes @([byte[]](83,80,1,17),$Config.Screen,[byte[]](0,0,0))
    $screen=Join-InitBytes @($screen,[BitConverter]::GetBytes([NrfInitBytes]::Fnv($screen)))
    $boot=New-Object byte[] 16
    [NrfInitBytes]::Put($boot,0,0x3153424e);$boot[4]=1;$boot[5]=1
    [NrfInitBytes]::Put($boot,8,$Full.Image.AppBytes)
    [NrfInitBytes]::Put($boot,12,[NrfInitBytes]::Crc($Full.Image.Bytes,0x19000,$Full.Image.AppBytes))
    $head=New-Object byte[] 16
    [NrfInitBytes]::Put($head,0,0x31505349);[NrfInitBytes]::Put($head,4,1)
    [NrfInitBytes]::Put($head,8,[uint32]::MaxValue-1);[NrfInitBytes]::Put($head,12,[uint32]::MaxValue)
    # Only factory routing, screen and first-boot display. No other device's
    # MAC, QR, user settings, health, password, Find or activation record.
    $records=Join-InitBytes @($head,(New-InitRecord 1 $factory),(New-InitRecord 3 $screen),(New-InitRecord 12 $boot))
    Assert-InitCondition ($records.Length -le 4096) 'Factory defaults exceed journal page'
    $journal=[NrfInitBytes]::Filled(8192,255);[Buffer]::BlockCopy($records,0,$journal,0,$records.Length)
    return ,$journal
}

function Get-InitConfig([string]$Path) {
    $raw=Read-InitFile $Path 16384
    $text=[Text.UTF8Encoding]::new($false,$true).GetString($raw)
    $tables=@{root=@{};screen=@{};'gpio.display'=@{};'gpio.led'=@{}}
    $section='root';$seen=@{}
    foreach($original in ($text -split "`r?`n")) {
        $line=$original.Trim();$quoted=$false;$escaped=$false;$literal=$false
        for($i=0;$i -lt $line.Length;$i++) {
            $char=$line[$i]
            if($escaped){$escaped=$false;continue}
            if($quoted -and -not $literal -and $char -eq '\'){$escaped=$true;continue}
            if(-not $quoted -and ($char -eq '"' -or $char -eq "'")){$quoted=$true;$literal=$char -eq "'";continue}
            if($quoted -and (($literal -and $char -eq "'") -or (-not $literal -and $char -eq '"'))){$quoted=$false;continue}
            if(-not $quoted -and $char -eq '#'){$line=$line.Substring(0,$i).Trim();break}
        }
        if(-not $line){continue}
        if($line -match '^\[([a-z.]+)\]$') {
            $section=$Matches[1];Assert-InitCondition ($tables.ContainsKey($section) -and $section -ne 'root' -and -not $seen.ContainsKey($section)) 'Unknown/duplicate TOML section';$seen[$section]=$true;continue
        }
        Assert-InitCondition ($line -match '^([a-z][a-z0-9_]*)\s*=\s*(.+)$') 'Use the supplied flat TOML format'
        $key=$Matches[1];$value=$Matches[2].Trim()
        Assert-InitCondition (-not $tables[$section].ContainsKey($key)) 'Duplicate TOML key'
        if($value.StartsWith("'") -and $value.EndsWith("'")){$parsed=$value.Substring(1,$value.Length-2)}
        else {$parsed=(ConvertFrom-InitJson ([Text.Encoding]::UTF8.GetBytes('{"value":'+$value+'}'))).value}
        $tables[$section][$key]=$parsed
    }
    Assert-InitKeys $tables.root @('format_version','board_marking','note')
    Assert-InitCondition ($tables.root.format_version -is [int] -and $tables.root.format_version -eq 1 -and $tables.root.board_marking -ceq 'EL022HWRL' -and $tables.root.note -ceq 'CAL_VER06') 'Unknown PCB/revision or config schema'
    $screen=$tables.screen;$geometry=@('width','height','visible_x','visible_y','visible_width','visible_height')
    Assert-InitKeys $screen (@('ic','recipe','color','rotation','mirror_x','mirror_y','native_ui_scale')+$geometry)
    foreach($key in (@('recipe','rotation','native_ui_scale')+$geometry)) {
        Assert-InitCondition (($screen[$key] -is [int] -or $screen[$key] -is [long]) -and $screen[$key] -ge 0 -and $screen[$key] -le 65535) "Invalid screen $key"
    }
    Assert-InitCondition ($screen.mirror_x -is [bool] -and $screen.mirror_y -is [bool] -and @(0,90,180,270) -contains $screen.rotation -and $screen.native_ui_scale -le 5) 'Invalid absolute orientation/UI scale'
    $rules=ConvertFrom-InitJson (Read-InitFile (Join-Path $PSScriptRoot 'initialization-screen-rules.json') 65536)
    $rule=@($rules.rules.PSObject.Properties.Value | Where-Object {$_.name -ceq $screen.ic -and $_.recipe -eq $screen.recipe})
    Assert-InitCondition ($rule.Count -eq 1) 'Unsupported/retired screen controller or recipe'
    $rule=$rule[0]
    $colors=@{BW=1;BWR=2;BWRY=3;GRAY4=4;BWY=5;BWB=6;BWG=7;BWO=8;GRAY16=9;BWRYGB=10;BWRYGBO=11}
    Assert-InitCondition ($colors.Keys -ccontains $screen.color -and $rule.colors -contains $colors[$screen.color]) 'Unsupported color'
    $color=$colors[$screen.color]
    Assert-InitCondition ($screen.width -ge 1 -and $screen.height -ge 1 -and $screen.width -le $rule.max[0] -and $screen.height -le $rule.max[1] -and $screen.width % $rule.align -eq 0) 'Screen dimensions/alignment'
    Assert-InitCondition ($screen.visible_width -gt 0 -and $screen.visible_height -gt 0 -and $screen.visible_x+$screen.visible_width -le $screen.width -and $screen.visible_y+$screen.visible_height -le $screen.height) 'Invalid viewport'
    if($rule.PSObject.Properties.Name -contains 'full_viewport' -and $rule.full_viewport) {
        Assert-InitCondition ($screen.visible_x -eq 0 -and $screen.visible_y -eq 0 -and $screen.visible_width -eq $screen.width -and $screen.visible_height -eq $screen.height) 'Recipe requires full viewport'
    }
    $colorSizes=@();$colorRule=$rule.color_fixed_sizes.PSObject.Properties[[string]$color]
    if($null -ne $colorRule){$colorSizes=$colorRule.Value}
    foreach($sizes in @($rule.fixed_sizes,$colorSizes)) {
        if($null -ne $sizes -and @($sizes).Count -gt 0) {
            $match=@($sizes | Where-Object {$_[0] -eq $screen.width -and $_[1] -eq $screen.height})
            Assert-InitCondition ($match.Count -gt 0) 'Recipe/color fixed size'
        }
    }
    $wire=New-Object byte[] 17
    $wire[0]=1;$wire[1]=$rule.driver;$wire[2]=$screen.recipe;$wire[3]=$color
    $wire[4]=[byte]([int]($screen.rotation/90) -bor ([int]$screen.mirror_x -shl 2) -bor ([int]$screen.mirror_y -shl 3) -bor ($screen.native_ui_scale -shl 4))
    for($i=0;$i -lt $geometry.Count;$i++){[Buffer]::BlockCopy([BitConverter]::GetBytes([uint16]$screen[$geometry[$i]]),0,$wire,5+2*$i,2)}
    $display=$tables['gpio.display'];$led=$tables['gpio.led']
    $roles=@('mosi','clock','cs','dc','reset','busy','enable','red','green','blue')
    Assert-InitKeys $display @('mosi','clock','cs','dc','reset','busy','enable','enable_active_low')
    Assert-InitKeys $led @('red','green','blue','active_low')
    Assert-InitCondition ($display.enable_active_low -is [bool]) 'enable_active_low must be boolean'
    $active=@($led.active_low);Assert-InitCondition (@($active | Select-Object -Unique).Count -eq $active.Count) 'Duplicate LED polarity'
    foreach($role in $active){Assert-InitCondition (@('red','green','blue') -ccontains $role) 'Invalid LED polarity'}
    $pins=New-Object byte[] 10;$low=0;$used=@{}
    for($i=0;$i -lt $roles.Count;$i++) {
        $role=$roles[$i];$value=if($i -lt 7){$display[$role]}else{$led[$role]}
        if($value -ceq 'NONE'){$pin=255}
        else {
            Assert-InitCondition ($value -is [string] -and $value -cmatch '^P0\.([0-9]{2})$') 'Use P0.xx or NONE'
            $pin=[int]$Matches[1]
            Assert-InitCondition ($pin -lt 32 -and (([Convert]::ToUInt32('ffdf0ff8',16) -band ([long]1 -shl $pin)) -ne 0)) 'Protected/unavailable GPIO'
            Assert-InitCondition (-not $used.ContainsKey($pin)) 'Overlapping GPIO roles';$used[$pin]=$true
        }
        $pins[$i]=$pin
        if(($i -eq 6 -and $display.enable_active_low) -or ($i -ge 7 -and $active -ccontains $role)) {
            Assert-InitCondition ($pin -ne 255) 'Disconnected polarity';$low=$low -bor (1 -shl $i)
        }
    }
    $signals=@($pins[0..5] | Where-Object {$_ -ne 255}).Count
    Assert-InitCondition ($signals -eq 6 -or ($signals -eq 0 -and $pins[6] -eq 255)) 'Supply all display signals or disable the entire display'
    $native=[byte[]]@($pins[0],$pins[1],$pins[2],$pins[3],$pins[4],$pins[5],2,$pins[7],$pins[8],$pins[9],$pins[6],1,1)
    return [pscustomobject]@{Raw=$raw;Screen=$wire;NativePins=$native;ActiveLow=$low;Sha=[NrfInitBytes]::Hash($raw);BoardMarking=$tables.root.board_marking;Note=$tables.root.note}
}

function New-InitMaterialData($Full,$Config) {
    $image=[NrfInitBytes]::Slice($Full.Image.Bytes,0,196608)
    # Reinitialize SDK17 Bank0/progress/bond/advertising metadata. Existing
    # manufactured identity is in FICR, outside every programmed range.
    $seed=[NrfInitBytes]::Filled(4096,255)
    [Array]::Clear($seed,0,0x323)
    foreach($entry in @(@(4,2),@(8,$Full.Image.Version),@(0x18,$Full.Image.AppBytes),@(0x1c,[NrfInitBytes]::Crc($image,0x19000,$Full.Image.AppBytes)),@(0x20,1),@(0x34,[NrfInitBytes]::U32($image,0x2f034)))) {
        [NrfInitBytes]::Put($seed,$entry[0],[uint32]$entry[1])
    }
    $seed[0x260]=1;$seed[0x2a1]=1
    [NrfInitBytes]::Put($seed,0x261,[NrfInitBytes]::Crc($image,0x1000,[NrfInitBytes]::U32($seed,0x34)))
    [NrfInitBytes]::Put($seed,0x2a2,[NrfInitBytes]::Crc($image,0x19000,$Full.Image.AppBytes))
    [NrfInitBytes]::Put($seed,0x25c,[NrfInitBytes]::Crc($seed,0x260,195))
    [NrfInitBytes]::Put($seed,0,[NrfInitBytes]::Crc($seed,4,88))
    [Buffer]::BlockCopy($seed,0,$image,0x2e000,4096);[Buffer]::BlockCopy($seed,0,$image,0x2f000,4096)
    return [ordered]@{'internal.bin'=$image;'settings.bin'=(New-InitJournal $Full $Config);'gui-assets.bin'=$Full.Assets;'source-full.zip'=$Full.Raw;'board.toml'=$Config.Raw}
}
function Write-InitNewFile([string]$Path,[byte[]]$Bytes) {
    $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$stream.Write($Bytes,0,$Bytes.Length)}finally{$stream.Dispose()}
}
function Save-InitJson([string]$Path,$Object) {
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Object|ConvertTo-Json -Depth 12)+"`n")
    Write-InitNewFile $Path $bytes
}
# The two external regions use their own address space, separate from the MCU.
$script:InitHexSegments=@(
    @{file='internal.hex';raw='internal.bin';space='internal';address=0},
    @{file='settings.hex';raw='settings.bin';space='external';address=0},
    @{file='gui-assets.hex';raw='gui-assets.bin';space='external';address=8192}
)
function Get-InitHexFiles($Data) {
    $files=[ordered]@{}
    foreach($segment in $script:InitHexSegments) {
        $files[$segment.file]=[NrfInitBytes]::WriteIntelHex($Data[$segment.raw],$segment.address)
    }
    $files['source-full.zip']=$Data['source-full.zip']
    $files['board.toml']=$Data['board.toml']
    return $files
}
function New-InitMaterials($Full,$Config,[string]$OutputDirectory) {
    $destination=[IO.Path]::GetFullPath($OutputDirectory)
    Assert-InitCondition (-not (Test-Path -LiteralPath $destination)) 'Output already exists; choose a new directory'
    $data=New-InitMaterialData $Full $Config
    $null=[IO.Directory]::CreateDirectory($destination)
    $files=[ordered]@{}
    $outputFiles=Get-InitHexFiles $data
    foreach($name in $outputFiles.Keys) {
        $bytes=$outputFiles[$name]
        Write-InitNewFile (Join-Path $destination $name) $bytes
        $files[$name]=[ordered]@{bytes=$bytes.Length;sha256=[NrfInitBytes]::Hash($bytes)}
    }
    $manifest=[ordered]@{format='inktag-nrf52811-initialization-v2';mcu='nrf52811';hwVersion=52;board='EL022HWRL';sourceFirmwareSha=$Full.PackageSha;sourceConfigSha=$Config.Sha;
        identityPolicy='Nordic SDK manufactured lifetime address; no host identity or QR in the generic image';
        clearScope='New ordinary defaults and health/crash state; old image heads invalidated, pixel payload not securely erased; Free-only, no activation record';
        segments=@(foreach($segment in $script:InitHexSegments) {
            @{file=$segment.file;space=$segment.space;address=$segment.address;bytes=$data[$segment.raw].Length}
        });
        skips=@(@{space='FICR';address=268435456;bytes=4096},@{space='UICR';address=268439552;bytes=4096},@{space='external';address=20480;bytes=20480});
        imageHeads=@{space='external';start=40960;end=524288;stride=4096;bytesPerHead=4;action='invalidate'};files=$files}
    Save-InitJson (Join-Path $destination 'addresses.json') $manifest
    return [pscustomobject]@{Directory=$destination;Manifest=$manifest;Data=$data;Full=$Full;Config=$Config}
}
function Read-InitMaterials([string]$Directory) {
    $root=[IO.Path]::GetFullPath($Directory)
    $manifest=ConvertFrom-InitJson (Read-InitFile (Join-Path $root 'addresses.json') 32768)
    Assert-InitKeys $manifest @('format','mcu','hwVersion','board','sourceFirmwareSha','sourceConfigSha','identityPolicy','clearScope','segments','skips','imageHeads','files')
    Assert-InitCondition ($manifest.format -ceq 'inktag-nrf52811-initialization-v2' -and $manifest.mcu -ceq 'nrf52811' -and $manifest.hwVersion -eq 52 -and $manifest.board -ceq 'EL022HWRL') 'Wrong initialization identity'
    $names=@($script:InitHexSegments.file)+@('source-full.zip','board.toml')
    Assert-InitKeys $manifest.files $names
    $data=@{}
    foreach($name in $names) {
        $bytes=Read-InitFile (Join-Path $root $name) 2097152
        $record=$manifest.files.$name;Assert-InitKeys $record @('bytes','sha256')
        Assert-InitCondition ($record.bytes -eq $bytes.Length -and $record.sha256 -ceq [NrfInitBytes]::Hash($bytes)) "Initialization member changed: $name"
        $data[$name]=$bytes
    }
    $full=Get-InitFull (Join-Path $root 'source-full.zip');$config=Get-InitConfig (Join-Path $root 'board.toml')
    Assert-InitCondition ($manifest.sourceFirmwareSha -ceq $full.PackageSha -and $manifest.sourceConfigSha -ceq $config.Sha) 'Initialization provenance changed'
    $expected=New-InitMaterialData $full $config
    $expectedFiles=Get-InitHexFiles $expected
    foreach($name in $names){Assert-InitCondition ([NrfInitBytes]::Equal($data[$name],$expectedFiles[$name])) "Initialization bytes differ from source/config: $name"}
    $segments=@($manifest.segments)
    Assert-InitCondition ($segments.Count -eq 3) 'Exactly three declared Flash segments required'
    for($i=0;$i -lt 3;$i++) {
        $s=$segments[$i];Assert-InitKeys $s @('file','space','address','bytes')
        $segment=$script:InitHexSegments[$i]
        Assert-InitCondition ($s.file -ceq $segment.file -and $s.space -ceq $segment.space -and $s.address -eq $segment.address -and $s.bytes -eq $expected[$segment.raw].Length) 'Wrong segment address/space/size'
    }
    $skips=@($manifest.skips);Assert-InitCondition ($skips.Count -eq 3) 'Missing factory skip ranges'
    $skipSpace=@('FICR','UICR','external');$skipAddress=@(268435456,268439552,20480);$skipLength=@(4096,4096,20480)
    for($i=0;$i -lt 3;$i++) {
        Assert-InitKeys $skips[$i] @('space','address','bytes')
        Assert-InitCondition ($skips[$i].space -ceq $skipSpace[$i] -and $skips[$i].address -eq $skipAddress[$i] -and $skips[$i].bytes -eq $skipLength[$i]) 'Wrong factory skip range'
    }
    Assert-InitKeys $manifest.imageHeads @('space','start','end','stride','bytesPerHead','action')
    $heads=$manifest.imageHeads
    Assert-InitCondition ($heads.space -ceq 'external' -and $heads.start -eq 40960 -and $heads.end -eq 524288 -and $heads.stride -eq 4096 -and $heads.bytesPerHead -eq 4 -and $heads.action -ceq 'invalidate') 'Wrong image-head range'
    return [pscustomobject]@{Directory=$root;Manifest=$manifest;Data=$expected;Full=$full;Config=$config}
}
function Select-InitFirmwareFile([string]$Title) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog=[Windows.Forms.OpenFileDialog]::new()
    try {
        $dialog.Title=$Title
        $dialog.Filter='Full firmware ZIP (*.zip)|*.zip'
        $dialog.CheckFileExists=$true
        $dialog.Multiselect=$false
        if($dialog.ShowDialog() -eq [Windows.Forms.DialogResult]::OK){return $dialog.FileName}
        return ''
    } finally {$dialog.Dispose()}
}
