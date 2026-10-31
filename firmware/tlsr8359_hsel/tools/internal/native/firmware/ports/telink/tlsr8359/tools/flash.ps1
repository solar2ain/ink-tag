[CmdletBinding()]
param(
    [Parameter(HelpMessage='Required compatible TLSR PCB TOML/YAML/JSON preset; applies screen and LED defaults. Missing historical Flash records warn; fresh supported Flash identification remains mandatory.')][string]$Config = '',
    [Parameter(HelpMessage='Required app-full.zip package containing the common signed App and Recovery.')][string]$Firmware = '',
    [Parameter(HelpMessage='Serial port, for example COM13. Full requires an exact port; probe/backup may auto-select a qualified adapter.')][string]$Port = 'auto',
    [Parameter(HelpMessage='Optional exact target MAC guard; refuse a different unit. Never borrowed from another board.')][string]$ExpectedMac = '',
    [Parameter(HelpMessage='Optional verified exact-unit stock rescue for archive-reference mode. Normal Full does not require a backup or compare stock firmware; it preserves freshly read identity/calibration. Run backup separately if original-firmware recovery is wanted.')][string]$StockBackup = '',
    [Parameter(HelpMessage='Confirm the displayed Full user-data wipe without prompting. Default asks y/N; never skips signature, identity, calibration, capacity or readback checks.')][switch]$Yes,
    [Parameter(HelpMessage='Skip complete post-write readback only for later flashes after wiring is stable. Keep full readback enabled for the first flash. New Recovery/Settings write verification remains. Result is NOT VERIFIED; default verifies the complete prepared Full image.')][switch]$SkipReadbackVerify,
    [Parameter(HelpMessage='Write UART baud; 0 selects the registered qualified fixture value. Arbitrary Full-write overrides are refused.')][int]$Baud = 0,
    [Parameter(HelpMessage='Read UART baud; 0 selects the registered qualified fixture value.')][int]$ReadBaud = 0,
    [Parameter(HelpMessage='Activation window in milliseconds; 0 selects the registered fixture value.')][int]$ActivationMs = 0,
    [Parameter(HelpMessage='RTS-to-board-RESET pulse duration, 50..2000 milliseconds; default 500. Requires physical RTS wiring.')][int]$ResetPulseMs = 500,
    [Parameter(HelpMessage='SWS CPU-clock estimate; -1 selects the registered qualified Full fixture value.')][int]$ClockMHz = -1,
    [Parameter(HelpMessage='Optional path to the maintained UART/SWS backend; normally discovered automatically.')][string]$ToolPath = '',
    [Parameter(HelpMessage='Python executable for the maintained host tools.')][string]$Python = 'python',
    [Parameter(HelpMessage='Retry detection only; never automatically repeat Flash programming or an unknown write.')][switch]$RetryDetect,
    [Parameter(HelpMessage='Detection window in seconds when RetryDetect is selected; 1..3600.')][int]$DetectTimeoutSeconds = 120,
    [Parameter(HelpMessage='Validate and print the offline plan only; no serial/reset/Flash/device access.')][switch]$DryRun,
    [Parameter(HelpMessage='Show all options and descriptions without accessing a device.')][Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)][object[]]$RemainingArguments
)
. (Join-Path $PSScriptRoot 'internal/terminal-log.ps1')
$flashTerminalColour = Get-InkTagTerminalColour
try {
    Set-InkTagTerminalEnglish
    $ErrorActionPreference = 'Stop'
    $repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../../../../')).Path
    . (Join-Path $repoRoot 'tools/powershell-cli.ps1')
    $guard = Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help `
        -RemainingArguments $RemainingArguments -Summary 'TLSR complete flashing: preserves valid original same-device Pro and identity/calibration, clears other user/diagnostic state after y/N; explicit package + PCB Config; complete readback by default; SkipReadbackVerify marks NOT VERIFIED'
    if ($null -ne $guard) { exit $guard }
    if ($ResetPulseMs -lt 50 -or $ResetPulseMs -gt 2000) { throw 'Invalid ResetPulseMs. Expected 50..2000 milliseconds; no device accessed.' }
    if ($DetectTimeoutSeconds -lt 1 -or $DetectTimeoutSeconds -gt 3600) { throw 'Invalid DetectTimeoutSeconds. Expected 1..3600 seconds; no device accessed.' }
    if (!$Config -or !$Firmware) { throw '-Config <board.toml/yaml/json> and -Firmware <app-full.zip> are required. No legacy Board/bin mode.' }
    # Internal source-family detection is automatic; never implicitly read the whole chip.
    . (Join-Path $PSScriptRoot 'internal/tlsr8359-uart-sws-common.ps1')
    $pythonExe = Resolve-TlsrPython -RequestedPython $Python
    $planner = Join-Path $repoRoot 'tools/hardware/tlsr_full_flash_plan.py'
    $options = @('--base', $Firmware, '--config', $Config)
    if ($SkipReadbackVerify) { $options += '--skip-readback-verify' }
    if (!$DryRun) {
        if ($Port -eq 'auto') { throw 'Full requires an exact -Port; automatic port selection is not allowed.' }
        if ($ExpectedMac -and $ExpectedMac -notmatch '^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$') { throw 'Invalid -ExpectedMac; no device accessed.' }
        $template = Join-Path $repoRoot ('.local/full-config/prepared/{0}-{1}.bin' -f [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'), [Guid]::NewGuid().ToString('N'))
        $options += @('--require-write', '--output', $template)
    }
    $reviewJson = & $pythonExe $planner @options
    if ($LASTEXITCODE -ne 0) { throw 'Config/base validation failed; no device accessed.' }
    $review = ($reviewJson -join "`n") | ConvertFrom-Json
    if ($review.write_admitted) { $null = Get-Tlsr8359FullTarget -ConfigReview $review }
    if ($DryRun) {
        # Pure offline review: no programmer resolution, probe/reset or files.
        $reviewJson
        exit 0
    }
    $operation = @{
        Config=(Resolve-Path -LiteralPath $Config).Path; BaseFirmware=(Resolve-Path -LiteralPath $Firmware).Path
        Board=$review.board; Firmware=$template; Port=$Port; ExpectedMac=$ExpectedMac
        StockBackup=$StockBackup
        Yes=$Yes.IsPresent
        SkipReadbackVerify=$SkipReadbackVerify.IsPresent
        Baud=$Baud; ReadBaud=$ReadBaud; ActivationMs=$ActivationMs; ClockMHz=$ClockMHz; ResetPulseMs=$ResetPulseMs
        ToolPath=$ToolPath; Python=$pythonExe
        RetryDetect=$RetryDetect.IsPresent; DetectTimeoutSeconds=$DetectTimeoutSeconds
        ShowCommand=[bool]$PSBoundParameters['Verbose']
    }
    Write-InkTagTerminalStage 'CONFIGURED FULL'
    foreach ($notice in $review.qualification_warnings) { Write-InkTagTerminalMessage $notice -Level WARNING }
    Write-InkTagTerminalMessage 'Complete flashing preserves only verified original same-device Pro plus identity/address type/calibration; clears other mutable user and diagnostic state after y/N. Unrecognized, damaged, incompatible or foreign authorization cannot be restored and will be cleared; the device initializes Free and needs normal activation for Pro. Separate NFC EEPROM is not erased. Recovery state is rebuilt from the selected package.' -Level WARNING
    Write-InkTagTerminalMessage ('Config: {0}; PCB: {1}; base SHA: {2}' -f $Config, $review.board_marking, $review.base_sha256)
    if ($SkipReadbackVerify) { Write-InkTagTerminalMessage 'Explicit SkipReadbackVerify: preservation checks -> bounded preparation -> one write -> App start. Installation will be NOT VERIFIED. No automatic write retry.' -Level WARNING }
    else { Write-InkTagTerminalMessage 'Bounded identity/calibration preservation -> current defaults and Recovery state -> complete Full write/readback in one halted session. No write retry.' }
    & (Join-Path $PSScriptRoot 'internal/full-flash.ps1') @operation
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} catch {
    Write-InkTagTerminalMessage $_.Exception.Message -Level ERROR
    exit 1
} finally { Restore-InkTagTerminalColour $flashTerminalColour }
