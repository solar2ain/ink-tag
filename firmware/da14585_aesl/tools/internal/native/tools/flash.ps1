[CmdletBinding()]
param(
    [ValidateSet('DA14585')][string]$Chip = 'DA14585',
    [Parameter(HelpMessage='Compatible signed native Full bin; not an App-only image.')][string]$Firmware = '',
    [Parameter(HelpMessage='Maintained board_marking TOML, not a live identity override.')][string]$Config = '',
    [string]$PythonExe = 'python',
    [Parameter(HelpMessage='Optional pinned J-Link 7.88j executable location; its matching DLL is hash-checked.')][string]$JLinkExe = '',
    [Alias('JLink','Snr')][ValidatePattern('^$|^[0-9]{4,16}$')][string]$JLinkSerial = '',
    [Parameter(HelpMessage='SWD frequency in Hz; positive whole kHz. Default 500000 Hz.')]
    [ValidateScript({$_ -ge 1000 -and $_ -le 4000000 -and $_ % 1000 -eq 0})][int]$FrequencyHz = 500000,
    [Parameter(HelpMessage='Legacy kHz spelling; cannot combine with FrequencyHz.')]
    [ValidateRange(1,4000)][int]$SpeedKHz,
    [Parameter(HelpMessage='Host program chunk; cannot exceed the fixed 256-byte Flash page. Not a multi-page batch.')]
    [ValidateSet(16,32,64,128,256)][int]$ProgramChunkBytes = 256,
    [Parameter(HelpMessage='Independent Flash readback chunk; the entire 256 KiB is always verified.')]
    [ValidateSet(256,512,1024,2048,4096)][int]$ReadChunkBytes = 4096,
    [Parameter(HelpMessage='Maximum native SRAM transfer block; mandatory SRAM readback stays enabled.')]
    [ValidateSet(64,128,256,512,1024,2048,4096)][int]$SramChunkBytes = 256,
    [Parameter(HelpMessage='Detection readiness only; never retries a failed/unknown Flash operation.')][switch]$RetryDetect,
    [Parameter(HelpMessage='Optional log parent directory; each invocation creates a unique child session.')][string]$OutputDir = '',
    [Parameter(HelpMessage='Offline package/configuration review; no target access.')][switch]$DryRun,
    [Parameter(HelpMessage='Confirms the default-No clearing scope; all live protections remain mandatory.')][switch]$Yes,
    [Parameter(HelpMessage='Per validation/backend operation limit; timeout fails without replay.')]
    [Alias('AttemptTimeoutSeconds')][ValidateRange(1,300)][int]$OperationTimeoutSeconds = 60,
    [Parameter(HelpMessage='Overall transaction child deadline; unknown results never replay.')]
    [ValidateRange(1,86400)][int]$FullTimeoutSeconds = 1800,
    [ValidateRange(1,300)][int]$IntervalSeconds = 1,
    [ValidateRange(0,86400)][int]$TimeoutSeconds = 0,
    [Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments=$true)][object[]]$RemainingArguments
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'internal/context.ps1')
$toolContext = Get-InkTagDaToolContext -ToolsRoot $PSScriptRoot
$RepoRoot = $toolContext.Root
. (Join-Path $PSScriptRoot 'internal/host-common.ps1')
$terminalState=Start-InkTagDaTerminal
try {
. (Resolve-InkTagDaToolFile $toolContext 'tools/powershell-cli.ps1')
$summary=if($toolContext.Portable){'DA14585 portable tools-only: offline Full preflight; device writing is unavailable'}else{'DA14585 complete flash: protected current-unit preparation, programming, independent readback and final start'}
$cli = Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help -RemainingArguments $RemainingArguments -Summary $summary
if ($null -ne $cli) { exit $cli }
if($PSBoundParameters.ContainsKey('SpeedKHz')){
    if($PSBoundParameters.ContainsKey('FrequencyHz')){throw 'Use FrequencyHz or SpeedKHz, not both; no device accessed.'}
    $FrequencyHz=$SpeedKHz*1000
}
$SpeedKHz=[int]($FrequencyHz/1000)
if (!$Firmware -or !$Config) { throw 'Both -Firmware <compatible Full bin> and -Config <board_marking TOML> are required; no separate Board/ID input.' }
if($toolContext.Portable -and !$DryRun){throw 'Portable Full writer is not qualified; no target accessed. Use -DryRun for offline validation. -Yes cannot enable programming.'}
Write-InkTagDaLog INFO "Transfer: SWD=${SpeedKHz}kHz; program=${ProgramChunkBytes}B; read=${ReadChunkBytes}B; SRAM=${SramChunkBytes}B; RetryDetect=$([bool]$RetryDetect)"
Write-InkTagDaStage 'COMPLETE FLASH / LOCAL PACKAGE VALIDATION'
$python = Resolve-InkTagDaPython $toolContext $PythonExe
Assert-InkTagDaToolEnvironment $toolContext $python -OperationTimeoutSeconds $OperationTimeoutSeconds
$operation=$null;$authLog='';$configLog=''
if(!$DryRun){
    $operation=New-InkTagDaToolLogDirectory $toolContext -OutputDir $OutputDir
    $authLog=Join-Path $operation 'package-validation.log';$configLog=Join-Path $operation 'config-validation.log'
    Write-InkTagDaLog INFO "Operation evidence: $operation"
}
$authentication=Invoke-InkTagDaLoggedProcess -Exe $python -Arguments @('-B',(Resolve-InkTagDaToolFile $toolContext 'tools/hardware/da14585-image-verify.py'),$Firmware,'--kind','full-template') -Log $authLog -OperationTimeoutSeconds $OperationTimeoutSeconds -Label 'Verify package' -Detailed:([bool]$PSBoundParameters['Verbose'])
if ($authentication.ExitCode -ne 0) { throw 'Publisher/native image verification rejected; no debugger resolved or opened.' }
$receipt=$authentication.Text|ConvertFrom-Json
if($receipt.appPublisherVerified -isnot [bool] -or !$receipt.appPublisherVerified -or $receipt.fullBytes -ne 262144 -or $receipt.layout -ne 'DAV1' -or $receipt.configuredWriterQualified -isnot [bool] -or $receipt.configuredWriterQualified){throw 'Incomplete or unsafe Full verification receipt; no target accessed.'}
Write-InkTagDaStage 'COMPLETE FLASH / CONFIGURATION AND DEFAULTS'
$configHash=(Get-FileHash -LiteralPath $Config -Algorithm SHA256).Hash.ToLowerInvariant()
$configured=Invoke-InkTagDaLoggedProcess -Exe $python -Arguments @('-B',(Resolve-InkTagDaToolFile $toolContext 'tools/hardware/da14585-board-config.py'),'--chip',$Chip,'--config',$Config,'--firmware',$Firmware) -Log $configLog -OperationTimeoutSeconds $OperationTimeoutSeconds -Label 'Check configuration' -Detailed:([bool]$PSBoundParameters['Verbose'])
if ($configured.ExitCode -ne 0 -or (Get-FileHash -LiteralPath $Config -Algorithm SHA256).Hash -ne $configHash) { throw 'Configuration/base-family preflight rejected or input changed; no debugger resolved or opened.' }
$plan = $configured.Text | ConvertFrom-Json
if($plan.firmware_family -ne 'da14585_aesl' -or $plan.board_id -notin @(2,3) -or $plan.base.bytes -ne 262144 -or $receipt.fullSha256 -notmatch '^[0-9a-fA-F]{64}$' -or $receipt.fullSha256 -ne $plan.base.sha256 -or (Get-FileHash -LiteralPath $Firmware -Algorithm SHA256).Hash -ne $plan.base.sha256){throw 'Package changed or configuration receipt incomplete; no target accessed.'}
Write-InkTagDaLog INFO "Selected: $($plan.firmware_family) / $($plan.product) / board_id=$($plan.board_id); image SHA256=$($plan.base.sha256)"
if($plan.gpio -and $plan.gpio.display){
    $displayPins=$plan.gpio.display
    Write-InkTagDaLog INFO ("GPIO: MOSI={0}; CLK={1}; CS={2}; DC={3}; RESET={4}; BUSY={5}; EN={6}; BS={7}" -f $displayPins.mosi,$displayPins.clock,$displayPins.cs,$displayPins.dc,$displayPins.reset,$displayPins.busy,$displayPins.enable,$displayPins.bs)
}
# Native Recovery/boot/GUI checks are version-independent format/CRC/bounds,
# not publisher authentication. The SRAM helper itself remains hash-pinned.
$transferArgs=@('--speed-khz',[string]$SpeedKHz,'--program-chunk-bytes',[string]$ProgramChunkBytes,
    '--read-chunk-bytes',[string]$ReadChunkBytes,'--sram-chunk-bytes',[string]$SramChunkBytes)
if($JLinkExe){$transferArgs+=@('--jlink-exe',$JLinkExe)}
$fixed=Invoke-InkTagDaLoggedProcess -Exe $python -Arguments (@('-B',(Resolve-InkTagDaToolFile $toolContext 'tools/hardware/da14585-full-transaction.py'),'--firmware',$Firmware,'--config',$Config,'--output','unused-offline','--validate-only')+$transferArgs) -Log $(if($operation){Join-Path $operation 'native-inputs.log'}else{''}) -OperationTimeoutSeconds $OperationTimeoutSeconds -Label 'Check native firmware and helper' -Detailed:([bool]$PSBoundParameters['Verbose'])
if($fixed.ExitCode -ne 0){throw 'Native firmware/helper validation rejected; no target accessed.'}
Write-InkTagDaLog WARNING 'Complete programming reinitializes settings/time/labels/replay/pin overrides and all 68 KiB image slots, then clears/readbacks the supported 32-byte RAM reset trace before start. Packaged fonts/defaults are reinstalled. No automatic backup.'
Write-InkTagDaLog WARNING 'Only the configured 256 KiB Flash window is written/read back; any additional physical Flash is preserved, not cleared or verified. OTP, immutable address/type and calibration remain protected.'
Write-InkTagDaLog INFO 'Immutable identity/calibration and OTP remain protected. Signature, voltage and independent complete readback checks remain required.'
if ($DryRun) {
    Write-InkTagDaLog INFO "DRY RUN: $($plan.firmware_family) / $($plan.product) / board_id=$($plan.board_id); image SHA256=$($plan.base.sha256)"
    Write-InkTagDaLog INFO 'Transaction: confirmed snapshots, current-unit preparation, guarded erase/program, independent complete readback, final same-unit check and sole App start. No target accessed in DryRun.'
    exit 0
}
Write-InkTagDaStage 'COMPLETE FLASH / CONFIRM CLEAR USER DATA'
Write-InkTagDaLog INFO "Chip: $Chip; board: $($plan.board_marking); J-Link: $(if($JLinkSerial){$JLinkSerial}else{'not selected yet'}); target not detected yet"
Write-InkTagDaLog INFO "Screen: $($plan.screen.ic), $($plan.screen.width)x$($plan.screen.height) $($plan.screen.color), recipe=$($plan.screen.recipe)"
if($Yes){
    Write-InkTagDaLog WARNING 'Explicit -Yes confirms the clearing scope; all signature, identity, voltage and readback checks remain required.'
} elseif(!(Confirm-InkTagDaFull)){
    Write-InkTagDaLog INFO 'Cancelled; no target accessed.'
    exit 0
}
Write-InkTagDaStage 'COMPLETE FLASH / TRANSACTION CORE'
$arguments=@('-B',(Resolve-InkTagDaToolFile $toolContext 'tools/hardware/da14585-full-transaction.py'),
    '--firmware',$Firmware,'--config',$Config,'--output',(Join-Path $operation 'transaction'),
    '--expected-firmware-sha',$receipt.fullSha256,'--expected-config-sha',$configHash,
    '--operation-timeout',[string]$OperationTimeoutSeconds,'--interval',[string]$IntervalSeconds,
    '--timeout',[string]$TimeoutSeconds,'--yes')
$arguments+=$transferArgs
if($JLinkSerial){$arguments+=@('--serial',$JLinkSerial)}
if($RetryDetect){$arguments+='--retry-detect'}
# Only the fixed pinned backend is selectable. Internal regression dependencies
# cannot be selected from any public CLI/config/environment input.
$transaction=Invoke-InkTagDaLoggedProcess -Exe $python -Arguments $arguments -Log (Join-Path $operation 'full-transaction.log') -OperationTimeoutSeconds $FullTimeoutSeconds -Label 'Complete Flash' -Detailed:([bool]$PSBoundParameters['Verbose'])
if($transaction.TimedOut -or $transaction.ExitCode -eq 124){
    throw [TimeoutException]::new('Full transaction timed out; result unknown. Preserve evidence; no write/reset/start replay.')
}
$json=@($transaction.Lines|Where-Object {([string]$_).TrimStart().StartsWith('{')})
if($json.Count -ne 1){throw 'Missing/ambiguous Full result; preserve evidence without replay.'}
$outcome=([string]$json[0])|ConvertFrom-Json
if($transaction.ExitCode -eq 0 -and $outcome.status -eq 'cancelled' -and $outcome.success -is [bool] -and !$outcome.success){
    Write-InkTagDaLog INFO 'Full cancelled; no device accessed or programming occurred.'
    exit 0
}
if($transaction.ExitCode -ne 0 -or $outcome.success -isnot [bool] -or !$outcome.success){
    if($outcome.error){throw [string]$outcome.error}
    throw 'Full transaction refused or incomplete. Preserve evidence; no automatic replay.'
}
if($outcome.status -ne 'verified' -or $outcome.startIssued -isnot [bool] -or !$outcome.startIssued -or
    $outcome.noReplay -isnot [bool] -or !$outcome.noReplay -or $outcome.outcomeUnknown -isnot [bool] -or $outcome.outcomeUnknown -or
    $outcome.preparedSha256 -notmatch '^[0-9a-fA-F]{64}$' -or $outcome.readbackSha256 -ne $outcome.preparedSha256){
    throw 'Incomplete or contradictory Full success receipt; preserve evidence without replay.'
}
Write-InkTagDaLog SUCCESS 'Complete Flash independently verified. Expected names are predictions, not observed BLE/display evidence.'
Write-InkTagDaLog INFO "Expected label: $($outcome.expectedLabel); App BLE: $($outcome.expectedAppName); Recovery BLE: $($outcome.expectedRecoveryName)"
} catch {
    Write-InkTagDaLog ERROR $_.Exception.Message
    Write-InkTagDaLog WARNING 'Complete Flash stopped. Preserve raw logs and transaction receipts; no automatic write/reset/start replay. Use -Verbose for local diagnostics.'
    if($_.Exception -is [TimeoutException]){exit 124}
    exit 1
} finally {Restore-InkTagDaTerminal $terminalState}
