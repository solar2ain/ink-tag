[CmdletBinding()]
param(
    [Parameter(HelpMessage='Required complete512-KiB Free bin from generate-bin.ps1. Written unchanged at address0; no ZIP, Config or companion JSON required.')][Alias('Firmware','File')][string]$Bin = '',
    [Parameter(HelpMessage='Required exact CH340 serial port, e.g. COM13.')][string]$Port = '',
    [Parameter(HelpMessage='Opt in to complete post-write readback on the same halted connection. Default writes only.')][switch]$Verify,
    [Parameter(HelpMessage='Confirm replacing lower512-KiB identity/calibration, Pro, credentials and data with this bin without prompting.')][switch]$Yes,
    [Parameter(HelpMessage='UART/SWS baud; 0 uses the selected board registered write baud. Explicit overrides still require a representable hardware divider.')][ValidateSet(0,460800,576000,921600)][int]$Baud = 0,
    [Parameter(HelpMessage='Repair mode: capture at921600, use maintained exact-UID/JEDEC checks to prepare a halted24MHz/460800 programming link. Reads no old Flash array; firmware still starts at its normal clock.')][switch]$PrepareClock24,
    [Parameter(HelpMessage='Optional supported Flash JEDEC override when the actual part differs from the bin board registration. Actual part/status are still checked.')][ValidatePattern('^$|^(EB6013|EB6014|856013)$')][string]$FlashJedec = '',
    [Parameter(HelpMessage='Initial SWS detection window, in milliseconds.')][ValidateRange(1000,120000)][int]$ActivationMs = 30000,
    [Parameter(HelpMessage='RTS-to-RESET pulse, in milliseconds; requires the physical reset wire.')][ValidateRange(50,2000)][int]$ResetPulseMs = 500,
    [Parameter(HelpMessage='Retry initial contact only; never replay writing.')][switch]$RetryDetect,
    [Parameter(HelpMessage='Maximum initial-contact retry window, in seconds.')][ValidateRange(1,3600)][int]$DetectTimeoutSeconds = 120,
    [Parameter(HelpMessage='Optional path to the maintained UART/SWS backend.')][string]$ToolPath = '',
    [Parameter(HelpMessage='Python executable with tools dependencies installed.')][string]$Python = 'python',
    [Parameter(HelpMessage='Optional new evidence directory. Existing directories are refused.')][string]$OutputDirectory = '',
    [Parameter(HelpMessage='Inspect the bin offline; no hardware access or output files.')][switch]$DryRun,
    [Parameter(HelpMessage='Show options without hardware access.')][Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments=$true)][object[]]$RemainingArguments
)
. (Join-Path $PSScriptRoot 'internal/terminal-log.ps1')
$terminalColour=Get-InkTagTerminalColour
try {
    Set-InkTagTerminalEnglish
    $ErrorActionPreference='Stop'
    . (Join-Path $PSScriptRoot 'internal/tlsr8359-uart-sws-common.ps1')
    $repoRoot=Get-Tlsr8359RepoRoot
    . (Join-Path $repoRoot 'tools/powershell-cli.ps1')
    $guard=Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help -RemainingArguments $RemainingArguments `
        -Summary 'TLSR CH340 direct bin write: no old Flash/UID capture, no preservation or repackaging; optional -Verify; upper Flash untouched'
    if ($null -ne $guard) { exit $guard }
    if (!$Bin) { throw '-Bin <generated.bin> is required; no device accessed.' }
    if (!$DryRun -and (!$Port -or $Port -eq 'auto')) { throw 'An exact -Port is required; no automatic device selection.' }
    $pythonExe=Resolve-TlsrPython -RequestedPython $Python
    $reviewJson=& $pythonExe (Join-Path $repoRoot 'tools/hardware/tlsr_bin_flash.py') --bin $Bin
    if ($LASTEXITCODE -ne 0) { throw 'Bin validation failed; no device accessed.' }
    $review=($reviewJson -join "`n") | ConvertFrom-Json
    if (!$Baud) { $Baud=[int]$review.flash_contract.uartSws.writeBaud }
    if ($PrepareClock24 -and $Baud -ne 460800) { throw '-PrepareClock24 requires -Baud 460800; no device accessed.' }
    $review | Add-Member -MemberType NoteProperty -Name selected_uart_baud -Value $Baud
    if ($DryRun) { $review | ConvertTo-Json -Depth 8; exit 0 }
    if (!$FlashJedec) { $FlashJedec=[string]$review.flash_contract.flashJedecId }
    if (!$FlashJedec) { throw 'No registered Flash model; supply the actual supported -FlashJedec. No device accessed.' }
    $source=(Resolve-Path -LiteralPath $Bin).Path
    Write-InkTagTerminalStage 'DIRECT BIN WRITE'
    $macHint=if ($review.automatic_mac) { 'automatic at first boot' } else { $review.mac }
    Write-InkTagTerminalMessage ('PCB: {0}; bin MAC: {1}; bytes: {2}; SHA256: {3}' -f $review.board_marking,$macHint,$review.image_bytes,$review.image_sha256)
    Write-InkTagTerminalMessage ('UART/SWS baud: {0}; automatic clock/divider validation remains mandatory.' -f $Baud)
    Write-InkTagTerminalMessage 'Writes this bin unchanged to0x00000..0x7FFFF. Replaces original identity/calibration, Pro, passwords, settings, fonts and lower images. No UID/Factory/Settings capture or retention. Upper Flash and separate NFC EEPROM are untouched.' -Level WARNING
    if ($Verify) { Write-InkTagTerminalMessage 'Write once, then compare all512KiB before final reset.' }
    else { Write-InkTagTerminalMessage 'Write only; no readback. Result will be WRITTEN-NOT-VERIFIED.' -Level WARNING }
    if (!$Yes) {
        $answer=Read-Host -Prompt 'Continue? [y/N]'
        if (!$answer -or $answer.Trim().ToLowerInvariant() -ne 'y') { throw 'Cancelled; no device accessed.' }
    }
    $resolvedPort=Resolve-TlsrSerialPort -RequestedPort $Port -RequireQualifiedCh340
    $backend=Resolve-Tlsr825xComFlasher -RequestedPath $ToolPath
    if (!$OutputDirectory) {
        $tag=[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
        $OutputDirectory=Join-Path $repoRoot ('.local/bin-flash/'+$tag)
    }
    if (Test-Path -LiteralPath $OutputDirectory) { throw 'Evidence directory exists; no write.' }
    $null=New-Item -ItemType Directory -Path $OutputDirectory
    $directory=(Resolve-Path -LiteralPath $OutputDirectory).Path
    $receipt=Join-Path $directory 'result.json'
    $writeActivationMs=$ActivationMs
    if ($PrepareClock24) {
        $uidFile=Join-Path $directory 'programming-link.json'
        Write-InkTagTerminalMessage 'Repair link: read-only UID/JEDEC checks, then qualified stopped-clock preparation. No Factory/Settings/App pre-read or Flash erase/program.'
        $identifyOptions=@($backend,'--port',$resolvedPort,'--baud','921600','--activation-ms',"$ActivationMs",'--reset-pulse-ms',"$ResetPulseMs",'--clock-mhz','0','--samples','3','--uid-output',$uidFile,'--expected-jedec',$FlashJedec.ToUpperInvariant(),'--halted-program-clock24','--hold-cpu')
        if ($RetryDetect) { $identifyOptions+=@('--retry-detect','--detect-timeout',"$DetectTimeoutSeconds") }
        $identifyPreference=$ErrorActionPreference
        try {
            $ErrorActionPreference='Continue'
            & $pythonExe -u (Join-Path $PSScriptRoot 'internal/tlsr825x-flash-identify.py') @identifyOptions 2>&1 | Tee-Object -FilePath (Join-Path $directory 'programming-link.log') | Show-InkTagTerminalLog -CompletionMessage 'Programming-link preparation finished; no Flash erased/programmed.'
            $identifyExit=$LASTEXITCODE
        } finally { $ErrorActionPreference=$identifyPreference }
        if ($identifyExit -ne 0) { throw 'Programming link not confirmed; no Flash write or reset.' }
        $link=Get-Content -LiteralPath $uidFile -Raw | ConvertFrom-Json
        if ($link.schema -ne 'inktag-tlsr-live-flash-uid-v1' -or !$link.cpuHeld -or
            $link.jedec -ne $FlashJedec.ToUpperInvariant() -or $link.flashWrites -ne 0 -or
            !$link.clockPreparation.cpuHalted -or $link.clockPreparation.uartBaud -ne 460800 -or
            @($link.samples).Count -ne 3 -or @($link.samples | Where-Object { $_ -ne $link.uidHex }).Count -ne 0) {
            throw 'Programming-link receipt incomplete; no Flash write.'
        }
        $writeActivationMs=0 # Preserve held CPU/link; do not reset between preparation and write.
    }
    $options=@($backend,'-p',$resolvedPort,'-b',"$Baud",'-t',"$writeActivationMs",'--inktag-reset-pulse-ms',"$ResetPulseMs",'--inktag-flash-jedec',$FlashJedec.ToUpperInvariant(),'--inktag-bin-receipt',$receipt,'--inktag-bin-sha256',$review.image_sha256)
    if ($Verify) { $options+=@('--inktag-post-write-verify-file',(Join-Path $directory 'readback.bin')) }
    if ($RetryDetect -and !$PrepareClock24) { $options+=@('--inktag-retry-detect','--inktag-detect-timeout',"$DetectTimeoutSeconds") }
    $options+=@('-r','wf','0',$source)
    $nativePreference=$ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        & $pythonExe -u (Join-Path $PSScriptRoot 'internal/tlsr825x-early-activate.py') @options 2>&1 | Tee-Object -FilePath (Join-Path $directory 'write.log') | Show-InkTagTerminalLog -CompletionMessage 'Bin transport finished; checking its receipt.'
        $transportExit=$LASTEXITCODE
    } finally { $ErrorActionPreference=$nativePreference }
    if ($transportExit -ne 0) { throw 'Bin write failed/unknown; no automatic write retry. Retain this evidence directory.' }
    $result=Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
    $expected=if ($Verify) { 'BIN-READBACK-MATCHED' } else { 'BIN-WRITTEN-NOT-VERIFIED' }
    if ($result.status -ne $expected -or $result.imageSha256 -ne $review.image_sha256 -or $result.writtenBytes -ne 524288 -or !$result.wholeChipResetRequested) { throw 'Incomplete/mismatched bin receipt; installation unconfirmed.' }
    if ($Verify -and $result.readbackSha256 -ne $review.image_sha256) { throw 'Readback mismatch; installation unconfirmed.' }
    Write-InkTagTerminalMessage ($result.status+'; evidence: '+$receipt)
} catch {
    Write-InkTagTerminalMessage $_.Exception.Message -Level ERROR
    exit 1
} finally { Restore-InkTagTerminalColour $terminalColour }
