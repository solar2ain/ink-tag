param(
    [ValidateSet("tlsr8359_nebular420r", "tlsr8359_nebular213r", "tlsr8359_nowa154r", "tlsr8359_nowa213r", "hs_nebular420", "hs_nebular213", "hs_nowa154", "hs_nowa213")]
    [string]$Board,
    [ValidateSet("tlsr825x", "tlsr8359", "hs9118")]
    [string]$Chip = "tlsr825x",
    [string]$Port = "auto",
    [int]$Baud = 0,
    [int]$ActivationMs = 0,
    [int]$ClockMHz = -1,
    [string]$OutputDir = "",
    [string]$ToolPath = "",
    [string]$Python = "python",
    [switch]$NoManualReset,
    [switch]$LegacyActivation,
    [switch]$AllowUnqualifiedBaud,
    [switch]$TlsrDebug,
    [switch]$ShowCommand,
    [switch]$Wait,
    [switch]$NoWait,
    [int]$IntervalSeconds = 1,
    [int]$TimeoutSeconds = 0,
    [Alias("h")]
    [switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)]
    [object[]]$RemainingArguments
)

. (Join-Path $PSScriptRoot 'terminal-log.ps1')
$probeOperationColour = Get-InkTagTerminalColour
try {
    Set-InkTagTerminalEnglish
. (Join-Path $PSScriptRoot "..\..\..\..\..\..\tools\powershell-cli.ps1")
$cliResult = Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help `
    -RemainingArguments $RemainingArguments -Summary "Internal bounded SWS transport probe; no Flash access. Activation may reset/halt the target."
if ($null -ne $cliResult) { exit $cliResult }
if ($Board -and $PSBoundParameters.ContainsKey("Chip")) {
    throw "Use either -Chip for a transport probe or -Board for a board preset, not both."
}
if ($Wait -and $NoWait) { throw "Use either -Wait or -NoWait." }
if ($IntervalSeconds -lt 1 -or $TimeoutSeconds -lt 0) { throw "IntervalSeconds >= 1 and TimeoutSeconds >= 0 required." }

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..\..\..\..\..")).Path
. (Join-Path $PSScriptRoot "tlsr8359-uart-sws-common.ps1")
if ($Wait) {
    # Reuse the one maintained host contact loop. It calls this script without
    # -Wait for each attempt; no nested loop, backup or programmer replay.
    if ($LegacyActivation -or $TlsrDebug) { throw "Wait supports the maintained ordinary probe only, not legacy/debug transport modes." }
    $waitArgs = @("probe", "--wait", "--port", $Port,
        "--interval-seconds", "$IntervalSeconds", "--timeout-seconds", "$TimeoutSeconds",
        "--probe-baud", "$Baud", "--probe-activation-ms", "$ActivationMs",
        "--probe-clock-mhz", "$ClockMHz", "--powershell", (Get-Process -Id $PID).Path)
    if ($Board) { $waitArgs += @("--board", $Board) } else { $waitArgs += @("--chip", $Chip) }
    if ($ToolPath) { $waitArgs += @("--tool-path", $ToolPath) }
    if ($OutputDir) { $waitArgs += @("--log-dir", $OutputDir) }
    if ($AllowUnqualifiedBaud) { $waitArgs += "--allow-unqualified-probe-baud" }
    if ($ShowCommand) { $waitArgs += "--verbose" }
    $waitPython = Resolve-TlsrPython -RequestedPython $Python
    & $waitPython (Join-Path $repoRoot "tools/hardware/flash-workflow.py") @waitArgs
    exit $LASTEXITCODE
}
if ($Board) {
    $target = Get-Tlsr8359ResearchTarget -Board $Board
    $Board = $target.BoardId
    $probeLabel = $Board
} else {
    # This selects only a reviewed SWS transport family. It proves neither
    # exact silicon identity nor board wiring/Flash capacity/write permission.
    $target = [pscustomobject]@{
        DisplayName = "$Chip SWS transport (exact chip/PCB unconfirmed)"
        QualifiedProbeBaud = 921600
        ProbeActivationMs = 30000
        ProbeClockMHz = 0
        CapacityEvidence = "not-established-by-transport-probe"
    }
    $probeLabel = $Chip
}
$Port = Resolve-TlsrSerialPort -RequestedPort $Port
if ($Baud -eq 0) { $Baud = $target.QualifiedProbeBaud }
if ($ActivationMs -eq 0) { $ActivationMs = $target.ProbeActivationMs }
if ($ClockMHz -lt 0) { $ClockMHz = $target.ProbeClockMHz }
if ($Board -and $Baud -ne $target.QualifiedProbeBaud -and !$AllowUnqualifiedBaud) {
    throw "Board '$Board' has qualified probe baud $($target.QualifiedProbeBaud). Use -AllowUnqualifiedBaud only for a read-only experiment."
}
if ($OutputDir -eq "") { $OutputDir = Join-Path $repoRoot ".local\probes" }
$OutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
$targetDir = Join-Path $OutputDir ("{0}-{1}" -f $probeLabel, (Get-Date -Format "yyyyMMdd-HHmmss-fff"))
New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
$infoFile = Join-Path $targetDir "probe-info.txt"
$logFile = Join-Path $targetDir "probe.log"
$invoke = Join-Path $PSScriptRoot "invoke-tlsr8359-uart-sws.ps1"
$forward = @{
    Operation = "probe"
    Port = $Port
    Baud = $Baud
    ActivationMs = $ActivationMs
    ClockMHz = $ClockMHz
    Python = $Python
    Run = $true
}
if ($ToolPath -ne "") { $forward["ToolPath"] = $ToolPath }
if ($TlsrDebug) { $forward["TlsrDebug"] = $true }
if ($ShowCommand) { $forward["ShowCommand"] = $true }
if ($LegacyActivation) { $forward["LegacyActivation"] = $true }
if ($NoManualReset) { $forward["NoManualReset"] = $true }

Write-InkTagTerminalStage 'PROBE: SWS transport only; no Flash read/write'
Write-InkTagTerminalMessage ("Target: {0}; port: {1}; evidence: {2}" -f $target.DisplayName, $Port, $targetDir)
if ($NoManualReset) {
    Write-InkTagTerminalMessage ("Automatic activation; keep power/wiring stationary. Maximum window: {0:N0}s; RTS/SWS reset requests may occur; they are not reset attestation." -f ($ActivationMs / 1000))
} else {
    Write-TlsrResetPrompt -Action "Start the read-only SWS probe." -ActivationMs $ActivationMs
}
& $invoke @forward 2>&1 | Tee-Object -FilePath $logFile | Show-InkTagTerminalLog
Set-Content -LiteralPath $infoFile -Encoding ASCII -Value @(
    "schema=inktag-tlsr8359-probe-v1",
    "board_id=$Board",
    "requested_chip_family=$Chip",
    "exact_chip_identified=false",
    "target_name=$($target.DisplayName)",
    "preset_capacity_evidence=$($target.CapacityEvidence)",
    "method=USB-UART SWS",
    "port=$Port",
    "baud=$Baud",
    "activation_ms=$ActivationMs",
    "result=transport-responded",
    "log_file=probe.log",
    ("probed_utc={0}" -f (Get-Date).ToUniversalTime().ToString("o"))
)
Write-InkTagTerminalMessage 'SWS transport responded. Exact chip/physical Flash capacity are not qualified.' -Level SUCCESS
Write-InkTagTerminalMessage ("Probe record: {0}; raw log: {1}" -f $infoFile, $logFile)
} catch {
    Write-InkTagTerminalMessage $_.Exception.Message -Level ERROR
    exit 1
} finally { Restore-InkTagTerminalColour $probeOperationColour }
