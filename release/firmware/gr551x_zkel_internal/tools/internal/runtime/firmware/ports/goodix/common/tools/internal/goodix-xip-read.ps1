# GR5331's existing diagnostic XIP range, not new silicon/capacity qualification.
# One Commander session per independent pass; never restart/replay a chunk.
. (Join-Path $PSScriptRoot 'goodix-native-output.ps1')
function Test-InkTagGoodixXipReadOutput {
    param([string]$Output, [int]$NativeExit, [string]$ExpectedCpu)
    # ExitOnError=1 prints this informational banner even on a successful read.
    # Exempt only that complete line; never suppress an actual error or suffix.
    $checkedOutput=Get-InkTagGoodixCheckedNativeOutput -Output $Output
    return ($NativeExit -eq 0 -and
        $checkedOutput -notmatch '(?i)could not read|cannot connect|could not connect|\berror\b|\bfailed\b|unknown command|Connect fallback:.*Reset' -and
        $Output -match ('(?im)E000ED00\s*=\s*'+[regex]::Escape($ExpectedCpu)+'\b'))
}

function Invoke-InkTagGoodixXipRead {
    param([string]$RepoRoot, [string]$EvidenceDir, [string]$JLinkExe,
          [string]$Snr, [int]$FrequencyHz=500000, [string]$IdentityFile)
    $identity=Get-Content -LiteralPath $IdentityFile -Raw|ConvertFrom-Json
    if($identity.chipConfirmed -or $identity.chip -ne 'UNIDENTIFIED-MCU' -or
       $identity.requestedChip -ne 'gr5331' -or $identity.cpuid -notmatch '^0x41[0-9A-Fa-f]{2}C24[0-9A-Fa-f]$' -or
       $identity.flashBytes -ne 524288 -or $identity.baseAddress -ne 0x00200000 -or
       $Snr -notmatch '^\d+$' -or $identity.probeSerial -ne $Snr) {
        throw 'Diagnostic XIP read requires the captured Cortex-M4, GR5331 diagnostic range and exact probe serial; no read'
    }
    . (Join-Path $RepoRoot 'tools/jlink-tools.ps1')
    . (Join-Path $PSScriptRoot 'goodix-backup-archive.ps1')
    $tool=Resolve-InkTagJLinkTool -ToolName 'JLink.exe' -ExplicitPath $JLinkExe -RepoRoot $RepoRoot
    Assert-InkTagJLink788j -JLinkExe $tool
    Assert-InkTagGoodixArchiveDirectory -Path $EvidenceDir
    if(Test-Path -LiteralPath $EvidenceDir){throw 'Read evidence collision; no replay'}
    $chunks=Join-Path $EvidenceDir 'chunks'
    New-Item -ItemType Directory -Path $chunks -ErrorAction Stop|Out-Null
    $capacity=524288; $chunkBytes=16384; $count=32
    $speed=[Math]::Max(1,[int][Math]::Ceiling($FrequencyHz/1000.0))
    # Set commands return the previous value, not a success/failure code.
    $commands=@('si SWD',"speed $speed",'SWDSelect',
        'connect','halt','exec SetRestartOnClose = 0','mem32 0xE000ED00 1','exec SetAllowFlashCache = 0')
    $paths=@()
    for($i=0;$i -lt $count;$i++) {
        $path=Join-Path $chunks ('chunk-{0:D3}.bin' -f $i)
        $paths+=@($path)
        $commands+=('savebin "{0}" 0x{1:X8} 0x{2:X}' -f $path,(0x00200000+$i*$chunkBytes),$chunkBytes)
    }
    $commands+='qc' # CPU stays halted; the owning workflow performs one explicit cleanup.
    $script=Join-Path $EvidenceDir 'read.jlink'
    $commands|Set-Content -LiteralPath $script -Encoding ASCII
    $arguments=@('-SelectEmuBySN',$Snr,'-NoGui','1','-Device','Cortex-M4','-If','SWD',
        '-Speed',"$speed",'-AutoConnect','1','-ExitOnError','1','-CommanderScript',('"'+$script+'"'))
    $stdout=Join-Path $EvidenceDir 'stdout.log'; $stderr=Join-Path $EvidenceDir 'stderr.log'
    $process=$null; $lastBytes=-1; $nativeExit=$null
    Write-Host 'GR_READ_STAGE: diagnostic XIP pass; one connection, 32 fresh chunks; no automatic read retry.'
    try {
        $process=Start-Process -FilePath $tool -WindowStyle Hidden -PassThru -ArgumentList $arguments `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $nativeHandle=$process.Handle
        do {
            # Only complete files emitted by SaveBin count as finished chunks.
            # The reader never fabricates a percentage from elapsed time.
            [long]$done=0
            foreach($path in $paths) {
                if((Test-Path -LiteralPath $path -PathType Leaf) -and
                   (Get-Item -LiteralPath $path).Length -eq $chunkBytes){$done+=$chunkBytes}
            }
            if($done -ne $lastBytes) {
                Write-Host ('GR_READ_PROGRESS: {0}/{1}' -f $done,$capacity)
                $lastBytes=$done
            }
            if(!$process.HasExited){Start-Sleep -Milliseconds 100;$process.Refresh()}
        } while(!$process.HasExited)
        $process.WaitForExit()
        $nativeExit=$process.ExitCode
    } finally {
        if($null -ne $process) {
            if(!$process.HasExited){$process.Kill();$process.WaitForExit()}
            $process.Dispose()
        }
    }
    $output=(Get-Content -LiteralPath $stdout -Raw)+(Get-Content -LiteralPath $stderr -Raw)
    Write-Host $output
    $expectedCpu=$identity.cpuid.Substring(2)
    if(!(Test-InkTagGoodixXipReadOutput -Output $output -NativeExit $nativeExit -ExpectedCpu $expectedCpu)) {
        throw "Diagnostic XIP read failed (exit $nativeExit); retain partial chunks; no replay"
    }
    foreach($path in $paths) {
        if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -ne $chunkBytes) {
            throw 'Incomplete diagnostic XIP chunk; retain files; no replay'
        }
    }
    $dump=Join-Path $EvidenceDir 'gr5331-xip-flash.bin'
    $stream=[IO.File]::Open($dump,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
    try {
        foreach($path in $paths){$bytes=[IO.File]::ReadAllBytes($path);$stream.Write($bytes,0,$bytes.Length)}
    } finally {$stream.Dispose()}
    if((Get-Item -LiteralPath $dump).Length -ne $capacity){throw 'Diagnostic XIP image length mismatch; no replay'}
    if($lastBytes -ne $capacity){Write-Host "GR_READ_PROGRESS: $capacity/$capacity"}
    Write-Host 'Diagnostic XIP pass completed; exact MCU/physical Flash and original-source qualification remain separate.'
}
