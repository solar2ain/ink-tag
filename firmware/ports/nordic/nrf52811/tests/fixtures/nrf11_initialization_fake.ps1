# Test-only process boundary. Appended to a temporary copy of the unchanged
# J-Link helper; production scripts have no fake mode or environment hook.
$script:NrfFake=$null
$script:NrfRealWaitCancellation=${function:Test-InkTagWaitCancellation}
function Test-InkTagWaitCancellation {
    if($env:INKTAG_NRF_TEST_CASE -eq 'cancel-wait' -and $script:NrfFake -and $script:NrfFake.EnumCount -ge 2){throw [OperationCanceledException]::new('test cancellation while waiting')}
    & $script:NrfRealWaitCancellation
}
function Invoke-InkTagJLinkProcess {
    param($FilePath,$Arguments,$StdoutPath,$StderrPath,$Label,$OperationTimeoutSeconds)
    if($null -eq $script:NrfFake) {
        $f=[NrfInitBytes]::Filled(4096,255)
        [NrfInitBytes]::Put($f,0x10,4096);[NrfInitBytes]::Put($f,0x14,48)
        [NrfInitBytes]::Put($f,0x100,0x52811);[NrfInitBytes]::Put($f,0x10c,24);[NrfInitBytes]::Put($f,0x110,192)
        [NrfInitBytes]::Put($f,0x60,0x01020304);[NrfInitBytes]::Put($f,0x64,0x10203040)
        [NrfInitBytes]::Put($f,0xa0,1);[NrfInitBytes]::Put($f,0xa4,0x12345678);[NrfInitBytes]::Put($f,0xa8,0xc234)
        $script:NrfFake=@{Mode=$env:INKTAG_NRF_TEST_CASE;Flash=[NrfInitBytes]::Filled(196608,90);External=[NrfInitBytes]::Filled(524288,105);Ram=[NrfInitBytes]::Filled(24576,165);Ficr=$f;Uicr=[NrfInitBytes]::Filled(4096,167);Registers=@{1073742848=[uint32]0x5000f;1073743132=[uint32]0xb4;1073743136=[uint32]127};Calls=[Collections.Generic.List[object]]::new();FlashWrites=0;ExternalWrites=0;ResetCount=0;AppStarts=0;PC=0;Primask=0;Control=3;Xpsr=0;Restart=$true;Halted=$true}
        if($script:NrfFake.Mode -eq 'wrong-chip'){[NrfInitBytes]::Put($f,0x100,0x52832)}
        if($script:NrfFake.Mode -eq 'blank-old'){
            [Buffer]::BlockCopy([NrfInitBytes]::Filled(8192,255),0,$script:NrfFake.External,0,8192)
        }
        if($script:NrfFake.Mode -eq 'opaque-pro'){
            [Buffer]::BlockCopy([Text.Encoding]::ASCII.GetBytes('UNVERIFIED-PRO-DATA'),0,$script:NrfFake.External,0,19)
        }
        if($script:NrfFake.Mode -in @('valid-old','corrupt-old')){
            $old=[IO.File]::ReadAllBytes($env:INKTAG_NRF_TEST_OLD_JOURNAL)
            [Buffer]::BlockCopy($old,0,$script:NrfFake.External,0,$old.Length)
            if($script:NrfFake.Mode -eq 'corrupt-old'){$script:NrfFake.External[24]=$script:NrfFake.External[24] -bxor 1}
        }
    }
    $s=$script:NrfFake;$code=0;$text="J-Link Commander V7.88j`nVTref=3.300V`n"
    if($s.Mode -eq 'low-supply'){$text="J-Link Commander V7.88j`nVTref=2.600V`n"}
    $commands=@();$commandPath=$null
    for($i=0;$i -lt $Arguments.Count-1;$i++){if($Arguments[$i] -eq '-CommanderScript'){$commandPath=$Arguments[$i+1];$commands=@([IO.File]::ReadAllLines($commandPath))}}
    $s.Calls.Add(@{label=$Label;argv=$Arguments;script=$commands})
    try {
        if($s.Mode -eq 'missing-old' -and $Label -match '^old-'){throw 'injected optional old read unavailable'}
        if($s.Mode -eq 'write-fail' -and $Label -eq 'program-mcu'){throw 'injected failed write before confirmation'}
        if($s.Mode -eq 'target-changed' -and $Label -eq 'program-mcu'){$s.Ficr[0x60]=$s.Ficr[0x60] -bxor 1}
        foreach($command in $commands) {
            if($command -eq 'Exec SetRestartOnClose = 0'){$s.Restart=$false;continue}
            if($command -eq 'connect'){
                if($Label -eq 'chip-info'){$s.ConnectCount=[int]$s.ConnectCount+1}
                if($s.Mode -eq 'connect-fail' -or ($s.Mode -eq 'target-late' -and $Label -eq 'chip-info' -and $s.ConnectCount -lt 3)){throw 'cannot connect to target'}
                continue
            }
            if($command -eq 'halt'){$s.Halted=$true;continue}
            if($command -in @('ExitOnError 1','qc','exit','RReg PRIMASK','RReg CONTROL','RReg XPSR')){continue}
            if($command -eq 'ShowEmuList USB'){
                $s.EnumCount=[int]$s.EnumCount+1
                if($s.Mode -in @('cancel-wait','probe-never') -or ($s.Mode -eq 'probe-late' -and $s.EnumCount -lt 3)){continue}
                $text+="J-Link[0]: Connection: USB, Serial number: 12345, ProductName: Test`n"
                if($s.Mode -eq 'multiple-probes'){$text+="J-Link[1]: Connection: USB, Serial number: 67890, ProductName: Test`n"}
                continue
            }
            if($command -match '^WReg (PRIMASK|CONTROL|XPSR|MSP), 0x([0-9a-f]+)$') {
                $r=$Matches[1];$v=[Convert]::ToUInt32($Matches[2],16)
                if($r -eq 'PRIMASK'){$s.Primask=$v};if($r -eq 'CONTROL'){$s.Control=$v};if($r -eq 'XPSR'){$s.Xpsr=$v};continue
            }
            if($command -match '^SetPC 0x([0-9a-f]+)$'){$s.PC=[Convert]::ToUInt32($Matches[1],16);continue}
            if($command -eq 'Reset'){$s.ResetCount++;$s.PC=0x19000;$s.Halted=$true;continue}
            if($command -eq 'Go') {
                if($s.PC -ne 0x20000000){$s.AppStarts++;$s.Halted=$false;continue}
                if($s.Primask -ne 1 -or $s.Control -ne 0 -or $s.Xpsr -ne 0x01000000){throw 'unsafe worker register entry'}
                $request=[NrfInitBytes]::Slice($s.Ram,4096,48)
                $kind=[NrfInitBytes]::U32($request,4);$a=[NrfInitBytes]::U32($request,8);$length=[NrfInitBytes]::U32($request,12)
                if($kind -in @(1,2)) {
                    if($length -ne 4096 -or $a+4096 -gt 524288){throw 'bad worker range'}
                    if($kind -eq 2) {
                        if($a%4096 -or $a -ge 20480){throw 'factory sector would be written'}
                        [Buffer]::BlockCopy($s.Ram,8192,$s.External,$a,4096);$s.ExternalWrites++
                    }
                    [Buffer]::BlockCopy($s.External,$a,$s.Ram,8192,4096);$count=4096
                } else {
                    if($kind -notin @(3,4) -or $a -ne 40960 -or $length -ne 483328){throw 'bad image-head command'}
                    for($j=0;$j -lt 118;$j++) {
                        if($kind -eq 3){[Array]::Clear($s.External,40960+$j*4096,4)}
                        [Buffer]::BlockCopy($s.External,40960+$j*4096,$s.Ram,8192+$j*4,4)
                    }
                    $count=472
                }
                $reply=[NrfInitBytes]::Slice($request,0,48)
                [NrfInitBytes]::Put($reply,24,[NrfInitBytes]::U32($request,20))
                [NrfInitBytes]::Put($reply,28,0);[NrfInitBytes]::Put($reply,32,0x454e4f44);[NrfInitBytes]::Put($reply,36,0xa14013)
                [NrfInitBytes]::Put($reply,40,[NrfInitBytes]::Crc($s.Ram,8192,$count));[NrfInitBytes]::Put($reply,44,0)
                if($s.Mode -eq 'worker-write-fail' -and $kind -eq 2){[NrfInitBytes]::Put($reply,28,3)}
                [Buffer]::BlockCopy($reply,0,$s.Ram,4096,48);$s.Halted=$true;continue
            }
            if($command -eq 'WaitHalt 30000'){$s.Halted=$true;continue}
            if($command -match '^W4 0x([0-9a-f]+) (0x[0-9a-f]+|0)$') {
                $a=[int][Convert]::ToUInt32($Matches[1],16);$v=[Convert]::ToUInt32($Matches[2].Replace('0x',''),16)
                if(-not $s.Registers.ContainsKey($a)){throw 'unexpected MMIO write'}
                if($a -eq 0x40000400){$s.Registers[$a]=[uint32]($s.Registers[$a] -band (-bnot $v))}else{$s.Registers[$a]=$v};continue
            }
            if($command -match '^(loadbin|VerifyBin|SaveBin) "([^"]+)", 0x([0-9a-f]+)(?:, 0x([0-9a-f]+))?$') {
                $verb=$Matches[1];$path=$Matches[2];$address=[Convert]::ToUInt32($Matches[3],16)
                $length=if($verb -eq 'SaveBin'){[Convert]::ToInt32($Matches[4],16)}else{(Get-Item -LiteralPath $path).Length}
                $offset=$address
                if($address -lt 196608){$memory=$s.Flash}
                elseif($address -ge 0x20000000 -and $address+$length -le 0x20006000){$memory=$s.Ram;$offset=$address-0x20000000}
                elseif($address -ge 0x10000000 -and $address+$length -le 0x10001000){$memory=$s.Ficr;$offset=$address-0x10000000}
                elseif($address -ge 0x10001000 -and $address+$length -le 0x10002000){$memory=$s.Uicr;$offset=$address-0x10001000}
                elseif($s.Registers.ContainsKey([int]$address)){$memory=[BitConverter]::GetBytes([uint32]$s.Registers[[int]$address]);$offset=0}
                else{throw ('unexpected memory access '+$command)}
                if($verb -eq 'loadbin') {
                    if($address -ge 0x10000000 -and $address -lt 0x20000000){throw 'manufacturer page would be written'}
                    $data=[IO.File]::ReadAllBytes($path);[Buffer]::BlockCopy($data,0,$memory,$offset,$data.Length)
                    if($address -eq 0){$s.FlashWrites++;$text+="Downloading file...O.K.`n";if($s.Mode -eq 'late-write-fail'){throw 'write result unknown after programming'}}
                } elseif($verb -eq 'VerifyBin') {
                    if(-not [NrfInitBytes]::Equal([IO.File]::ReadAllBytes($path),[NrfInitBytes]::Slice($memory,$offset,$length))){throw 'anchor or immediate readback differs'}
                } else {
                    $data=[NrfInitBytes]::Slice($memory,$offset,$length)
                    if($s.Mode -eq 'short-old' -and $Label -match '^old-'){$data=[NrfInitBytes]::Slice($data,0,$data.Length-1)}
                    if($s.Mode -eq 'readback-fail' -and $Label -eq 'internal-readback'){$data[16]=$data[16] -bxor 1}
                    [IO.File]::WriteAllBytes($path,$data)
                }
                continue
            }
            throw ('unknown fake command '+$command)
        }
    } catch {$code=1;$text+='Error: '+$_.Exception.Message}
    [IO.File]::WriteAllText($StdoutPath,$text);[IO.File]::WriteAllText($StderrPath,'')
    $reserved=[NrfInitBytes]::Slice($s.External,20480,20480)
    $report=@{mode=$s.Mode;flashWrites=$s.FlashWrites;externalWrites=$s.ExternalWrites;resets=$s.ResetCount;appStarts=$s.AppStarts;calls=@($s.Calls);factoryUnchanged=([NrfInitBytes]::Equal($reserved,[NrfInitBytes]::Filled(20480,105)));ficrHash=[NrfInitBytes]::Hash($s.Ficr);uicrHash=[NrfInitBytes]::Hash($s.Uicr);pythonProcessCalls=0}
    [IO.File]::WriteAllText($env:INKTAG_NRF_TEST_TRACE,($report|ConvertTo-Json -Depth 12))
    return [pscustomobject]@{ExitCode=$code;Text=$text;Log=$StdoutPath}
}
