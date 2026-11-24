# Maintained J-Link7.88j + existing RAM-worker protocol. No CLI/BLE/Python.
. (Join-Path $PSScriptRoot 'initialization-jlink.ps1')

function Read-InitExact([string]$Path,[int]$Bytes) {
    $value=[IO.File]::ReadAllBytes($Path)
    Assert-InitCondition ($value.Length -eq $Bytes) "Incomplete native readback: $Path"
    return ,$value
}
function Invoke-InitNative([string]$Label,[string[]]$Commands,[switch]$Optional) {
    if($Optional){foreach($command in $Commands){Assert-InitCondition ($command -match '^SaveBin ') 'Only old-data reads may be optional'}}
    $s=$script:InitSession;$s.Counter++
    $prefix=('{0:D3}-{1}' -f $s.Counter,$Label)
    $path=Join-Path $s.Root ($prefix+'.jlink')
    Assert-InitCondition ($path.Length -lt 250 -and $path -cmatch '^[\x20-\x7e]+$' -and $path -notmatch '["\r\n]') 'J-Link7.88j needs a short ASCII log directory (spaces are supported)'
    $checks=@()
    foreach($anchor in $s.Anchors) {
        Assert-InitCondition ([NrfInitBytes]::Hash([IO.File]::ReadAllBytes($anchor.Path)) -ceq $anchor.Sha) 'Immutable target anchor file changed'
        $checks+='VerifyBin "{0}", 0x{1:x}' -f $anchor.Path,$anchor.Address
    }
    $lines=@('ExitOnError 1','Exec SetRestartOnClose = 0','connect','halt')+$checks+$Commands+@('qc')
    Write-InitNewFile $path ([Text.Encoding]::ASCII.GetBytes(($lines -join "`n")+"`n"))
    $stdout=Join-Path $s.Root ($prefix+'.stdout.log');$stderr=Join-Path $s.Root ($prefix+'.stderr.log')
    $arguments=@('-NoGui','1','-ExitOnError','1','-Device','nRF52811_xxAA','-If','SWD','-Speed',[string]($s.Frequency/1000),'-SelectEmuBySN',$s.Serial,'-AutoConnect','0','-CommanderScript',$path)
    $record=[ordered]@{label=$Label;argv=$arguments;startUtc=[DateTime]::UtcNow.ToString('o');optionalOldRead=[bool]$Optional;exitCode=$null;failure=$null;noReplay=$true}
    $text=$null
    try {
        $result=Invoke-InkTagJLinkProcess -FilePath $s.JLink -Arguments $arguments -StdoutPath $stdout -StderrPath $stderr -Label $Label -OperationTimeoutSeconds $s.Timeout
        $record.exitCode=$result.ExitCode
        Assert-InitCondition ($result.ExitCode -eq 0 -and $result.Text -notmatch '(?i)cannot|could not|\bfailed\b|error while|\berror:|unknown (?:command|register)|invalid register|syntax error|timeout|timed out') "J-Link $Label failed/unknown; no write replay"
        $volts=@([regex]::Matches($result.Text,'(?i)(?:VTref|VTarget)\s*[=:]\s*(\d+(?:\.\d+)?)\s*V') | ForEach-Object {[double]::Parse($_.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture)})
        Assert-InitCondition ($volts.Count -gt 0 -and ($volts | Measure-Object -Minimum).Minimum -ge 2.7 -and ($volts | Measure-Object -Maximum).Maximum -le 3.6) 'J-Link supply unknown/outside2.7..3.6V'
        $text=$result.Text
    } catch {
        $record.failure=$_.Exception.Message
        if(-not $Optional){throw}
        $s.Optional.Add(@{item=$Label;status='unavailable';reason=$record.failure})
    } finally {
        $record.endUtc=[DateTime]::UtcNow.ToString('o')
        Save-InitJson (Join-Path $s.Root ($prefix+'.json')) $record
    }
    return $text
}
function Read-InitTarget([string]$Label,[uint32]$Address,[int]$Length,[switch]$Optional) {
    $path=Join-Path $script:InitSession.Root ($Label+'.bin')
    $text=Invoke-InitNative $Label @('SaveBin "{0}", 0x{1:x}, 0x{2:x}' -f $path,$Address,$Length) -Optional:$Optional
    if($null -eq $text){return $null}
    try {return ,(Read-InitExact $path $Length)} catch {
        if(-not $Optional){throw}
        $script:InitSession.Optional.Add(@{item=$Label;status='unavailable';reason=$_.Exception.Message});return $null
    }
}
function Add-InitAnchor([string]$Label,[uint32]$Address,[byte[]]$Bytes) {
    $script:InitSession.Anchors.Add(@{Path=(Join-Path $script:InitSession.Root ($Label+'.bin'));Address=$Address;Sha=[NrfInitBytes]::Hash($Bytes)})
}
function Invoke-InitExternal([uint32]$Command,[uint32]$Address,[uint32]$Length,[byte[]]$Payload) {
    $s=$script:InitSession
    $nonce=New-Object byte[] 4;$rng=[Security.Cryptography.RandomNumberGenerator]::Create()
    try{$rng.GetBytes($nonce)}finally{$rng.Dispose()}
    $token=[BitConverter]::ToUInt32($nonce,0);if(-not $token){$token=1}
    $crc=if($null -ne $Payload){[NrfInitBytes]::Crc($Payload,0,$Payload.Length)}else{[uint32]0}
    $prefix='worker-{0:D3}-{1}-{2:x}' -f ($s.Counter+1),$Command,$Address
    $request=New-Object byte[] 48
    $words=[uint32[]]@(0x3157524e,$Command,$Address,$Length,$crc,$token,0,0,0,0,0,0)
    for($i=0;$i -lt 12;$i++){[NrfInitBytes]::Put($request,$i*4,$words[$i])}
    $mail=Join-Path $s.Root ($prefix+'-request.bin');$response=Join-Path $s.Root ($prefix+'-reply.bin');$output=Join-Path $s.Root ($prefix+'-data.bin')
    Write-InitNewFile $mail $request
    $commands=@('WReg PRIMASK, 0x1','WReg CONTROL, 0x0','WReg XPSR, 0x01000000',('loadbin "{0}", 0x20000000' -f $s.Worker),('loadbin "{0}", 0x20001000' -f $mail))
    if($null -ne $Payload){
        Assert-InitCondition ($Command -eq 2 -and $Payload.Length -eq 4096) 'Worker write must be one configured sector'
        $source=Join-Path $s.Root ($prefix+'-source.bin');Write-InitNewFile $source $Payload
        $commands+='loadbin "{0}", 0x20002000' -f $source
    }
    $count=if($Command -eq 1 -or $Command -eq 2){4096}else{472}
    $commands+=@('WReg MSP, 0x20005F00','SetPC 0x20000000','RReg PRIMASK','RReg CONTROL','RReg XPSR','Go','WaitHalt 30000',('SaveBin "{0}", 0x20001000, 0x30' -f $response),('SaveBin "{0}", 0x20002000, 0x{1:x}' -f $output,$count))
    $null=Invoke-InitNative $prefix $commands
    $data=Read-InitExact $output $count;$reply=Read-InitExact $response 48
    $expected=[uint32[]]@(0x3157524e,$Command,$Address,$Length,$crc,$token,$token,0,0x454e4f44,0xa14013,[NrfInitBytes]::Crc($data,0,$data.Length),0)
    for($i=0;$i -lt 12;$i++){Assert-InitCondition ([NrfInitBytes]::U32($reply,4*$i) -eq $expected[$i]) 'Worker did not confirm exact operation; no replay/reset'}
    if($null -ne $Payload){Assert-InitCondition ([NrfInitBytes]::Equal($data,$Payload)) 'External write/readback differs; no reset/replay'}
    if($Command -eq 3){Assert-InitCondition ([NrfInitBytes]::Equal($data,(New-Object byte[] 472))) 'Image-head clear not confirmed'}
    return ,$data
}
function Clear-InitVolatile([byte[]]$ExpectedFlash) {
    $s=$script:InitSession
    $registers=@(@('resetreas',0x40000400,0x0005000f),@('gpregret',0x4000051c,255),@('gpregret2',0x40000520,255))
    $commands=@()
    foreach($r in $registers){$commands+='SaveBin "{0}", 0x{1:x}, 0x4' -f (Join-Path $s.Root ($r[0]+'-before.bin')),$r[1]}
    $null=Invoke-InitNative 'read-volatile-state' $commands
    foreach($r in $registers){$v=[NrfInitBytes]::U32((Read-InitExact (Join-Path $s.Root ($r[0]+'-before.bin')) 4),0);Assert-InitCondition (($v -band (-bnot [uint32]$r[2])) -eq 0) 'Unknown physical reset/retention read'}
    $zero=Join-Path $s.Root 'ram-zero.bin';Write-InitNewFile $zero (New-Object byte[] 24576)
    $ram=Join-Path $s.Root 'ram-readback.bin';$boot=Join-Path $s.Root 'boot-readback-after-cleanup.bin'
    $commands=@(('loadbin "{0}", 0x20000000' -f $zero),('VerifyBin "{0}", 0x20000000' -f $zero),'W4 0x4000051c 0','W4 0x40000520 0','W4 0x40000400 0x5000f',('SaveBin "{0}", 0x20000000, 0x6000' -f $ram),('SaveBin "{0}", 0x2e000, 0x2000' -f $boot))
    foreach($r in $registers){$commands+='SaveBin "{0}", 0x{1:x}, 0x4' -f (Join-Path $s.Root ($r[0]+'-after.bin')),$r[1]}
    $null=Invoke-InitNative 'clear-volatile-state' $commands
    Assert-InitCondition ([NrfInitBytes]::Equal((Read-InitExact $ram 24576),(New-Object byte[] 24576))) 'RAM cleanup readback differs'
    foreach($r in $registers){Assert-InitCondition ([NrfInitBytes]::U32((Read-InitExact (Join-Path $s.Root ($r[0]+'-after.bin')) 4),0) -eq 0) 'Retention/reset cleanup differs'}
    Assert-InitCondition ([NrfInitBytes]::Equal((Read-InitExact $boot 8192),[NrfInitBytes]::Slice($ExpectedFlash,0x2e000,8192))) 'Boot metadata changed during RAM cleanup'
}

function Invoke-InitFlash($Materials,[string]$Mode,[string]$JLinkExe,[string]$Serial,[string]$OutputDirectory,[int]$FrequencyHz=500000,[int]$TimeoutSeconds=180,[switch]$RetryDetect) {
    Assert-InitCondition ($Mode -in @('Clean','Preserve') -and $FrequencyHz -ge 1000 -and $FrequencyHz%1000 -eq 0) 'Invalid programmer mode/frequency'
    $root=[IO.Path]::GetFullPath($OutputDirectory);Assert-InitCondition (-not (Test-Path -LiteralPath $root)) 'Log directory exists'
    $null=[IO.Directory]::CreateDirectory($root)
    $script:InitSession=@{Root=$root;Counter=0;Anchors=[Collections.Generic.List[object]]::new();Optional=[Collections.Generic.List[object]]::new();JLink=$null;Serial=$null;Frequency=$FrequencyHz;Timeout=$TimeoutSeconds;Worker=$null;McuAttempted=$false;McuConfirmed=$false;ExternalConfirmed=0;ProgrammingSucceeded=$false;ReadbackMatched=$false;ResetRequested=$false}
    $s=$script:InitSession
    $s.WaitingForDevice=[bool]$RetryDetect
    try {
        $s.JLink=Resolve-InkTagJLinkTool -ExplicitPath $JLinkExe
        Assert-InitCondition ([IO.Path]::GetFileName($s.JLink) -ieq 'JLink.exe') 'Pinned Windows JLink.exe required'
        $lock=ConvertFrom-InitJson (Read-InitFile (Join-Path $PSScriptRoot 'initialization-dependencies.json') 16384)
        foreach($file in $lock.native.files) {
            $actual=Read-InitFile (Join-Path (Split-Path -Parent $s.JLink) $file.name) 67108864
            Assert-InitCondition ([NrfInitBytes]::Hash($actual) -ceq $file.sha256) 'Install pinned J-Link7.88j; native dependency differs'
        }
        Assert-InkTagJLink788j -JLinkExe $s.JLink -LogDir $root
        $s.Serial=Resolve-InkTagJLinkSerial -Snr $Serial -JLinkExe $s.JLink -LogDir $root -WaitForDevice:$RetryDetect
        $worker=Read-InitFile (Join-Path (Split-Path -Parent $PSScriptRoot) 'assets/full-writer.bin') 4096
        Assert-InitCondition ($worker.Length -eq 1176 -and [NrfInitBytes]::Hash($worker) -ceq 'd3e34c2be735e84c484198b95c9b10c92798a20c1abacfbb8af3e3a26b854acf') 'RAM worker integrity mismatch'
        $s.Worker=Join-Path $root 'worker.bin';Write-InitNewFile $s.Worker $worker
        while($true) {
            try {$info=Read-InitTarget 'chip-info' 0x10000100 20;break}
            catch {
                if(-not $RetryDetect -or $_.Exception -is [OperationCanceledException]){throw}
                $log=Join-Path $root ('{0:D3}-chip-info.stdout.log' -f $s.Counter)
                $text=if(Test-Path -LiteralPath $log){[IO.File]::ReadAllText($log)}else{''}
                if($text -notmatch '(?i)cannot connect to (?:the )?(?:target|J-Link)|could not connect to (?:the )?target|no emulators connected|VTref\s*=\s*0\.0+\s*V'){throw}
                Wait-InkTagDeviceRetry $script:InitMessages.waitingTarget
            }
        }
        $pages=Read-InitTarget 'chip-pages' 0x10000010 8
        Assert-InitCondition ([NrfInitBytes]::U32($info,0) -eq 0x52811 -and [NrfInitBytes]::U32($info,12) -eq 24 -and [NrfInitBytes]::U32($info,16) -eq 192 -and [NrfInitBytes]::U32($pages,0) -eq 4096 -and [NrfInitBytes]::U32($pages,4) -eq 48) 'Exact nRF52811 24KiB/192KiB target required'
        Add-InitAnchor 'chip-info' 0x10000100 $info;Add-InitAnchor 'chip-pages' 0x10000010 $pages
        $s.WaitingForDevice=$false
        if($Mode -eq 'Preserve') {
            foreach($item in @(@('old-uid',0x10000060,8),@('old-address',0x100000a0,12),@('old-uicr',0x10001000,4096))) {
                $value=Read-InitTarget $item[0] $item[1] $item[2] -Optional
                if($null -ne $value) {
                    $empty=[NrfInitBytes]::Equal($value,[NrfInitBytes]::Filled($value.Length,255)) -or [NrfInitBytes]::Equal($value,(New-Object byte[] $value.Length))
                    $s.Optional.Add(@{item=$item[0];status=$(if($empty){'blank-no-record'}else{'read-only-unchanged'})})
                    if(-not $empty){Add-InitAnchor $item[0] $item[1] $value}
                }
            }
        }
        # Verify the fitted A14013 external Flash through the existing worker;
        # contents (including blank/corrupt old settings) are not admission data.
        $null=Invoke-InitExternal 1 0 4096
        $main=Join-Path $root 'internal.bin';Write-InitNewFile $main $Materials.Data['internal.bin']
        $s.McuAttempted=$true
        $text=Invoke-InitNative 'program-mcu' @('loadbin "{0}", 0x0' -f $main)
        Assert-InitCondition ($text -match '(?i)Downloading file|Flash contents already match') 'MCU write was not confirmed; no replay'
        $s.McuConfirmed=$true
        # Retire old journal generation before committing the new first page.
        $order=@(@(4096,'settings.bin',4096),@(0,'settings.bin',0),@(8192,'gui-assets.bin',0),@(12288,'gui-assets.bin',4096),@(16384,'gui-assets.bin',8192))
        foreach($entry in $order){$null=Invoke-InitExternal 2 $entry[0] 4096 ([NrfInitBytes]::Slice($Materials.Data[$entry[1]],$entry[2],4096));$s.ExternalConfirmed++}
        $null=Invoke-InitExternal 3 0xa000 0x76000
        $s.ProgrammingSucceeded=$true
        $readback=Read-InitTarget 'internal-readback' 0 196608
        Assert-InitCondition ([NrfInitBytes]::Equal($readback,$Materials.Data['internal.bin'])) 'Whole internal Flash readback differs; no reset'
        foreach($entry in $order) {
            $data=Invoke-InitExternal 1 $entry[0] 4096
            Assert-InitCondition ([NrfInitBytes]::Equal($data,[NrfInitBytes]::Slice($Materials.Data[$entry[1]],$entry[2],4096))) 'Independent external readback differs; no reset'
        }
        $heads=Invoke-InitExternal 4 0xa000 0x76000
        Assert-InitCondition ([NrfInitBytes]::Equal($heads,(New-Object byte[] 472))) 'Independent image-head readback differs'
        $s.ReadbackMatched=$true
        Clear-InitVolatile $Materials.Data['internal.bin']
        $s.ResetRequested=$true
        $null=Invoke-InitNative 'verified-reset' @('Reset','Go')
        $result=[ordered]@{status='verified-initialization-write';mode=$Mode;generationSucceeded=$true;programmingSucceeded=$true;readbackMatched=$true;ordinaryUserData='reset defaults; old image heads invalidated, pixel payload not securely erased';identity='Manufactured FICR/SoftDevice address untouched; no host MAC/UID/QR generated';factory='FICR/UICR and external0x5000..0x9FFF skipped by every write command';pro='not-applicable: independent Free-only target';optionalOldReads=@($s.Optional);appRuntimeTested=$false;automaticWriteRetries=0;bleOperations=0;pythonRequired=$false}
        Save-InitJson (Join-Path $root 'result.json') $result
        return $result
    } catch {
        Save-InitJson (Join-Path $root 'failure.json') @{error=$_.Exception.Message;generationSucceeded=$true;automaticRetry=$false;automaticReset=$false;resetRequested=$s.ResetRequested;optionalOldReads=@($s.Optional);mcuWriteAttempted=$s.McuAttempted;mcuWriteConfirmed=$s.McuConfirmed;externalWritesConfirmed=$s.ExternalConfirmed;programmingSucceeded=$s.ProgrammingSucceeded;readbackMatched=$s.ReadbackMatched}
        throw
    }
}
