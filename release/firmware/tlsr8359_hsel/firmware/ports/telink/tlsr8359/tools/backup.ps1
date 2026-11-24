[CmdletBinding()]
param(
    # Requested marking is retained; this owning tool publishes under tlsr8359.
    [Parameter(HelpMessage='Required requested MCU marking: tlsr8359 or hs9118. This Telink tool archives under tlsr8359 and retains the requested marking as metadata.')][string]$Chip = "",
    [Parameter(HelpMessage='Required PCB silkscreen used for factory archive grouping; new markings are allowed for backup only.')][string]$Board = "",
    [Parameter(HelpMessage='Optional board remark, such as a production code; stored in metadata, not an extra path level.')][string]$BoardNote = "",
    [Parameter(HelpMessage='Serial port, for example COM13. Full requires an exact port; probe/backup may auto-select a qualified adapter.')][string]$Port = "auto",
    [Parameter(HelpMessage='Retry detection only; never automatically repeat Flash programming or an unknown write.')][switch]$RetryDetect,
    [Parameter(HelpMessage='Independently read and compare against the latest complete first capture for this board; error if none exists.')][Alias("VerfiyOnly")][switch]$VerifyOnly,
    [Parameter(HelpMessage='Resume an exited partial capture in new segments after y/N confirmation of the same board; never overwrite old fragments.')][switch]$Resume,
    [Parameter(HelpMessage='Flash-read UART baud. Default 921600; 460800 is also standard; 0 selects the backend default.')][int]$Baud = 921600,
    [Parameter(HelpMessage='UART baud during initial activation; 0 follows the read baud.')][int]$ActivationBaud = 0,
    [Parameter(HelpMessage='Maximum activation window in milliseconds; stop early when SWS validation succeeds.')][int]$ActivationMs = 20000,
    # 0 keeps the reader's automatic CPU-clock selection (not fixed 24 MHz).
    [Parameter(HelpMessage='SWS CPU-clock estimate in MHz, 0..192; 0 keeps automatic selection.')][int]$ClockMHz = 0,
    [Parameter(HelpMessage='Allow a custom backup UART baud >=340000; standard 460800/921600 require no override. Not Full-write qualification.')][switch]$AllowUnqualifiedBaud,
    [Parameter(HelpMessage='Seconds between outer detection attempts; not a data-read retry interval.')][int]$IntervalSeconds = 1,
    [Parameter(HelpMessage='Overall detection-wait limit in seconds; 0 means no overall wait limit.')][int]$TimeoutSeconds = 0,
    [Parameter(HelpMessage='Maximum seconds for one tool operation; does not replay failed operations.')][int]$OperationTimeoutSeconds = 7200,
    [Parameter(HelpMessage='Optional evidence/output directory; backup uses the maintained factory archive layout by default.')][string]$OutputDir = "",
    [Parameter(HelpMessage='Optional path to the maintained UART/SWS backend; normally discovered automatically.')][string]$ToolPath = "",
    [Parameter(HelpMessage='Python executable for the maintained host tools.')][string]$Python = "python",
    [Parameter(HelpMessage='Validate and print the offline plan only; no serial/reset/Flash/device access.')][switch]$DryRun,
    [Parameter(HelpMessage='Show all options and descriptions without accessing a device.')][Alias("h")][switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)][object[]]$RemainingArguments
)
. (Join-Path $PSScriptRoot 'internal/terminal-log.ps1')
$backupTerminalColour = Get-InkTagTerminalColour
try {
Set-InkTagTerminalEnglish
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "../../../../..")).Path
. (Join-Path $repoRoot "tools/powershell-cli.ps1")
$guard = Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help `
    -RemainingArguments $RemainingArguments -Summary "TLSR stock backup: two independent full reads; no Flash write. SWS activation may reset/halt the target."
if ($null -ne $guard) { exit $guard }
if ($Chip -notin @('tlsr8359', 'hs9118')) { exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "-Chip tlsr8359 or hs9118 and -Board <PCB silkscreen> are required; new PCB markings are allowed for backup only. No device accessed.") }
if ([string]::IsNullOrWhiteSpace($Board)) {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message 'Backup requires -Board <PCB silkscreen>; new markings are allowed. No device accessed.')
}
$Board = $Board.Trim()
if ($Baud -lt 0 -or ($Baud -ne 0 -and $Baud -lt 340000) -or
    $ActivationBaud -lt 0 -or ($ActivationBaud -ne 0 -and $ActivationBaud -lt 340000) -or
    $ActivationMs -le 0 -or $ClockMHz -lt 0 -or $ClockMHz -gt 192 -or
    ($Baud -notin @(0,460800,921600) -and !$AllowUnqualifiedBaud)) {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message 'Invalid read transport settings. Standard read baud: 460800/921600; custom baud >=340000 requires -AllowUnqualifiedBaud. ActivationMs >0; ClockMHz 0..192 (0=auto). No device accessed.')
}
. (Join-Path $PSScriptRoot "internal/tlsr8359-uart-sws-common.ps1")
$pythonExe = Resolve-TlsrPython -RequestedPython $Python
$engineArgs = @("backup", "--chip", $Chip, "--port", $Port,
    "--interval-seconds", "$IntervalSeconds", "--timeout-seconds", "$TimeoutSeconds",
    "--operation-timeout-seconds", "$OperationTimeoutSeconds", "--powershell", (Get-Process -Id $PID).Path)
$engineArgs += @("--board", $Board)
if ($BoardNote) { $engineArgs += @("--board-note", $BoardNote) }
if ($VerifyOnly) { $engineArgs += "--verify-only" }
if ($Resume) { $engineArgs += "--resume" }
$engineArgs += @('--backup-baud', "$Baud", '--backup-activation-baud', "$ActivationBaud",
    '--backup-activation-ms', "$ActivationMs", '--backup-clock-mhz', "$ClockMHz")
if ($AllowUnqualifiedBaud) { $engineArgs += '--allow-unqualified-backup-baud' }
if (!$RetryDetect) { $engineArgs += "--no-wait" }
if (($VerifyOnly -or $Resume) -and $OutputDir) { throw 'VerifyOnly/Resume selects preserved captures automatically; do not specify OutputDir.' }
if ($ToolPath) { $engineArgs += @("--tool-path", $ToolPath) }
if ($DryRun) { $engineArgs += "--dry-run" }
if ($PSBoundParameters.ContainsKey("Verbose") -and $PSBoundParameters["Verbose"]) { $engineArgs += "--verbose" }
if (!$DryRun -and $Host.Name -eq 'ConsoleHost' -and ![Console]::IsOutputRedirected) { $engineArgs += "--console-output" }
. (Join-Path $repoRoot 'tools/stock-backup.ps1')
. (Join-Path $repoRoot 'tools/hardware/tlsr-stock-archive.ps1')
if ($DryRun) {
    $plannedRaw = if ($OutputDir) { $OutputDir } else { Join-Path (Get-InkTagBackupRoot $repoRoot) ".local/backup-archive/$Chip/pending/SESSION-TIMESTAMP" }
    $engineArgs += @('--factory-session-dir', $plannedRaw)
} else {
    $session = New-InkTagStockBackupSession -RepoRoot $repoRoot -Chip $Chip -Board $Board -BoardNote $BoardNote -OutputDir $OutputDir
    [pscustomobject]@{schema='inktag-tlsr-raw-session-v1';session=$session;factorySourceConfirmed=$true;
        sharedHelperSource='88f4d268c92cb1191dfdb611abc1dbe716138a8c';hardwareAuthority='human-started-read-only-backup'} |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $session.Directory 'session.json') -Encoding UTF8
    $engineArgs += @('--factory-session-dir', $session.Directory)
}
& $pythonExe (Join-Path $repoRoot "tools/hardware/flash-workflow.py") @engineArgs
$readExit = $LASTEXITCODE
if ($DryRun -or $readExit -ne 0) { exit $readExit }
try {
    $archive = Complete-TlsrStockArchive -Session $session -FactorySourceConfirmed:$true -Python $pythonExe
    Write-Host ("{0} [SUCCESS] Backup archive: {1}" -f [DateTimeOffset]::Now.ToString('yyyy-MM-dd HH:mm:ss zzz'), $archive) -ForegroundColor Green
    Write-InkTagTerminalMessage 'Congratulations! Your factory backup is complete: both independent reads matched and the archive was validated.' -Level SUCCESS
} catch {
    Write-Host ("{0} [ERROR] Archive refused: {1}. Raw captures preserved; no hardware replay." -f [DateTimeOffset]::Now.ToString('yyyy-MM-dd HH:mm:ss zzz'), $_.Exception.Message) -ForegroundColor Red
    exit 1
}
exit 0
} catch {
    # Render synchronously inside the colour guard, not as an unhandled host
    # ErrorRecord formatted after finally has restored the console.
    Write-InkTagTerminalMessage $_.Exception.Message -Level ERROR
    exit 1
} finally { Restore-InkTagTerminalColour $backupTerminalColour }
