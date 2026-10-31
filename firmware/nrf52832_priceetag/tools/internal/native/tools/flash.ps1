[CmdletBinding()]
param(
    [Parameter(HelpMessage='Complete app-full.zip containing the signed App and native Full image.')][string]$Firmware = '',
    [Parameter(HelpMessage='PCB TOML/JSON/YAML configuration; no executable build selection.')][string]$Config = '',
    [Parameter(HelpMessage='Programmer backend; only J-Link Full is currently qualified.')][string]$Programmer = 'jlink',
    [Parameter(HelpMessage='J-Link serial number when several probes are attached.')][Alias('Snr')][string]$JLinkSerial = '',
    [Parameter(HelpMessage='Optional path to the maintained J-Link executable.')][string]$JLinkExe = '',
    [Parameter(HelpMessage='Confirm the displayed user-data clearing and verified native Pro retention scope without an interactive y/N prompt.')][switch]$Yes,
    [Parameter(HelpMessage='Validate package/config and print the clearing plan; no device access or file generation.')][switch]$DryRun,
    [Parameter(HelpMessage='Exact MCU; no foreign-chip override.')][Alias('Target')][string]$Chip = 'nrf52832',
    [Parameter(HelpMessage='Qualified J-Link part selector.')][string]$Device = 'nRF52832_xxAA',
    [Parameter(HelpMessage='SWD frequency in whole kHz, expressed in Hz.')][int]$FrequencyHz = 500000,
    [Parameter(HelpMessage='Qualified J-Link connection mode; halt only.')][string]$ConnectMode = 'halt',
    [Parameter(HelpMessage='Explicit readiness retries only, never programming retries.')][switch]$RetryDetect,
    [Parameter(HelpMessage='Legacy explicit single detection attempt; conflicts with RetryDetect.')][switch]$NoWait,
    [Parameter(HelpMessage='Seconds between readiness attempts.')][int]$IntervalSeconds = 1,
    [Parameter(HelpMessage='Readiness limit; 0 is unlimited only with RetryDetect.')][int]$TimeoutSeconds = 0,
    [Parameter(HelpMessage='Maximum seconds per vendor operation; failed/unknown writes never replay.')]
    [ValidateRange(1,86400)][int]$OperationTimeoutSeconds = 180,
    [Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)][object[]]$RemainingArguments
)
. (Join-Path $PSScriptRoot 'internal/runtime.ps1')
. (Join-Path $PSScriptRoot 'internal/terminal-log.ps1')
$terminalState = Start-InkTagHardwareTerminal
try {
    $ErrorActionPreference = 'Stop'
    $repoRoot = (Get-NordicRuntimeRoot)
    . (Join-Path $repoRoot 'tools/powershell-cli.ps1')
    $summary='nRF52832 Full: signed package + Config + programmer; clears user data, retains only verified same-device native Pro, default y/N'
    if(Test-NordicToolBundle){$summary='nRF52832 TOOLS-ONLY: complete writer GUARDED; explicit signed Firmware and Config with -DryRun only; no device accessed'}
    $guard = Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help `
        -RemainingArguments $RemainingArguments -Summary $summary
    if ($null -ne $guard) { exit $guard }
    if (!$Firmware) { throw 'Full requires explicit -Firmware <external signed app-full.zip>; no device accessed.' }
    if (Test-NordicToolBundle) {
        if(!$DryRun){throw 'TOOLS-ONLY: HW56 complete writer is guarded pending exact-board qualification; no device accessed. Use an explicit signed Firmware with -DryRun for offline validation only.'}
        $defaults=Get-Content (Join-Path $PSScriptRoot 'bundle.json') -Raw|ConvertFrom-Json
        if(!$Config){$Config=Get-NordicBundlePath $defaults.config}
    }
    if (!$Firmware -or !$Config) { throw 'Full requires Firmware app-full.zip and Config; no device accessed.' }
    if ($Chip -ne 'nrf52832' -or $Device -ne 'nRF52832_xxAA' -or $Programmer -ne 'jlink' -or $ConnectMode -ne 'halt') {
        throw 'Full supports nRF52832 xxAA with the qualified J-Link halt recipe only; no device accessed.'
    }
    if (($RetryDetect -and $NoWait) -or $FrequencyHz -lt 1000 -or $FrequencyHz % 1000 -ne 0 -or $IntervalSeconds -lt 1 -or $TimeoutSeconds -lt 0) {
        throw 'Invalid readiness/frequency parameters or RetryDetect/NoWait conflict; no device accessed.'
    }
    if ($JLinkSerial -and $JLinkSerial -notmatch '^[0-9]+$') { throw 'Invalid exact J-Link serial; no device accessed.' }
    $configText = Invoke-NordicPython (Join-Path $repoRoot 'tools/hardware/nordic_full_config.py') --config $Config --base $Firmware
    if ($LASTEXITCODE -ne 0) { throw 'Full package/config validation failed; no device accessed.' }
    $review = ($configText -join "`n") | ConvertFrom-Json
    $review | Add-Member -NotePropertyName deviceAccess -NotePropertyValue $false
    $review | Add-Member -NotePropertyName retryDetect -NotePropertyValue ([bool]$RetryDetect)
    $review | Add-Member -NotePropertyName operationTimeoutSeconds -NotePropertyValue $OperationTimeoutSeconds
    $planner = Join-Path $repoRoot 'tools/sdk/nordic-full-settings.py'
    $preserveText = Invoke-NordicPython $planner describe --firmware $Firmware --config $Config
    if ($LASTEXITCODE -ne 0) { throw 'Preservation range validation failed; no device accessed.' }
    $preserveReview = ($preserveText -join "`n") | ConvertFrom-Json
    $review | Add-Member -NotePropertyName preservation -NotePropertyValue $preserveReview
    if ($DryRun) { $review | ConvertTo-Json -Depth 8; exit 0 }
    Assert-NordicBundlePrerequisites $Programmer $JLinkExe
    Write-InkTagHardwareStage 'CONFIGURED FULL / CLEAR MCU USER DATA'
    Write-InkTagHardwareMessage ("{0}; Full allows downgrade. Cleared user data cannot be restored by this tool; no automatic backup." -f $review.warning) -Level WARNING
    Write-InkTagHardwareMessage $review.recovery_trust -Level WARNING
    Write-InkTagHardwareMessage ('Config: {0}; PCB: {1}' -f $Config, $review.board_marking)
    Write-InkTagHardwareMessage ('Package SHA-256: {0}' -f $review.base_sha256)
    Write-InkTagHardwareMessage ('Selected target: {0}; programmer: {1}. Exact silicon identity check follows; no target detected yet.' -f $Device, $Programmer)
    Write-InkTagHardwareMessage ("No whole-Flash backup/pre-read; FICR/UICR + {0} necessary native authority/low-Flash bytes only. One write, independent complete readback, diagnostic cleanup, guarded reset. No write retry. No BLE." -f $preserveReview.mainFlashPreReadBytes)
    Write-InkTagHardwareMessage 'Progress is per operation, not final verification. Detailed native output is in the evidence logs; use -Verbose to display it.'
    if ($Yes) {
        Write-InkTagHardwareMessage 'Explicit -Yes confirms the displayed wipe; signature, identity, capacity and readback checks are not bypassed.' -Level WARNING
    } else {
        # Keep the question in redirected logs too: PS7 can suppress a
        # Read-Host -Prompt when stdin/stdout are redirected.
        Write-InkTagHardwareMessage 'Continue? [y/N]' -Level WARNING
        $answer = [string](Read-Host)
        if ($answer.Trim() -notmatch '^(?i:y|yes)$') {
            Write-InkTagHardwareMessage 'Full cancelled; no device accessed or programming occurred.'
            exit 0
        }
    }
    $token = [Guid]::NewGuid().ToString('N')
    $operationDirectory = Join-Path $repoRoot ".local/flashing/nrf52832-full-$token"
    New-Item -ItemType Directory -Path $operationDirectory | Out-Null
    $firmwareCopy = Join-Path $operationDirectory 'app-full.zip'
    $configCopy = Join-Path $operationDirectory ('board'+[IO.Path]::GetExtension($Config))
    Copy-Item -LiteralPath $Firmware -Destination $firmwareCopy
    Copy-Item -LiteralPath $Config -Destination $configCopy
    if ((Get-FileHash $firmwareCopy).Hash.ToLowerInvariant() -ne $review.base_sha256 -or
        (Get-FileHash $configCopy).Hash.ToLowerInvariant() -ne $review.configuration_sha256) {
        throw 'Confirmed package/config changed before target access; no programming.'
    }
    $Firmware = $firmwareCopy; $Config = $configCopy
    $capture = Join-Path $operationDirectory 'preservation'
    Write-InkTagHardwareMessage ("Operation evidence: {0}" -f $operationDirectory)
    Write-InkTagHardwareStage 'COMPLETE FLASH / EXACT PROBE AND SWD TARGET'
    & (Join-Path $PSScriptRoot 'internal/invoke-nrf52832-swd.ps1') -Operation preserve-full -Programmer $Programmer `
        -Target $Chip -Device $Device -Snr $JLinkSerial -JLinkExe $JLinkExe -FrequencyHz $FrequencyHz -ConnectMode $ConnectMode `
        -Wait ([bool]$RetryDetect) -IntervalSeconds $IntervalSeconds -TimeoutSeconds $TimeoutSeconds -OutputDir $capture `
        -SourceFirmware $Firmware -SourceConfig $Config -OperationTimeoutSeconds $OperationTimeoutSeconds `
        -Verbose:([bool]$PSBoundParameters['Verbose'])
    if ($LASTEXITCODE -ne 0) { throw 'Necessary preservation read failed; retained evidence, no Full write or replay.' }
    $capturedProbe = Get-Content -LiteralPath (Join-Path $capture 'probe.json') -Raw | ConvertFrom-Json
    if ($capturedProbe.serial -notmatch '^[0-9]+$' -or ($JLinkSerial -and $JLinkSerial -cne $capturedProbe.serial)) {
        throw 'Exact capture probe serial missing/changed; no Full write.'
    }
    $JLinkSerial = $capturedProbe.serial
    Write-InkTagHardwareStage 'COMPLETE FLASH / PREPARE CURRENT DEFAULTS'
    $planDirectory = Join-Path $operationDirectory 'plan'
    Invoke-NordicPython $planner prepare --capture $capture --firmware $Firmware --config $Config --output $planDirectory
    if ($LASTEXITCODE -ne 0) { throw 'Full preparation refused; capture retained, no write/replay.' }
    $plan = Get-Content -LiteralPath (Join-Path $planDirectory 'plan.json') -Raw | ConvertFrom-Json
    foreach ($warning in $plan.warnings) { Write-InkTagHardwareMessage $warning -Level WARNING }
    Write-InkTagHardwareMessage 'Prepared current defaults and native DFU state; rechecking exact identity, preserved bytes and voltage before one write.'
    Write-InkTagHardwareMessage ("Exact plan: {0}" -f $planDirectory) -Level DEBUG
    & (Join-Path $PSScriptRoot 'internal/invoke-nrf52832-swd.ps1') -Operation flash -Programmer $Programmer `
        -Target $Chip -Device $Device -Snr $JLinkSerial -JLinkExe $JLinkExe -FrequencyHz $FrequencyHz -ConnectMode $ConnectMode `
        -Wait $false -IntervalSeconds $IntervalSeconds -TimeoutSeconds $TimeoutSeconds `
        -Firmware (Join-Path $planDirectory 'prepared.hex') -EraseImageStore $false -ExpectedDeviceId $plan.deviceId `
        -FullPlanDirectory $planDirectory -CurrentCapture $capture -SourceFirmware $Firmware -SourceConfig $Config `
        -OperationTimeoutSeconds $OperationTimeoutSeconds -Verbose:([bool]$PSBoundParameters['Verbose'])
    $code = $LASTEXITCODE
    if ($code -ne 0) { Write-InkTagHardwareMessage 'Full failed; retained evidence, no automatic programming/reset replay.' -Level ERROR }
    exit $code
} catch {
    Write-InkTagHardwareMessage $_.Exception.Message -Level ERROR
    exit 1
} finally { Restore-InkTagHardwareTerminal $terminalState }
