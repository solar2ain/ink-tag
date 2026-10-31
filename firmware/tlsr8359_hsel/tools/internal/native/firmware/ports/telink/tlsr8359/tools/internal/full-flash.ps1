[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Config,
    [Parameter(Mandatory=$true)][string]$BaseFirmware,
    [Parameter(Mandatory=$true)][string]$Firmware,
    [Parameter(Mandatory=$true)][string]$Board,
    [Parameter(Mandatory=$true)][string]$Port,
    [string]$ExpectedMac='', [string]$StockBackup='',
    [switch]$SkipReadbackVerify, [switch]$Yes,
    [int]$Baud=0, [int]$ReadBaud=0, [int]$ActivationMs=0,
    [ValidateRange(50,2000)][int]$ResetPulseMs=500,
    [int]$ClockMHz=-1, [string]$ToolPath='', [string]$Python='python',
    [switch]$RetryDetect, [ValidateRange(1,3600)][int]$DetectTimeoutSeconds=120,
    [switch]$ShowCommand, [Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments=$true)][object[]]$RemainingArguments
)
. (Join-Path $PSScriptRoot 'terminal-log.ps1')
$fullTerminalColour=Get-InkTagTerminalColour
$receiptPath=''; $phase='admission'
try {
    Set-InkTagTerminalEnglish
    $ErrorActionPreference='Stop'; Set-StrictMode -Version Latest
    . (Join-Path $PSScriptRoot 'tlsr8359-uart-sws-common.ps1')
    . (Join-Path $PSScriptRoot '../../../../../../tools/powershell-cli.ps1')
    $guard=Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help -RemainingArguments $RemainingArguments -Summary 'Internal v1 complete flashing preserves valid same-device Pro, clears other mutable user/diagnostic state and initializes Recovery state from selected package'
    if ($null -ne $guard) { exit $guard }
    if ($Port -eq 'auto') { throw 'Full requires one exact Port; no automatic selection.' }
    if ($ExpectedMac -and $ExpectedMac -notmatch '^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$') { throw 'Invalid ExpectedMac; no device accessed.' }
    $repoRoot=Get-Tlsr8359RepoRoot; $pythonExe=Resolve-TlsrPython -RequestedPython $Python
    $planner=Join-Path $repoRoot 'tools/hardware/tlsr_full_flash_plan.py'
    $reviewOptions=@('--base',$BaseFirmware,'--config',$Config,'--require-write','--check-template',$Firmware)
    if ($SkipReadbackVerify) { $reviewOptions+='--skip-readback-verify' }
    $reviewJson=& $pythonExe $planner @reviewOptions
    if ($LASTEXITCODE -ne 0) { throw 'Package integrity or Config validation failed; no serial access.' }
    $review=($reviewJson -join "`n") | ConvertFrom-Json
    if ($review.board -ne $Board -or $review.format_version -ne 1) { throw 'Config/backend mismatch.' }
    $target=Get-Tlsr8359FullTarget -ConfigReview $review
    $registeredJedec=[string]$target.Contract.flashJedecId
    foreach ($warning in $review.qualification_warnings) { Write-InkTagTerminalMessage $warning -Level WARNING }
    if (!$Baud) { $Baud=$target.WriteBaud }; if (!$ReadBaud) { $ReadBaud=$target.ReadBaud }
    if (!$ActivationMs) { $ActivationMs=$target.ActivationMs }; if ($ClockMHz -lt 0) { $ClockMHz=$target.ClockMHz }
    if ($Baud -ne $target.WriteBaud -or $ReadBaud -ne $target.ReadBaud -or $ClockMHz -ne $target.ClockMHz) { throw 'Full requires the resolved UART/SWS parameters; backup overrides are not write qualification.' }
    # Explicit original rescue references are validated, never used as current
    # Factory/user-state evidence and never silently searched or substituted.
    if ($StockBackup) {
        $archiveOptions=@('--board',$Board,'--archive',$StockBackup)
        if ($ExpectedMac) { $archiveOptions+=@('--expected-mac',$ExpectedMac) }
        $archiveJson=& $pythonExe (Join-Path $repoRoot 'tools/hardware/tlsr_full_flash_archive.py') @archiveOptions
        if ($LASTEXITCODE -ne 0) { throw 'Explicit rescue archive is invalid; no device accessed.' }
    }
    Write-InkTagTerminalMessage ('Complete flashing clears user settings/password/Find, fonts, main-Flash images and diagnostic history on {0} (PCB: {1}); preserves only verified same-device Pro, original identity/address type and calibration. Unrecognized, damaged, incompatible or foreign authorization will be cleared and the device initializes Free; Pro then requires normal activation. Separate NFC EEPROM is not erased. Recovery state is rebuilt.' -f $Port,$review.board_marking) -Level WARNING
    if ($Yes) {
        Write-InkTagTerminalMessage 'Explicit -Yes confirms the displayed Full user-data wipe. No identity/signature/capacity checks or requested readback policy are bypassed.' -Level WARNING
    } else {
        $answer=Read-Host -Prompt 'Continue? [y/N]'
        if ([string]::IsNullOrWhiteSpace($answer) -or $answer.Trim().ToLowerInvariant() -ne 'y') { throw 'Full cancelled; no device accessed or Flash erased.' }
    }
    $fullWipeConfirmed=$true
    & $pythonExe (Join-Path $repoRoot 'tools/hardware/tlsr_full_flash_entitlement.py') --check-dependencies
    if ($LASTEXITCODE -ne 0) { throw 'Shared authorization verifier is missing/incomplete; no device accessed or erase started.' }
    & $pythonExe (Join-Path $repoRoot 'tools/hardware/tlsr_full_flash_state.py') --check-dependencies
    if ($LASTEXITCODE -ne 0) { throw 'Programmer default-data dependencies are incomplete. Run setup.ps1; no CLI installation is required. No device accessed or erase started.' }
    $tag=[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
    $directory=Join-Path $repoRoot ('.local/full-config/hardware/{0}/{1}' -f $review.board_marking,$tag)
    New-Item -ItemType Directory -Path $directory | Out-Null
    $receiptPath=Join-Path $directory 'result.json'
    $factory=Join-Path $directory 'current-factory.bin'; $uidFile=Join-Path $directory 'flash-uid.json'
    $settings=Join-Path $directory 'private-current-settings.bin'
    $prepared=Join-Path $directory 'prepared-full.bin'; $readback=Join-Path $directory 'readback-spans.bin'
    $Port=Resolve-TlsrSerialPort -RequestedPort $Port -RequireQualifiedCh340
    $resolvedTool=Resolve-Tlsr825xComFlasher -RequestedPath $ToolPath
    $invoke=Join-Path $PSScriptRoot 'invoke-tlsr8359-uart-sws.ps1'
    Write-InkTagTerminalStage 'LAYOUT-V1 CONFIGURED FULL'
    Write-InkTagTerminalMessage 'No whole-Flash pre-read. Bounded Factory and native Settings reads preserve valid same-device Pro and identity/calibration; other mutable state is cleared after y/N.'
    Write-InkTagTerminalMessage 'Original firmware cannot be restored from bounded data reads. Use backup separately before the first Full if original-firmware rescue is wanted.' -Level WARNING
    Write-InkTagTerminalMessage 'Watch USB-UART traffic, not target-board LED progress (blue may share SWS). Keep power and wiring stable after activation.'
    $phase='uid'
    Write-InkTagTerminalMessage 'Please wait: three read-only Flash UID/JEDEC samples; no percentage yet, no Flash erase/program.'
    $uidOptions=@($resolvedTool,'--port',$Port,'--baud',"$ReadBaud",'--activation-ms',"$ActivationMs",'--clock-mhz',"$ClockMHz",'--reset-pulse-ms',"$ResetPulseMs",'--samples','3','--uid-output',$uidFile)
    if ($registeredJedec) { $uidOptions+=@('--expected-jedec',$registeredJedec) }
    if ($RetryDetect) { $uidOptions+=@('--retry-detect','--detect-timeout',"$DetectTimeoutSeconds") }
    # This separate native entry does not inherit the read/write helper's
    # PYTHONUNBUFFERED guard. Keep stdout live through both PowerShell pipes.
    $uidErrorActionPreference=$ErrorActionPreference
    try {
        # Windows PowerShell wraps native stderr in ErrorRecord. Retain/drain
        # the entire child output and then inspect its actual exit status.
        $ErrorActionPreference='Continue'
        & $pythonExe -u (Join-Path $PSScriptRoot 'tlsr825x-flash-identify.py') @uidOptions 2>&1 | Tee-Object -FilePath (Join-Path $directory 'uid.log') | Show-InkTagTerminalLog -CompletionMessage 'Flash identification step finished; no Flash erased or programmed.'
        $uidExitCode=$LASTEXITCODE
    } finally { $ErrorActionPreference=$uidErrorActionPreference }
    if ($uidExitCode -ne 0) { throw 'Fresh Flash identification failed; no erase.' }
    $uid=Get-Content -LiteralPath $uidFile -Raw | ConvertFrom-Json
    $observedJedec=[string]$uid.jedec
    $readOptions=@{Port=$Port;Baud=$ReadBaud;ActivationMs=$ActivationMs;ResetPulseMs=$ResetPulseMs;ClockMHz=$ClockMHz;ToolPath=$resolvedTool;Python=$pythonExe;ExpectedFlashJedec=$observedJedec;ReadUnitUid=[string]$uid.uidHex;NoManualReset=$true}
    if ($ShowCommand) { $readOptions.ShowCommand=$true }
    $phase='bounded-evidence'
    Write-InkTagTerminalStage 'READ CURRENT 4-KiB FACTORY'
    Write-InkTagTerminalMessage 'Please wait: 0x7F000..0x7FFFF, 4 KiB, may take tens of seconds. Preserve this unit identity/calibration and unknown bytes; inspect source family. Not App code, not a backup; SkipReadbackVerify does not skip it.'
    & $invoke @readOptions -Operation read -Address 0x7F000 -SizeBytes 4096 -File $factory 2>&1 | Tee-Object -FilePath (Join-Path $directory 'factory.log') | Show-InkTagTerminalLog -CompletionMessage 'Factory read complete (4 KiB); no Flash erased or programmed.'
    if ($LASTEXITCODE -ne 0) { throw 'Factory read failed; no erase.' }
    $sourceJson=& $pythonExe (Join-Path $repoRoot 'tools/hardware/tlsr_full_flash_source.py') --board $Board --factory $factory
    if ($LASTEXITCODE -ne 0) { throw 'Factory capture could not be inspected; no erase.' }
    $source=($sourceJson -join "`n") | ConvertFrom-Json
    foreach ($warning in $source.warnings) { Write-InkTagTerminalMessage $warning -Level WARNING }
    Write-InkTagTerminalStage 'CAPTURE NATIVE AUTHORIZATION; BEFORE ERASE'
    Write-InkTagTerminalMessage 'Please wait: 0x59000..0x5AFFF, 8 KiB, both Settings pages. Preserve verified current same-device Pro where available; otherwise rebuild Free with an explicit impact report. Unknown metadata is not claimed empty. Private capture remains in this operation directory; no token is printed.'
    & $invoke @readOptions -Operation read -Address 0x59000 -SizeBytes 8192 -File $settings 2>&1 | Tee-Object -FilePath (Join-Path $directory 'settings-capture.log') | Show-InkTagTerminalLog -CompletionMessage 'Native Settings capture complete (8 KiB); no Flash erased or programmed.'
    if ($LASTEXITCODE -ne 0) { throw 'Native Settings capture failed; no erase.' }
    $prepareOptions=@('--base',$BaseFirmware,'--config',$Config,'--factory',$factory,'--settings',$settings,'--uid-receipt',$uidFile,'--output',$prepared)
    if ($ExpectedMac) { $prepareOptions+=@('--expected-mac',$ExpectedMac) }
    if (!$fullWipeConfirmed) { throw 'Full user-data wipe was not confirmed; no erase.' }
    $prepareOptions+='--destructive-confirmed'
    if ($source.source_layout -eq 'stock') {
        $calibration=Join-Path $directory 'current-stock-data.bin'
        Write-InkTagTerminalMessage 'Please wait: stock-only 8 KiB at 0x76000..0x77FFF for immutable identity/RF calibration. No firmware fingerprint or whole-Flash read.'
        & $invoke @readOptions -Operation read -Address 0x76000 -SizeBytes 8192 -File $calibration 2>&1 | Tee-Object -FilePath (Join-Path $directory 'stock-data.log') | Show-InkTagTerminalLog -CompletionMessage 'Stock identity/calibration read complete (8 KiB); no Flash erased or programmed.'
        if ($LASTEXITCODE -ne 0) { throw 'Stock calibration read failed; no erase.' }
        $prepareOptions+=@('--calibration',$calibration)
    }
    $phase='prepare'
    $preparedJson=& $pythonExe (Join-Path $repoRoot 'tools/hardware/tlsr_full_flash_v1.py') @prepareOptions
    if ($LASTEXITCODE -ne 0) { throw 'Package/config/identity/default-state preparation refused; no erase.' }
    $preparation=($preparedJson -join "`n") | ConvertFrom-Json
    foreach ($warning in $preparation.identity.warnings) { Write-InkTagTerminalMessage $warning -Level WARNING }
    $proLevel=if ($preparation.proPreservation.state -eq 'reinitialized-free') { 'WARNING' } else { 'INFO' }
    Write-InkTagTerminalMessage ('Authorization: {0}. {1}' -f $preparation.proPreservation.state,$preparation.proPreservation.userImpact) -Level $proLevel
    $identity=[string]$preparation.identity.address
    if ($StockBackup) {
        & $pythonExe (Join-Path $repoRoot 'tools/hardware/tlsr_full_flash_archive.py') --board $Board --archive $StockBackup --expected-mac $identity | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Explicit stock rescue is not this unit; no erase.' }
    }
    $plan=[IO.Path]::ChangeExtension($prepared,'.plan.json')
    $phase='write'
    Write-InkTagTerminalStage 'ONE COMPLETE WRITE; THEN ONE COMPLETE READBACK'
    Write-InkTagTerminalMessage ('Please wait: one {0}-KiB write, then one complete readback; capacity bound {1} KiB. Before unlock: recheck UID/JEDEC, Factory/native Settings captures and prepared inputs. Old OTA epochs are not read. No write retries.' -f ($preparation.written_bytes/1024),($preparation.capacity/1024))
    if ($SkipReadbackVerify) { Write-InkTagTerminalMessage 'Explicit SkipReadbackVerify: NOT VERIFIED. New Recovery/Settings verification still runs.' -Level WARNING }
    $writeOptions=$readOptions.Clone(); $writeOptions.Baud=$Baud; $writeOptions.ActivationBaud=$ReadBaud
    $writeOptions.Remove('ReadUnitUid')
    & $invoke @writeOptions -Operation write -Address 0 -File $prepared -FullWritePlan $plan -PostWriteReadbackFile $readback -SkipReadbackVerify:$SkipReadbackVerify -Run 2>&1 | Tee-Object -FilePath (Join-Path $directory 'write.log') | Show-InkTagTerminalLog -CompletionMessage 'Write transport finished; checking final Full receipt.'
    if ($LASTEXITCODE -ne 0) { throw 'Write/readback failed or unknown. No automatic retry, reset, rollback or second Full.' }
    $transport=Get-Content -LiteralPath ([IO.Path]::ChangeExtension($readback,'.receipt.json')) -Raw | ConvertFrom-Json
    $expectedStatus=if ($SkipReadbackVerify) {'FULL-WRITTEN-NOT-VERIFIED'} else {'FULL-READBACK-MATCHED'}
    if ($transport.status -ne $expectedStatus -or !$transport.recoveryStateVerified -or $transport.preparedSha256 -ne $preparation.prepared_sha256 -or $transport.planSha256 -ne $preparation.write_plan_sha256 -or $transport.writtenBytes -ne $preparation.written_bytes) { throw 'Transport receipt is incomplete/mismatched; installation not confirmed.' }
    if ($transport.readbackOrder -ne 'linear-address' -or $transport.writtenSha256 -ne $preparation.prepared_sha256) { throw 'Complete-image write receipt mismatch.' }
    if (!$transport.wholeChipResetRequested) { throw 'Native final reset was not requested; startup/retained-runtime cleanup remains unconfirmed.' }
    if (!$SkipReadbackVerify -and ((Get-Item -LiteralPath $readback).Length -ne $transport.writtenBytes -or (Get-FileHash -LiteralPath $readback).Hash -ne $transport.writtenSha256 -or $transport.readbackSha256 -ne $transport.writtenSha256)) { throw 'Complete-image readback receipt mismatch.' }
    $names=Get-TlsrExpectedBluetoothNames -MacAddress $identity
    $appName=[string]$preparation.application_name_hint
    if (!$appName) { throw 'Prepared Settings have no valid application-name hint.' }
    $receipt=@{schema='inktag-tlsr-full-flash-v1';status=$transport.status;board=$Board;pcb=$review.board_marking;identity=$identity;port=$Port;observedFlashJedec=$observedJedec;targetLayout=$review.target_layout;configSchema=1;configSha256=$review.configuration_sha256;basePackageSha256=$review.base_package_sha256;preparedSha256=$transport.preparedSha256;planSha256=$transport.planSha256;writtenBytes=$transport.writtenBytes;writtenSha256=$transport.writtenSha256;readbackOrder=$transport.readbackOrder;readbackSha256=$transport.readbackSha256;readbackSkipped=$transport.readbackSkipped;candidateEpoch=$preparation.recovery_epoch;recoveryStateInitialized=$true;recoveryStateVerified=$true;allUserDataCleared=$false;upperFlashTouched=$preparation.upper_flash_touched;retainedHolesVerified=$false;sourceLayout=$preparation.source_layout;stockBackup=$StockBackup;expectedAppBluetoothName=$names.App;expectedRecoveryBluetoothName=$names.Recovery;bluetoothObserved=$false;displayObserved=$false;applicationStartResetRequested=$true;releaseQualified=$false;finishedUtc=[DateTime]::UtcNow.ToString('o')}
    $receipt.expectedAppBluetoothName=$appName
    $receipt.userStateCleared='password/QR control, Find, preferences/name, fonts, main-Flash images/slots/slideshow, old diagnostic history'
    $receipt.proPreservation=$transport.proPreservation
    $receipt.externalRetained=$transport.externalRetained
    $receipt.retainedDiagnostics=$transport.retainedDiagnostics
    $receipt.diagnosticCleanup=$transport.diagnosticCleanup
    $receipt.factoryFallbackBluetoothName=$names.App
    $receipt.nameHintAuthority=$preparation.name_hint_authority
    $receipt.confirmation=if ($Yes) {'explicit-yes'} else {'per-unit-y'}
    $receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptPath -Encoding UTF8
    Write-InkTagTerminalMessage ('Congratulations! {0}. Evidence: {1}. Retained holes were not read or claimed verified.' -f $transport.status,$receiptPath) -Level SUCCESS
    Write-InkTagTerminalMessage ('App reset requested; Settings name hint {0}; factory fallback {1}; Recovery {2}. Runtime entitlement/startup/BLE/display are not observed.' -f $appName,$names.App,$names.Recovery)
} catch {
    if ($receiptPath -and !(Test-Path -LiteralPath $receiptPath)) { @{schema='inktag-tlsr-full-flash-v1';status='FAILED-OR-UNKNOWN';phase=$phase;error=$_.Exception.Message;writeRetryAllowed=$false;finishedUtc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $receiptPath -Encoding UTF8 }
    Write-InkTagTerminalMessage $_.Exception.Message -Level ERROR
    exit 1
} finally { Restore-InkTagTerminalColour $fullTerminalColour }
