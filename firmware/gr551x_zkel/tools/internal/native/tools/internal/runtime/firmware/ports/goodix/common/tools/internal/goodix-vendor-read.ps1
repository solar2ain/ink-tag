# GR551x physical Flash reader. The generic Cortex-M4 XIP window can return
# repeated ROM/RAM words while Flash is parked: never use it for stock backup.
# This helper invokes only vendor enumeration/readFlash and a separate one-shot
# ordinary reset to leave the read helper. No erase/program/status/OTP commands.
. (Join-Path $PSScriptRoot 'goodix-native-output.ps1')
function Get-InkTagGoodixVendorIndex {
    param([string]$Console, [string]$Serial, [string]$EvidenceDir)
    Push-Location -LiteralPath (Split-Path $Console -Parent)
    try {$listing=@(& $Console device 1 2>&1);$enumerationExit=$LASTEXITCODE}
    finally {Pop-Location}
    $text=($listing|ForEach-Object{$_.ToString()}) -join "`n"
    $text|Set-Content -LiteralPath (Join-Path $EvidenceDir 'enumeration.log') -Encoding UTF8
    $rows=@([regex]::Matches($text,'(?m)^\s*(\d+)\s+.+?\s+(\d{6,})\s*$')|Where-Object{$_.Groups[2].Value -eq $Serial})
    if($enumerationExit -ne 0 -or $rows.Count -ne 1){throw 'Vendor tool did not uniquely find the captured J-Link serial; no replay'}
    return $rows[0].Groups[1].Value
}
function Send-InkTagGoodixVendorProgress {
    param([string]$Path, $State, [string]$DeviceIndex)
    if(!(Test-Path -LiteralPath $Path)){return}
    $stream=$null;$reader=$null
    try {
        $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
        [void]$stream.Seek($State.Offset,[IO.SeekOrigin]::Begin)
        $reader=[IO.StreamReader]::new($stream)
        $text=$State.Carry+$reader.ReadToEnd();$State.Offset=$stream.Position
    } finally {if($null -ne $reader){$reader.Dispose()}elseif($null -ne $stream){$stream.Dispose()}}
    $lines=$text -split "`n";$State.Carry=$lines[-1]
    for($i=0;$i -lt $lines.Count-1;$i++) {
        $line=$lines[$i].Trim()
        # The vendor can spend minutes retrying an attach while stderr is
        # already reporting failure. Do not hide it behind a read-wait label
        # or replay that read. The owning finally retains evidence and handles
        # this process only; the workflow owns the one cleanup attempt.
        if($line -match '(?i)^connect:\s*failed to connect with jlink\s*$|^Failed to connect JLink\s+\d+\s*$') {
            Write-Host ('ERROR: Vendor reader connection failed: '+$line)
            throw 'Vendor reader could not attach after detection; no completed read; evidence retained; no read replay'
        }
        if(!$line.StartsWith('{')){continue}
        try {$event=$line|ConvertFrom-Json -ErrorAction Stop}catch{continue}
        if([string]$event.Device -ne $DeviceIndex -or $event.Status -ne 'Reading' -or
           $event.Progress -isnot [ValueType]){continue}
        $value=[double]$event.Progress
        if($value -ge 0 -and $value -le 100 -and $value -gt $State.Last) {
            Write-Host ('GR_READ_VENDOR_PROGRESS: {0}' -f $value.ToString([Globalization.CultureInfo]::InvariantCulture))
            $State.Last=$value
        }
    }
}

function Invoke-InkTagGoodixVendorRead {
    param([string]$RepoRoot, [ValidateSet('gr5513','gr5515')][string]$Chip,
          [string]$Snr, [string]$EvidenceDir, [string]$IdentityFile='', [switch]$Cleanup)
    if($Snr -notmatch '^\d+$') { throw 'An exact detected J-Link serial is required for vendor Flash reads' }
    $EvidenceDir=[IO.Path]::GetFullPath($EvidenceDir)
    . (Join-Path $PSScriptRoot 'goodix-backup-archive.ps1')
    $primary=Get-InkTagGoodixBackupPrimaryRoot -RepoRoot $RepoRoot
    $console=Join-Path $primary '.tools/gprogrammer/_._/GR5xxx_console.exe'
    if(!(Test-Path -LiteralPath $console -PathType Leaf)) { throw "Maintained vendor reader is missing: $console; no Flash read started" }
    Assert-InkTagGoodixArchiveDirectory -Path $EvidenceDir
    if(Test-Path -LiteralPath $EvidenceDir) { throw 'Vendor read evidence collision; no replay' }
    New-Item -ItemType Directory -Path $EvidenceDir -ErrorAction Stop|Out-Null
    $consoleRoot=Split-Path $console -Parent
    if($Cleanup) {
        $marker=Get-Content -LiteralPath $IdentityFile -Raw|ConvertFrom-Json
        if($marker.vendorReadStarted -ne $true -or $marker.chip -ne $Chip -or $marker.probeSerial -ne $Snr) {
            throw 'Vendor read was not started on this exact target; refusing cleanup reset'
        }
        # The installed console uses named options. Positional "reset 0 SNR"
        # is not its reset API. Use the same chip-aware backend as readFlash,
        # not a fresh generic Cortex-M4 connection to a possibly sleeping App.
        $vendorIndex=Get-InkTagGoodixVendorIndex -Console $console -Serial $Snr -EvidenceDir $EvidenceDir
        $arguments=@('reset','--chip',$Chip.ToUpperInvariant(),'--jlink',$vendorIndex)
    } else {
        $identity=Get-Content -LiteralPath $IdentityFile -Raw|ConvertFrom-Json
        $expected=if($Chip -eq 'gr5513'){@(5,7)}else{@(4)}
        $capacity=if($Chip -eq 'gr5513'){524288}else{1048576}
        if(!$identity.chipConfirmed -or $identity.chip -ne $Chip -or
           $identity.packageId -notin $expected -or $identity.flashBytes -ne $capacity -or
           $identity.baseAddress -ne 0x01000000 -or $identity.probeSerial -ne $Snr) {
            throw 'Actual chip/package/capacity/probe identity mismatch; no Flash read'
        }
        $vendorIndex=Get-InkTagGoodixVendorIndex -Console $console -Serial $Snr -EvidenceDir $EvidenceDir
        $dump=Join-Path $EvidenceDir ($Chip+'-xip-flash.bin')
        $arguments=@('readFlash','--chip',$Chip.ToUpperInvariant(),'--flashSize',"$($capacity/1024)",
            '--address','0x01000000','--size',"$capacity",'--save',('"'+$dump+'"'),'--jlink',$vendorIndex)
        Write-Host "Physical Flash read: $Chip, $capacity bytes; vendor-managed SWD speed. No automatic read retry."
        $progressState=[pscustomobject]@{Offset=0L;Carry='';Last=-1.0}
        $errorState=[pscustomobject]@{Offset=0L;Carry='';Last=-1.0}
    }
    $arguments|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $EvidenceDir 'arguments.json') -Encoding UTF8
    $stdout=Join-Path $EvidenceDir 'stdout.log'; $stderr=Join-Path $EvidenceDir 'stderr.log'
    $process=$null
    try {
        if(!$Cleanup) {
            @{vendorReadStarted=$true;chip=$Chip;probeSerial=$Snr}|
                ConvertTo-Json|Set-Content -LiteralPath (Join-Path $EvidenceDir 'vendor-read-started.json') -Encoding UTF8
        }
        $process=Start-Process -FilePath $console -WorkingDirectory $consoleRoot -WindowStyle Hidden -PassThru `
            -ArgumentList $arguments -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $nativeHandle=$process.Handle
        if(!$Cleanup) {
            do {
                Send-InkTagGoodixVendorProgress -Path $stdout -State $progressState -DeviceIndex $vendorIndex
                Send-InkTagGoodixVendorProgress -Path $stderr -State $errorState -DeviceIndex $vendorIndex
                if(!$process.HasExited){Start-Sleep -Milliseconds 100;$process.Refresh()}
            } while(!$process.HasExited)
        }
        $process.WaitForExit()
        if(!$Cleanup){
            Send-InkTagGoodixVendorProgress -Path $stdout -State $progressState -DeviceIndex $vendorIndex
            Send-InkTagGoodixVendorProgress -Path $stderr -State $errorState -DeviceIndex $vendorIndex
        }
        $nativeExit=$process.ExitCode
    } finally {
        if($null -ne $process) {
            if(!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
            $process.Dispose()
        }
    }
    $output=(Get-Content -LiteralPath $stdout -Raw)+(Get-Content -LiteralPath $stderr -Raw)
    Write-Host $output
    $checkedOutput=Get-InkTagGoodixCheckedNativeOutput -Output $output
    if($nativeExit -ne 0 -or $checkedOutput -match '(?i)\berror\b|\bfailed\b|cannot connect|could not connect|could not read|unknown command') { throw "Vendor read/cleanup failed (exit $nativeExit); raw evidence retained; no replay" }
    if(!$Cleanup -and (!(Test-Path -LiteralPath $dump -PathType Leaf) -or (Get-Item -LiteralPath $dump).Length -ne $capacity)) {
        throw 'Vendor read returned an incomplete file; preserve evidence; no read replay'
    }
}
