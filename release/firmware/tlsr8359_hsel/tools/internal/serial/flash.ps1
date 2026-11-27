[CmdletBinding()]
param(
    [Parameter(HelpMessage='Required compatible TLSR PCB TOML/YAML/JSON preset; applies screen and LED defaults. Missing historical Flash records warn; fresh supported Flash identification remains mandatory.')][string]$Config = '',
    [Parameter(HelpMessage='Required complete-firmware ZIP with paired App and Recovery: native CRC-only Base or strictly publisher-verified signed Pro.')][string]$Firmware = '',
    [Parameter(HelpMessage='Serial port, for example COM13. Full requires an exact port; probe/backup may auto-select a qualified adapter.')][string]$Port = 'auto',
    [Parameter(HelpMessage='Optional exact target MAC guard; refuse a different unit. Never borrowed from another board.')][string]$ExpectedMac = '',
    [Parameter(HelpMessage='Preservation-install policy: unusable optional UID data is absent, so Pro is not restored; complete SWS transport, chip/capacity and readback remain required.')][switch]$AllowMissingUid,
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
    [Parameter(HelpMessage='Require explicit inputs; never open dialogs or prompt for missing values.')][switch]$NonInteractive,
    [Parameter(HelpMessage='Show all options and descriptions without accessing a device.')][Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)][object[]]$RemainingArguments
)
if ($env:INKTAG_TLSR_FLASH_RETRY -eq '1') {
    if (!$PSBoundParameters.ContainsKey('RetryDetect')) { $RetryDetect = $true }
    if (!$PSBoundParameters.ContainsKey('DetectTimeoutSeconds')) { $DetectTimeoutSeconds = 0 }
}
$flashTerminalColour = $null
$guided = $env:INKTAG_TLSR_FLASH_GUIDED -eq '1' -and !$DryRun -and !$NonInteractive
$chinese = $env:INKTAG_TLSR_FLASH_LANGUAGE -eq 'zh'
try {
    $ErrorActionPreference = 'Stop'
    if ($chinese -and [Console]::IsOutputRedirected) {
        [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
    }
    . (Join-Path $PSScriptRoot 'terminal-log.ps1')
    $flashTerminalColour = Get-InkTagTerminalColour
    Set-InkTagTerminalEnglish
    $repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../..')).Path
    . (Join-Path $repoRoot 'tools/internal/serial/powershell-cli.ps1')
    $guard = Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help `
        -RemainingArguments $RemainingArguments -Summary 'TLSR complete flashing (recommended entry: flash.bat): preserves valid original same-device Pro and identity/calibration, clears other user/diagnostic state after y/N; explicit package + PCB Config; complete readback by default; SkipReadbackVerify marks NOT VERIFIED'
    if ($null -ne $guard) { exit $guard }
    if ($ResetPulseMs -lt 50 -or $ResetPulseMs -gt 2000) { throw 'Invalid ResetPulseMs. Expected 50..2000 milliseconds; no device accessed.' }
    if ($DetectTimeoutSeconds -lt 0 -or $DetectTimeoutSeconds -gt 3600) { throw 'Invalid DetectTimeoutSeconds. Expected 0..3600 seconds; no device accessed.' }
    if (!$DryRun -and $ExpectedMac -and $ExpectedMac -notmatch '^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$') {
        throw 'Invalid -ExpectedMac; no device accessed.'
    }
    . (Join-Path $PSScriptRoot 'file-selection.ps1')
    if ($guided) {
        Write-InkTagTerminalStage (Get-InkTagFlashText 'TLSR FLASH GUIDE' 'TLSR 完整刷机指南')
        Write-Host (Get-InkTagFlashText 'Use a 3.3 V CH340 USB-UART and the included SWS tools. Connect GND and RX directly, TX through a 1 to 1.8 kOhm resistor to the same SWS point. Use a stable target supply.' '使用 3.3 V CH340 USB 转串口和随附的 SWS 工具。共地，RX 直连 SWS，TX 经 1–1.8 kΩ 电阻接同一个 SWS 点。为标签提供稳定电源。')
        Write-Host (Get-InkTagFlashText 'Choose a complete firmware ZIP and the board, then confirm complete flashing before the SWS wait. Reset or power-cycle the tag during activation if needed. Press Ctrl+C to cancel waiting.' '选择完整固件 ZIP 和主板，确认完整刷机后再等待 SWS 连通。如有需要，在探测期间复位标签或给标签重新上电。按 Ctrl+C 停止等待。')
    }
    if (!$DryRun -and !$NonInteractive) {
        if ([string]::IsNullOrWhiteSpace($Firmware)) {
            $Firmware = Select-InkTagFlashFirmware -Directory $PSScriptRoot -FullDirectory (Join-Path $repoRoot 'full')
        }
        if ([string]::IsNullOrWhiteSpace($Config)) {
            $Config = Select-InkTagBoardConfig -Directory $PSScriptRoot
        }
        if (!$guided -and ([string]::IsNullOrWhiteSpace($Port) -or $Port -eq 'auto')) {
            $Port = (Read-Host (Get-InkTagFlashText 'Exact CH340 port, for example COM13 (empty cancels)' '输入实际 CH340 串口，例如 COM13（回车取消）')).Trim()
            if (!$Port) { throw (Get-InkTagFlashText 'Cancelled; no device accessed.' '已取消，未访问设备。') }
        }
    }
    if (!$Config -or !$Firmware) { throw '-Config <board.toml/yaml/json> and -Firmware <app-full.zip> are required. No legacy Board/bin mode.' }
    # Internal source-family detection is automatic; never implicitly read the whole chip.
    . (Join-Path $PSScriptRoot 'tlsr8359-uart-sws-common.ps1')
    $pythonExe = Resolve-TlsrPython -RequestedPython $Python
    $planner = Join-Path $repoRoot 'tools/internal/python/tlsr_full_flash_plan.py'
    $options = @('--base', $Firmware, '--config', $Config)
    if ($SkipReadbackVerify) { $options += '--skip-readback-verify' }
    if (!$DryRun) {
        if (!$guided -and $Port -eq 'auto') { throw 'Full requires an exact -Port; automatic port selection is not allowed.' }
        $template = Join-Path $repoRoot ('.local/full-config/prepared/{0}-{1}.bin' -f [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'), [Guid]::NewGuid().ToString('N'))
        $options += @('--require-write', '--output', $template)
    }
    $reviewJson = & $pythonExe $planner @options
    if ($LASTEXITCODE -ne 0) { throw (Get-InkTagFlashText 'Config/base validation failed; no device accessed.' '主板配置或完整固件校验失败，未访问设备。') }
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
        AllowMissingUid=$AllowMissingUid.IsPresent
        SkipReadbackVerify=$SkipReadbackVerify.IsPresent
        Baud=$Baud; ReadBaud=$ReadBaud; ActivationMs=$ActivationMs; ClockMHz=$ClockMHz; ResetPulseMs=$ResetPulseMs
        ToolPath=$ToolPath; Python=$pythonExe
        RetryDetect=$RetryDetect.IsPresent; DetectTimeoutSeconds=$DetectTimeoutSeconds
        ShowCommand=[bool]$PSBoundParameters['Verbose']
    }
    Write-InkTagTerminalStage (Get-InkTagFlashText 'CONFIGURED FULL' '完整刷机配置')
    foreach ($notice in $review.qualification_warnings) { Write-InkTagTerminalMessage $notice -Level WARNING }
    $retention = Get-InkTagFlashText 'Complete flashing preserves only verified original same-device Pro plus identity/address type/calibration; clears other mutable user and diagnostic state after y/N. Unrecognized, damaged, incompatible or foreign authorization cannot be restored and will be cleared; the device initializes Free and needs normal activation for Pro. Separate NFC EEPROM is not erased. Recovery state is rebuilt from the selected package.' '完整刷机保留可验证的本机 Pro、设备地址及校准。确认后将清除密码、Find、用户设置、字库、主 Flash 图槽和旧诊断记录，并重建 Recovery 状态。无法验证的旧授权会清除。独立 NFC EEPROM 不擦除。'
    Write-InkTagTerminalMessage $retention -Level WARNING
    $configSummary = Get-InkTagFlashText 'Config: {0}; PCB: {1}; base SHA: {2}' '配置：{0}；主板：{1}；固件 SHA：{2}'
    $firmwareSummary = Get-InkTagFlashText 'Firmware: {0}; port: {1}' '固件：{0}；串口：{1}'
    Write-InkTagTerminalMessage ($configSummary -f $Config, $review.board_marking, $review.base_sha256)
    $portSummary = if ($guided -and $Port -eq 'auto') {
        Get-InkTagFlashText 'enter after confirmation' '确认后输入'
    } else { $Port }
    Write-InkTagTerminalMessage ($firmwareSummary -f (Resolve-Path -LiteralPath $Firmware).Path, $portSummary)
    if ($SkipReadbackVerify) { Write-InkTagTerminalMessage 'Explicit SkipReadbackVerify: preservation checks -> bounded preparation -> one write -> App start. Installation will be NOT VERIFIED. No automatic write retry.' -Level WARNING }
    else { Write-InkTagTerminalMessage 'Bounded identity/calibration preservation -> current defaults and Recovery state -> complete Full write/readback in one halted session. No write retry.' }
    & (Join-Path $PSScriptRoot 'full-flash.ps1') @operation
    $fullExit = $LASTEXITCODE
    if ($fullExit -ne 0) {
        if ($chinese) { Write-InkTagTerminalMessage ('完整刷机未成功，退出码 {0}。请查看上方日志。' -f $fullExit) -Level ERROR }
        exit $fullExit
    }
    if ($guided -and $chinese) { Write-InkTagTerminalMessage '完整刷机完成。' -Level SUCCESS }
} catch {
    if ($chinese) { Write-Host '刷机失败。请查看具体错误。' }
    if ($null -ne $flashTerminalColour) { Write-InkTagTerminalMessage $_.Exception.Message -Level ERROR }
    else { Write-Host $_.Exception.Message }
    exit 1
} finally {
    if ($null -ne $flashTerminalColour) { Restore-InkTagTerminalColour $flashTerminalColour }
    if ($guided -and $chinese) { Write-Host '按任意键退出。' }
}
