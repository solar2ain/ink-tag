[CmdletBinding()]
param(
    [ValidateSet("probe", "backup", "capture")][string]$Operation,
    [string]$JLinkExe = "",
    [string]$Snr = "",
    [int]$FrequencyHz = 500000,
    [bool]$Wait = $false,
    [long]$DetectionDeadline = 0,
    [int]$IntervalSeconds = 1,
    [int]$TimeoutSeconds = 0,
    [ValidateRange(1,86400)][int]$OperationTimeoutSeconds = 120,
    [string]$OutputDir = "",
    [string]$PythonExe = "python",
    [string]$ArchiveBoard = "UNIDENTIFIED-PCB",
    [string]$BoardNote = "",
    [switch]$PassThru,
    [uint32]$ReadAddress,
    [ValidateRange(1, 64)][int]$ReadWords = 1,
    [ValidateRange(1, 16)][int]$ReadCount = 1,
    [ValidateRange(1, 1000)][int]$ReadIntervalMs = 500
)

. (Join-Path $PSScriptRoot 'terminal-log.ps1')
$terminalState = Get-InkTagTerminalColour
try {
Set-InkTagTerminalEnglish
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ($Snr -and $Snr -notmatch '^\d+$') { throw "J-Link serial must contain digits only." }
if ($IntervalSeconds -lt 1 -or $TimeoutSeconds -lt 0 -or $FrequencyHz -lt 1000) { throw "Invalid polling interval, timeout or SWD frequency" }
. (Join-Path $PSScriptRoot 'runtime-context.ps1')
$context = Get-InkTagRuntimeContext (Split-Path -Parent $PSScriptRoot)
if ($context.IsBundle -and ($Operation -notin @('probe','backup') -or $PSBoundParameters.ContainsKey('ReadAddress'))) { throw 'Developer configuration/memory capture is not provided by this user bundle' }
$repoRoot = $context.Root
. $context.JLinkHelper
$JLinkExe = Resolve-InkTagJLinkTool -ToolName "JLink.exe" -ExplicitPath $JLinkExe -RepoRoot $repoRoot
if ($Operation -eq 'backup') {
    if ($ArchiveBoard -notin @('aesl22_nrf', 'UNIDENTIFIED-PCB') -or
        ($BoardNote -and ($BoardNote -notmatch '^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$' -or $BoardNote.EndsWith('.') -or
        $BoardNote -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)'))) {
        throw 'Invalid registered board or board note'
    }
    if (!$OutputDir) {
        if ($context.IsBundle) {
            $OutputDir = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'InkTag/backup-archive'
        } else {
            $canonical = & $PythonExe (Join-Path $PSScriptRoot 'backup-layout.py') --repo $repoRoot --canonical-root
            if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve the canonical backup archive root' }
            $OutputDir = Join-Path ([string]$canonical) '.local/backup-archive'
        }
    }
    if (([IO.Path]::GetFullPath($OutputDir) -split '[\\/]') -match '^(backups|factory_backups)$') {
        throw 'The backups directory is stock-only; use a diagnostic archive root'
    }
} elseif (!$OutputDir) {
    $OutputDir = if ($context.IsBundle) { Join-Path ([IO.Path]::GetTempPath()) 'InkTag-flashing' }
                 else { Join-Path $repoRoot '.local/flashing' }
}
if ($context.IsBundle) {
    $bundleRoot = [IO.Path]::GetFullPath($repoRoot).TrimEnd([char[]]@('\','/'))
    $outputRoot = [IO.Path]::GetFullPath($OutputDir).TrimEnd([char[]]@('\','/'))
    if ($outputRoot.Equals($bundleRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $outputRoot.StartsWith($bundleRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'OutputDir must be outside the immutable tool bundle; no target accessed'
    }
}
if ($FrequencyHz -lt 1000) { throw "FrequencyHz must be >= 1000" }
$speedKHz = [Math]::Max(1, [int][Math]::Ceiling($FrequencyHz / 1000.0))
$deadline = if ($DetectionDeadline -gt 0) { $DetectionDeadline } elseif ($TimeoutSeconds -gt 0) {
    [Diagnostics.Stopwatch]::GetTimestamp() + [long]($TimeoutSeconds * [double][Diagnostics.Stopwatch]::Frequency)
} else { [long]::MaxValue }
$sessionRoot = if ($Operation -eq 'backup') { Join-Path $OutputDir 'nrf52811/failed' } else { $OutputDir }
$sessionDir = Join-Path $sessionRoot ("nrf52811_asel22-$Operation-" + [Guid]::NewGuid().ToString("N"))
if ($context.IsBundle -and $sessionDir -cmatch '[^\x20-\x7E]') {
    throw 'J-Link 7.88j command files require an ASCII-only output directory. Specify -OutputDir pointing to an ASCII-only directory. No J-Link process started.'
}
New-Item -ItemType Directory -Force -Path $sessionDir | Out-Null
Assert-InkTagJLink788j -JLinkExe $JLinkExe -LogDir $sessionDir -Verbose:($VerbosePreference -eq 'Continue')
$Snr = Resolve-InkTagJLinkSerial -Snr $Snr -JLinkExe $JLinkExe -LogDir $sessionDir -Verbose:($VerbosePreference -eq 'Continue')
Write-InkTagTerminalMessage "Raw logs: $sessionDir"

function Invoke-JLink([string[]]$Commands) {
    $id = [Guid]::NewGuid().ToString("N")
    $commandFile = Join-Path $sessionDir "$id.jlink"
    $stdout = Join-Path $sessionDir "$id.stdout.log"
    $stderr = Join-Path $sessionDir "$id.stderr.log"
    Set-Content -LiteralPath $commandFile -Encoding ASCII -Value ($Commands + "qc")
    $arguments = @("-NoGui", "1", "-Device", "nRF52811_xxAA", "-If", "SWD", "-Speed", "$speedKHz", "-AutoConnect", "1", "-CommanderScript", $commandFile)
    if ($Snr) { $arguments = @("-SelectEmuBySN", $Snr) + $arguments }
    $attemptTimeout = $OperationTimeoutSeconds
    if ($detecting -and $Wait -and $deadline -ne [long]::MaxValue) {
        $remaining = ($deadline - [Diagnostics.Stopwatch]::GetTimestamp()) / [double][Diagnostics.Stopwatch]::Frequency
        if ($remaining -le 0) { throw "Detection deadline expired; raw logs: $sessionDir" }
        # Native timeout is in whole seconds: round up by less than one second.
        $attemptTimeout = [int][Math]::Min($OperationTimeoutSeconds, [Math]::Ceiling($remaining))
    }
    return (Invoke-InkTagJLinkProcess -FilePath $JLinkExe -Arguments $arguments `
        -StdoutPath $stdout -StderrPath $stderr -Label "nRF52811 $Operation" -ShowProgress -OperationTimeoutSeconds $attemptTimeout -Verbose:($VerbosePreference -eq 'Continue'))
}

function Test-Connected($Result) {
    return (Test-InkTagNativeReadResult $Result) -and $Result.Text -match "(?i)(Cortex-M4 identified|Found SW-DP)"
}

Write-InkTagTerminalMessage 'Detecting nRF52811 SWD target; please wait.'
$detecting = $true
do {
    if ($Wait -and [Diagnostics.Stopwatch]::GetTimestamp() -ge $deadline) { throw "Detection deadline expired; raw logs: $sessionDir" }
    # A read-only probe must not leave a healthy application halted after the
    # Commander process exits. The backup path halts again around savebin and
    # already resumes explicitly when that operation completes.
    $probeCommands = @("connect", "halt", "mem32 0xE000ED00 1", "mem32 0x10000100 2")
    $probeCommands += @("mem32 0x10000108 3", "mem32 0x10000060 2", "mem32 0x100000A4 2")
    if ($PSBoundParameters.ContainsKey("ReadAddress")) {
        $probeCommands += ("mem32 0x{0:X8} 0x{1:X}" -f $ReadAddress, $ReadWords)
        if ($ReadCount -gt 1) {
            # Resume after the initial attach/halt; subsequent reads run live.
            # The initial halt still interrupts BLE. Never overlap this probe
            # with a BLE timing or hardware-operation qualification.
            $probeCommands += "go"
            for ($readIndex = 1; $readIndex -lt $ReadCount; $readIndex++) {
                $probeCommands += "Sleep $ReadIntervalMs"
                $probeCommands += ("mem32 0x{0:X8} 0x{1:X}" -f $ReadAddress, $ReadWords)
            }
        }
    }
    if (!$PSBoundParameters.ContainsKey("ReadAddress") -or $ReadCount -eq 1) {
        $probeCommands += "go"
    }
    $probe = Invoke-JLink $probeCommands
    if (Test-Connected $probe) { break }
    if (!$Wait -or [Diagnostics.Stopwatch]::GetTimestamp() -ge $deadline -or -not (Test-InkTagTargetAbsent $probe)) {
        throw "nRF52811 target is not readable over J-Link 7.88j; stopped without replay. Raw logs: $sessionDir"
    }
    Write-InkTagTerminalMessage 'Target not detected; explicit RetryDetect will try contact again.' -Level WARNING
    $delayMs = $IntervalSeconds * 1000.0
    if ($deadline -ne [long]::MaxValue) {
        $remainingMs = ($deadline - [Diagnostics.Stopwatch]::GetTimestamp()) * 1000.0 / [double][Diagnostics.Stopwatch]::Frequency
        $delayMs = [Math]::Min($delayMs, [Math]::Max(0, [Math]::Ceiling($remainingMs)))
    }
    if ($delayMs -gt 0) { Start-Sleep -Milliseconds ([int]$delayMs) }
} while ($true)
$detecting = $false
if ($probe.Text -notmatch '(?im)^10000100\s*=\s*00052811\b' -or
    $probe.Text -notmatch '(?im)^10000108\s*=\s*[0-9A-F]{8}\s+00000018\s+000000C0\b') {
    throw "FICR does not identify nRF52811 / 24 KiB RAM / 192 KiB Flash. Evidence: $sessionDir"
}
Set-Content -LiteralPath (Join-Path $sessionDir "probe.log") -Value $probe.Text -Encoding utf8
Write-InkTagTerminalMessage 'Target detected; FICR geometry confirmed.'
Write-InkTagTerminalMessage 'Detected MCU: nRF52811; Flash: 192 KiB; RAM: 24 KiB. PCB identity is not inferred.'
if ($probe.Text -match '(?im)^10000060\s*=\s*([0-9A-F]{8})\s+([0-9A-F]{8})') {
    Write-InkTagTerminalMessage ("Device ID: {0}{1}" -f $Matches[2],$Matches[1])
}
if ($probe.Text -match '(?i)(?:VTref|VTarget)\s*[=:]\s*(\d+(?:\.\d+)?)\s*V') {
    Write-InkTagTerminalMessage ("Target reference: {0} V" -f $Matches[1])
}
if ($Operation -eq "probe") {
    Write-InkTagTerminalMessage 'Probe OK: nRF52811 SWD and FICR are readable.' -Level SUCCESS
    if ($PSBoundParameters.ContainsKey("ReadAddress")) {
        $addressText = "{0:X8}" -f $ReadAddress
        $readLines = @($probe.Text -split "`r?`n" | Where-Object { $_ -match "^$addressText\s*=\s*[0-9A-Fa-f]{8}" })
        if ($readLines.Count -ne $ReadCount) { throw "Requested memory reads incomplete ($($readLines.Count)/$ReadCount); evidence: $sessionDir" }
        $readLines | Write-Output
        Write-InkTagTerminalMessage "Read evidence: $sessionDir"
    }
    exit 0
}

$targetDir = if ($Operation -eq "capture") { Join-Path $sessionDir "configuration" } else { $sessionDir }
if ($Operation -eq 'capture') {
    if (Test-Path -LiteralPath $targetDir) { throw "Backup destination already exists; preserving it: $targetDir" }
    New-Item -ItemType Directory -Path $targetDir | Out-Null
}
$flash = Join-Path $targetDir "aesl22-nrf-flash.bin"
$ficr = Join-Path $targetDir "aesl22-nrf-ficr.bin"
$uicr = Join-Path $targetDir "aesl22-nrf-uicr.bin"
. (Join-Path $PSScriptRoot 'backup-read-plan.ps1')
$readPlan = Get-InkTagNrfBackupReadPlan -TargetDir $targetDir -ConfigOnly:($Operation -eq 'capture')
if ($Operation -eq 'backup') {
    New-Item -ItemType Directory -Path (Join-Path $targetDir 'read-2') | Out-Null
    Write-InkTagTerminalMessage 'Reading internal Flash/FICR/UICR twice; both complete captures will be retained. Please wait.'
} else {
    Write-InkTagTerminalMessage 'Reading configuration identity (4 KiB FICR/UICR); please wait.'
}
$result = Invoke-JLink $readPlan.Commands
Set-Content -LiteralPath (Join-Path $targetDir "backup.log") -Value $result.Text -Encoding utf8
if (!(Test-Connected $result)) { throw "nRF52811 backup lost SWD contact." }
foreach ($expected in $readPlan.Files) {
    if (!(Test-Path -LiteralPath $expected.Path) -or (Get-Item -LiteralPath $expected.Path).Length -ne $expected.Bytes) {
        throw "nRF52811 backup size gate failed: $($expected.Path); captures retained without replay: $targetDir"
    }
}
$info = Join-Path $targetDir "backup-info.txt"
$infoLines = @(
    "board=$ArchiveBoard",
    "ficr_sha256=$((Get-FileHash $ficr -Algorithm SHA256).Hash)",
    "uicr_sha256=$((Get-FileHash $uicr -Algorithm SHA256).Hash)",
    "programmer=jlink-7.88j"
    "jlink_serial=$Snr"
)
if ($Operation -eq "backup") { $infoLines += @("capture_count=2", "flash_size_bytes=196608", "flash_sha256=$((Get-FileHash $flash -Algorithm SHA256).Hash)") }
Set-Content -LiteralPath $info -Encoding ASCII -Value $infoLines
$validationArgs = @($targetDir, "--jlink-serial", $Snr, "--write-manifest")
if ($Operation -eq "capture") { $validationArgs += "--config-only" }
if ($Operation -eq "backup") { $validationArgs += "--require-double-read" }
Invoke-InkTagOfflineCheck $PythonExe (@((Join-Path $PSScriptRoot 'validate-backup.py')) + $validationArgs) (Join-Path $targetDir 'validation.log')
if ($Operation -eq 'backup') {
    $placementArgs = @('--repo', $repoRoot, '--directory', $targetDir, '--archive-root', $OutputDir, '--pcb', $ArchiveBoard)
    if ($BoardNote) { $placementArgs += @('--board-note', $BoardNote) }
    $placement = & $PythonExe (Join-Path $PSScriptRoot 'backup-layout.py') @placementArgs
    if ($LASTEXITCODE -ne 0) { throw "Backup placement failed; source and destination logs retained. No replay: $targetDir" }
    $targetDir = [string](($placement | ConvertFrom-Json).directory)
}
Write-InkTagTerminalMessage "$Operation OK: $targetDir" -Level SUCCESS
if ($PassThru) {
    [pscustomobject]@{ Directory = $targetDir; IdentityPath = (Join-Path $targetDir "expected-identity.json") }
}
} finally { try { Complete-InkTagTerminalProgress } finally { Restore-InkTagTerminalColour $terminalState } }
