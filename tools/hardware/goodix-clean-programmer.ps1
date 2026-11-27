# GR orchestration over the existing native Goodix/J-Link backends.
# No probe is touched when this module is imported or materials are generated.
. (Join-Path $PSScriptRoot 'goodix-clean-format.ps1')
function Read-GrText {
    param([string]$Prompt)
    if([Console]::IsInputRedirected){Write-Host ($Prompt+':');$value=[Console]::ReadLine();Assert-Gr ($null -ne $value) 'Input ended before required answer';return $value}
    Read-Host $Prompt
}
function Write-GrJson {
    param([string]$Path,$Value)
    [IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 30)+"`n",(New-Object Text.UTF8Encoding($false)))
}
function New-GrCleanMaterials {
    param([string]$Firmware,[string]$Config,[string]$Output,[string]$Port)
    Assert-Gr (!(Test-Path -LiteralPath $Output)) 'Output already exists; choose a new directory'
    $selected=Read-GrConfig $Config
    Assert-Gr (($Port -ceq 'gr5331') -eq ($selected.chip -ceq 'GR5331')) 'Wrong tool directory for selected PCB'
    $package=Read-GrFirmware $Firmware $selected.chip;$prepared=New-GrCleanImage $package $selected
    $out=[IO.Path]::GetFullPath($Output);$null=New-Item -ItemType Directory -Path $out
    # Bind the exact verified input bytes, never a downloaded/default firmware.
    [IO.File]::WriteAllBytes((Join-Path $out 'components.zip'),$package.raw)
    $board='board'+[IO.Path]::GetExtension($Config).ToLowerInvariant();[IO.File]::WriteAllBytes((Join-Path $out $board),$selected.raw)
    [IO.File]::WriteAllBytes((Join-Path $out 'mcu.bin'),$prepared.image)
    $prepared.report['files']=@{firmware='components.zip';config=$board;image='mcu.bin'}
    $prepared.report['generationSucceeded']=$true;$prepared.report['writeSucceeded']=$false;$prepared.report['readbackMatched']=$false
    Write-GrJson (Join-Path $out 'flash-map.json') $prepared.report
    return $prepared.report
}
function Read-GrCleanMaterials {
    param([string]$Path,[string]$Port)
    $folder=[IO.Path]::GetFullPath($Path);$m=Get-GrJson ([InkTag.GoodixBytes]::ReadFile((Join-Path $folder 'flash-map.json'),65536))
    Assert-Gr ($m.schema -ceq 'inktag-gr-clean-bin-v1' -and (!$m.files.gui -or $m.files.gui -ceq 'gui-font.bin') -and $m.files.config -cin @('board.toml','board.json') -and !$m.addressRetained -and !$m.proRetained -and $m.vendorRecordsRetained -eq 0) 'Not generic clean-initialization materials'
    $c=Read-GrConfig (Join-Path $folder $m.files.config);Assert-Gr (($Port -ceq 'gr5331') -eq ($c.chip -ceq 'GR5331')) 'Wrong tool directory for PCB'
    $firmwareDir=$folder
    if($m.files.firmware -ceq 'components.zip') {
        Assert-Gr ($m.files.image -ceq 'mcu.bin') 'Invalid legacy BIN filename'
    } else {
        Assert-Gr ($m.files.firmware -cmatch '^InkTag-(BASIC|PRO)-[0-9.]+-gr[A-Za-z0-9_]+-full\.zip$' -and $m.files.image -cmatch '^InkTag-(BASIC|PRO)-[0-9.]+-gr[A-Za-z0-9_]+-[A-Za-z0-9_.-]+-flash\.bin$') 'Invalid release firmware or BIN filename'
        $firmwareDir=[IO.Path]::GetFullPath((Join-Path $folder '../../full'))
    }
    $firmwarePath=Join-Path $firmwareDir $m.files.firmware
    $f=Read-GrFirmware $firmwarePath $c.chip;$gui=$f.gui
    # New materials use the already-bound component resource. Read historical
    # v1 materials without changing their GUI/input bindings or bytes.
    if($m.files.gui){$gui=[InkTag.GoodixBytes]::ReadFile((Join-Path $folder 'gui-font.bin'),32768)}
    Assert-GrRecord $f.raw $m.package;Assert-GrRecord $c.raw $m.config;Assert-GrRecord $gui $m.gui
    $prepared=New-GrCleanImage $f $c -Gui $gui;$image=[InkTag.GoodixBytes]::ReadFile((Join-Path $folder $m.files.image),1048576)
    Assert-GrRecord $image $m.image;Assert-Gr ([InkTag.GoodixBytes]::Equal($image,$prepared.image) -and $m.chip -ceq $c.chip -and $m.family -ceq $f.family -and $m.backend -ceq $c.backend) 'BIN differs from verified components/configuration'
    @{firmware=$f;config=$c;prepared=$prepared;firmwarePath=$firmwarePath}
}
function Invoke-GrNative {
    param([string]$File,[string[]]$Arguments,[string]$Folder,[string]$Label,[int]$Timeout=300,[switch]$OptionalRead,[switch]$CancelWait)
    $out=Join-Path $Folder ($Label+'.stdout.log');$err=Join-Path $Folder ($Label+'.stderr.log')
    Assert-Gr (!(Test-Path -LiteralPath $out) -and !(Test-Path -LiteralPath $err)) 'Native log collision; never replay a prior operation'
    # Standard Windows argv quoting, including trailing backslashes/spaces.
    $quoted=@($Arguments|ForEach-Object{if($_ -notmatch '[\s"]' -and $_.Length){$_}else{'"'+([regex]::Replace([regex]::Replace($_,'(\\*)"','$1$1\"'),'(\\+)$','$1$1'))+'"'}})
    $process=Start-Process -FilePath $File -ArgumentList $quoted -WindowStyle Hidden -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
    # Keep the native process handle before a short-lived console exits. On
    # Windows PowerShell5.1 a detached Process may otherwise lose ExitCode.
    $null=$process.Handle
    $cancelled=$false
    if($CancelWait) {
        $elapsed=[Diagnostics.Stopwatch]::StartNew()
        while(!$process.WaitForExit(100)) {
            $cancelled=Test-GrWaitCancelled
            if($cancelled -or $elapsed.Elapsed.TotalSeconds -ge $Timeout){break}
        }
    }
    if(!$process.WaitForExit($(if($CancelWait){0}else{$Timeout*1000}))) {
        if($CancelWait) {
            # Detection runs inside a J-Link child of the PowerShell adapter.
            & (Join-Path $env:SystemRoot 'System32/taskkill.exe') /PID $process.Id /T /F 2>$null | Out-Null
        } else {
            try{$process.Kill()}catch{}
        }
        if(!$cancelled){throw ('Native operation timed out; no automatic replay: '+$Label)}
    }
    if($cancelled){throw $script:GrGuideText.waitCancelled}
    $process.WaitForExit();$text=[IO.File]::ReadAllText($out)+"`n"+[IO.File]::ReadAllText($err);$code=$process.ExitCode;$process.Dispose()
    $failed=$code -ne 0 -or $text -match '(?im)\bfailed(?:[.:]|\s*$)|failed to (?:connect|read|write|erase|program)\b|could not read|syntax error|unknown command|not_supported|cannot connect|could not connect|no target|no (?:J-Link|probe) (?:found|connected)'
    if($failed) {
        # A failed physical connection is never optional-data absence.
        if($OptionalRead -and $text -notmatch '(?i)cannot connect|could not connect|no target|no (?:J-Link|probe) (?:found|connected)|transport interrupted|timed out') {return @{available=$false;text=$text;exitCode=$code}}
        $failure=New-Object InvalidOperationException ('Native stage failed; no automatic replay: '+$Label+' (exit '+$code+')')
        $failure.Data['InkTagNativeOutput']=$text
        throw $failure
    }
    @{available=$true;text=$text;exitCode=$code}
}
function Get-GrProbeIndex {
    param($Options,[switch]$CancelWait)
    $Options.counter++;$r=Invoke-GrNative $Options.console @('device','1') $Options.evidence ('probe-list-'+$Options.counter) -Timeout 30 -CancelWait:$CancelWait
    $rows=@([regex]::Matches($r.text,'(?m)^\s*(\d+)\s+.+?\s+(\d{6,})\s*$'))
    if(!$Options.snr){
        Assert-Gr ($rows.Count -gt 0) 'No J-Link probe found'
        if($rows.Count -eq 1){$Options.snr=$rows[0].Groups[2].Value}
        else {
            Assert-Gr (!$Options.yes) 'Multiple probes; pass -Snr for unattended execution'
            for($i=0;$i -lt $rows.Count;$i++){Write-Host ('{0}. J-Link {1}' -f ($i+1),$rows[$i].Groups[2].Value)}
            $prompt=if($Options.guide){$Options.guide.probe}else{'Select probe number (Enter cancels)'}
            $answer=Read-GrText $prompt;$number=0
            Assert-Gr (![string]::IsNullOrWhiteSpace($answer)) 'Cancelled before probe attachment'
            Assert-Gr ([int]::TryParse($answer,[ref]$number) -and $number -ge 1 -and $number -le $rows.Count) 'Invalid probe number'
            $Options.snr=$rows[$number-1].Groups[2].Value
        }
    }
    $selected=@($rows|Where-Object{$_.Groups[2].Value -ceq $Options.snr})
    if($CancelWait -and $selected.Count -eq 0){throw 'Exact probe serial missing'}
    Assert-Gr ($selected.Count -eq 1) 'Exact probe serial missing/ambiguous'
    return $selected[0].Groups[1].Value
}
function Get-GrLiveIdentity {
    param($Options,$Config,$Firmware)
    $script=Join-Path $script:GrCleanRoot 'tools/platforms/goodix/common/programmer/internal/goodix-stock-entry.ps1'
    $waiting=$Options.retryDetect
    while($true) {
        if($waiting){
            if(Test-GrWaitCancelled){throw $Options.guide.waitCancelled}
            Write-Host $Options.guide.waiting
        }
        try {
            $null=Get-GrProbeIndex $Options -CancelWait:$waiting
            $Options.counter++;$label='detect-'+$Options.counter;$probe=Join-Path $Options.evidence $label;$result=$probe+'.json'
            $nativeArgs=@('-NoProfile','-ExecutionPolicy','Bypass','-File',$script,'-Action','probe','-Chip',$Config.chip.ToLowerInvariant(),'-EvidenceDir',$probe,'-ResultFile',$result,'-Snr',$Options.snr,'-JLinkExe',$Options.jlink,'-FrequencyHz','500000','-LeaveHalted')
            # GR5331 part detection needs the SDK trim cache to identify the chip.
            if($Config.chip -ceq 'GR5331'){$nativeArgs+='-ReadProductionCache'}
            if($waiting){$nativeArgs+='-CaptureWindow'}
            $null=Invoke-GrNative $Options.powershell $nativeArgs $Options.evidence $label -Timeout 60 -CancelWait:$waiting
            break
        } catch {
            if(!$waiting){throw}
            $missing = $_.Exception.Message -cin @('No J-Link probe found','Exact probe serial missing') -or
                $_.Exception.Data['InkTagNativeOutput'] -match '(?im)\bno (?:target(?: connected)?|(?:J-Link(?: probe)?|probe) (?:found|connected))\b|\b(?:cannot|could not|failed to) connect to (?:the )?target\b'
            $partial = $_.Exception.Data['InkTagNativeOutput'] -match '(?im)E000ED00\s*=|\bConnected successfully\b|unknown command|syntax error|licen[cs]e|access denied|timed out|timeout'
            if(!$missing -or $partial){throw}
            Start-Sleep -Milliseconds 500
        }
    }
    $identity=Get-GrJson ([IO.File]::ReadAllBytes($result)) -AllowBom;$g=$Firmware.geometry
    $packages=if($Config.chip -ceq 'GR5331'){@(0x204,0x205,0x208)}elseif($Config.chip -ceq 'GR5515'){@(4)}elseif($Firmware.family -ceq 'gr5513_zkel_internal'){@(7)}else{@(5,7)}
    Assert-Gr ($identity.chipConfirmed -eq $true -and $identity.chip.ToUpperInvariant() -ceq $Config.chip -and $identity.packageId -in $packages -and $identity.flashBytes -eq $g.capacity -and $identity.baseAddress -eq $g.base -and $identity.probeSerial -ceq $Options.snr) 'Actual MCU/package/capacity/probe mismatch; no write'
    $native=@(Get-ChildItem -LiteralPath $probe -Filter '*.log'|ForEach-Object{[IO.File]::ReadAllText($_.FullName)}) -join "`n"
    $voltages=[regex]::Matches($native,'VTref\s*=\s*(\d+(?:\.\d+)?)V')
    Assert-Gr ($voltages.Count -gt 0) 'No live target voltage; no write';$voltage=[double]::Parse($voltages[$voltages.Count-1].Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture)
    Assert-Gr ($voltage -ge 2.7 -and $voltage -le 3.6) 'Unsafe live target voltage; no write'
    $uid=$null;$firstUid=$null;$secondUid=$null
    if($Config.chip -cne 'GR5331') {
        $one=Join-Path $Options.evidence 'optional-uid-first.bin';$two=Join-Path $Options.evidence 'optional-uid-second.bin'
        $r1=Invoke-GrJLinkWords $Options @(('savebin "'+$one+'" 0x00803212 0x3c')) 'optional-uid-first' -OptionalRead
        $r2=Invoke-GrJLinkWords $Options @(('savebin "'+$two+'" 0x00803212 0x3c')) 'optional-uid-second' -OptionalRead
        # Native connection failures above still throw. Missing/short optional
        # output is handled only as inability to retain this unit's Pro.
        if($null -ne $r1 -and $null -ne $r2 -and (Test-Path -LiteralPath $one) -and (Test-Path -LiteralPath $two)) {
            $firstUid=[IO.File]::ReadAllBytes($one);$secondUid=[IO.File]::ReadAllBytes($two)
        }
    }
    # A missing optional UID only drops Pro. Exact MCU detection above remains
    # mandatory; no typed factory address/complete old Flash is a prerequisite.
    try {
        if($Config.chip -ceq 'GR5331') {
            $snapshotPath=[IO.Path]::GetFullPath($identity.factorySnapshotFile)
            Assert-Gr ($snapshotPath.StartsWith([IO.Path]::GetFullPath($probe)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) 'Snapshot outside this probe operation'
            $snapshot=[IO.File]::ReadAllBytes($snapshotPath);Assert-Gr ([InkTag.GoodixBytes]::Sha($snapshot) -ceq $identity.factorySnapshotSha256 -and $snapshot.Length -eq 0x18000) 'Incomplete/changed current UID capture';$uid=[InkTag.GoodixBytes]::Slice($snapshot,0x24,16)
        } else {
            $snapshot=$firstUid
            Assert-Gr ($null -ne $snapshot -and $snapshot.Length -eq 60 -and [InkTag.GoodixBytes]::Equal($snapshot,$secondUid) -and $snapshot[28] -eq 68 -and $snapshot[29] -eq 71 -and ($snapshot[2] -band 31) -eq $identity.packageId) 'Unbound current UID capture';$uid=[InkTag.GoodixBytes]::Slice($snapshot,44,16)
        }
        Assert-Gr (![InkTag.GoodixBytes]::Uniform($uid,0) -and ![InkTag.GoodixBytes]::Uniform($uid,255)) 'Invalid current UID';
    } catch {$uid=$null}
    @{identity=$identity;uid=$uid;voltage=$voltage}
}
function Read-GrNativeFlash {
    param($Options,$Identity,[int]$Address,[int]$Bytes,[string]$Label,[switch]$Optional)
    $index=Get-GrProbeIndex $Options;$Options.counter++;$file=Join-Path $Options.evidence ($Label+'-'+$Options.counter+'.bin')
    $nativeArgs=@('readFlash','--chip',$Identity.chip.ToUpperInvariant(),'--flashSize',[string]($Identity.flashBytes/1024),'--address',('0x{0:X8}' -f $Address),'--size',[string]$Bytes,'--save',$file,'--jlink',$index)
    $r=Invoke-GrNative $Options.console $nativeArgs $Options.evidence ($Label+'-'+$Options.counter) -OptionalRead:$Optional
    if(!$r.available){return $null}
    if(!(Test-Path -LiteralPath $file) -or (Get-Item -LiteralPath $file).Length -ne $Bytes){if($Optional){return $null};throw 'Incomplete mandatory Flash readback'}
    return ,([IO.File]::ReadAllBytes($file))
}
function Invoke-GrJLinkWords {
    param($Options,[string[]]$Commands,[string]$Label,[switch]$OptionalRead)
    $Options.counter++;$script=Join-Path $Options.evidence ($Label+'-'+$Options.counter+'.jlink')
    [IO.File]::WriteAllLines($script,(@('exec SetRestartOnClose = 0','connect','halt')+$Commands+@('qc')),[Text.Encoding]::ASCII)
    $r=Invoke-GrNative $Options.jlink @('-SelectEmuBySN',$Options.snr,'-Device','Cortex-M4','-If','SWD','-Speed','500','-NoGui','1','-AutoConnect','0','-ExitOnError','1','-CommanderScript',$script) $Options.evidence ($Label+'-'+$Options.counter) -Timeout 15 -OptionalRead:$OptionalRead
    if(!$r.available){return $null}
    return $r.text
}
function Get-GrResetWord {
    param([string]$Text,[uint32]$Address)
    $rows=[regex]::Matches($Text,'(?mi)^\s*(?:0x)?([0-9a-f]{8})\s*=\s*([0-9a-f]{8})\s*$')
    Assert-Gr ($rows.Count -eq 1 -and [Convert]::ToUInt32($rows[0].Groups[1].Value,16) -eq $Address) 'Missing/ambiguous reset status; remains halted'
    [Convert]::ToUInt32($rows[0].Groups[2].Value,16)
}
function Clear-GrStaleResetCause {
    param($Options,[string]$Chip)
    $small=$Chip -ceq 'GR5331';[uint32]$address=if($small){0x4000a384}else{2684405060L};[uint32]$mask=if($small){4}else{64}
    $text=Invoke-GrJLinkWords $Options @(('mem32 0x{0:X8}, 1' -f $address)) 'reset-cause-before';$before=Get-GrResetWord $text $address;$commands=@()
    if($before -band $mask){[uint32]$write=if($small){($before -band ([uint32]::MaxValue -bxor 63)) -bor $mask}else{[uint32]::MaxValue -bxor $mask};$commands+=('w4 0x{0:X8}, 0x{1:X8}' -f $address,$write)}
    $commands+=('mem32 0x{0:X8}, 1' -f $address);$after=Get-GrResetWord (Invoke-GrJLinkWords $Options $commands 'reset-cause-readback') $address
    Assert-Gr (($after -band $mask) -eq 0 -and ($after -band ([uint32]::MaxValue -bxor $mask)) -eq ($before -band ([uint32]::MaxValue -bxor $mask))) 'Reset cleanup/readback mismatch; remains halted'
    @{address=$address;before=$before;after=$after;staleWatchdogCauseCleared=$true;unrelatedBitsPreserved=$true}
}
function Invoke-GrCleanFlash {
    param($Options,$Firmware,$Config,[switch]$Preserve)
    # One sequence only: exact detect, optional records, one write, mandatory
    # complete readback, existing watchdog cleanup, one native reset.
    $started=$false;$report=@{writeSucceeded=$false;readbackMatched=$false;applicationStartResetRequested=$false;automaticWriteRetry=$false;hardwareQualified=$false}
    try {
        $live=Get-GrLiveIdentity $Options $Config $Firmware;$g=$Firmware.geometry;$old=$null
        if($Preserve) {
            $size=if($Firmware.family -ceq 'gr5513_zkel_internal'){16384}else{$g.nvdsBytes};$address=$g.base+$g.capacity-$size
            $first=Read-GrNativeFlash $Options $live.identity $address $size 'optional-nvds-first' -Optional
            $second=Read-GrNativeFlash $Options $live.identity $address $size 'optional-nvds-second' -Optional
            if($null -ne $first -and [InkTag.GoodixBytes]::Equal($first,$second)){$old=$first}
        }
        $prepared=New-GrCleanImage $Firmware $Config -OldNvds $old -Uid $live.uid
        $prepared.report['hardwareAccessed']=$true;$prepared.report['probeSerial']=$Options.snr;$prepared.report['targetVoltage']=$live.voltage
        $image=Join-Path $Options.evidence 'prepared-mcu.bin';[IO.File]::WriteAllBytes($image,$prepared.image)
        Write-GrJson (Join-Path $Options.evidence 'prepared-flash-map.json') $prepared.report
        Write-Host ('Board: {0} ({1}); firmware: {2}' -f $Config.facts.board_marking,$Config.chip,$Options.firmwarePath)
        Write-Host ('Retained after verification: address={0}, Pro={1}, SDK overrides={2}' -f $prepared.report.addressRetained,$prepared.report.proRetained,$prepared.report.vendorRecordsRetained)
        Write-Host 'Clear: ordinary settings, password, Find, image index and health history. External image Flash is skipped.'
        Write-Host 'MCU image includes GUI resources; no separate GUI write.'
        $null=Get-GrProbeIndex $Options
        $nativeScript=Join-Path $script:GrCleanRoot 'tools/platforms/goodix/common/programmer/internal/flash-goodix-gprogrammer.ps1'
        $nativeArgs=@('-NoProfile','-ExecutionPolicy','Bypass','-File',$nativeScript,'-Target',$(if($Config.chip -ceq 'GR5331'){'gr5331'}else{'gr5515'}),'-Mode','download','-Layout','ota','-Firmware',$image,'-Snr',$Options.snr,'-ConsoleExe',$Options.console,'-GProgrammerDir',[IO.Path]::GetDirectoryName($Options.console),'-FlashStart',('0x{0:X8}' -f $g.base),'-DownloadAddress',('0x{0:X8}' -f $g.base),'-FlashSizeKB',[string]($g.capacity/1024),'-ProductType',$(if($Config.chip -ceq 'GR5331'){'2'}else{'0'}),'-NoRunImmediately')
        # No erase-all command: OTP/security/other address spaces are excluded.
        # Untrimmed MCU BIN includes erased user tail and has explicit capacity.
        $started=$true;$null=Invoke-GrNative $Options.powershell $nativeArgs $Options.evidence 'single-mcu-write'
        $report.writeSucceeded=$true;$back=Read-GrNativeFlash $Options $live.identity $g.base $g.capacity 'mandatory-full-readback'
        Assert-Gr ([InkTag.GoodixBytes]::Equal($back,$prepared.image)) 'Complete readback mismatch; no reset or write replay'
        $report.readbackMatched=$true;$report['resetBoundary']=Clear-GrStaleResetCause $Options $Config.chip
        $index=Get-GrProbeIndex $Options;$null=Invoke-GrNative $Options.console @('reset','--chip',$Config.chip,'--jlink',$index) $Options.evidence 'start-verified-app'
        $report.applicationStartResetRequested=$true;$report['preparation']=$prepared.report;$report['displayObserved']=$false;$report['bleConnected']=$false;$report['clockSynchronized']=$false
        Write-GrJson (Join-Path $Options.evidence 'result.json') $report
        return $report
    } catch {
        $report['writeStarted']=$started;$report['error']=$_.Exception.Message
        Write-GrJson (Join-Path $Options.evidence 'result.json') $report
        # Do not issue another attach/reset/write after failure or unknown
        # completion. Raw logs say whether the native process touched target.
        throw
    }
}
