param(
    [ValidateSet("tlsr8359_nebular420r", "tlsr8359_nebular213r", "tlsr8359_nowa154r", "tlsr8359_nowa213r", "hs_nebular420", "hs_nebular213", "hs_nowa154", "hs_nowa213", "HSEL4Q_04_20M_33", "HSEL4Q_02_13M_30", "HS_EL5101_5M_66_05", "HS_EL5102_1M_67_33")]
    [string]$Board,
    [ValidateSet("tlsr825x", "tlsr8359", "hs9118")]
    [string]$Chip = "tlsr825x",
    [string]$DeviceLabel = "",
    [string]$PcbSilkscreen = "",
    [switch]$RetryDetect,
    [int]$DetectTimeoutSeconds = 0,
    [int]$DetectIntervalSeconds = 1,
    [string]$Port = "auto",
    [uint32]$Address = 0,
    [uint32]$SizeBytes = 0,
    [string]$OutputDir = "",
    [int]$Baud = 921600,
    [int]$ActivationBaud = 0,
    [int]$ActivationMs = 20000,
    [int]$ClockMHz = 0,
    [string]$ToolPath = "",
    [string]$Python = "python",
    [switch]$AllowUnqualifiedBaud,
    [switch]$TlsrDebug,
    [switch]$ShowCommand,
    [Alias("h")]
    [switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)]
    [object[]]$RemainingArguments
)

. (Join-Path $PSScriptRoot "..\..\..\..\..\..\tools\powershell-cli.ps1")
$cliResult = Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help `
    -RemainingArguments $RemainingArguments -Summary "Back up a TLSR8359 candidate Flash window twice over USB-UART SWS"
if ($null -ne $cliResult) { exit $cliResult }
# Chip here is the requested marking for this explicit TLSR backend. Neither
# a product name nor a PCB prefix identifies the silicon behind HS9118.
if ($DeviceLabel -match '[\r\n]' -or $Port -match '[\r\n]') {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "DeviceLabel and Port must be single-line values")
}
if ($PcbSilkscreen -and ($PcbSilkscreen -cnotmatch '^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$' -or
    $PcbSilkscreen -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$')) {
    throw "PCB marking must be a safe 1-64 character name, not a path or hardware selector."
}

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "tlsr8359-uart-sws-common.ps1")

if ($Board) {
    $target = Get-Tlsr8359ResearchTarget -Board $Board
    $Board = $target.BoardId
} else {
    if ($Address -ne 0 -or $SizeBytes -ne 0) { throw "Chip-only backup uses a complete JEDEC-derived range, not a caller-selected window." }
    $target = [pscustomobject]@{
        BoardId = ""; DisplayName = "$Chip transport / PCB unassigned"
        QualifiedReadBaud = 460800; DefaultReadSizeBytes = 0
        CapacityEvidence = "known-JEDEC-declared-boundary; exact board/alias qualification separate"
    }
}
$Port = Resolve-TlsrSerialPort -RequestedPort $Port
if ($Baud -eq 0) { $Baud = 921600 }
if ($Baud -notin @($target.QualifiedReadBaud,921600) -and !$AllowUnqualifiedBaud) {
    throw "Board '$Board' has qualified read baud $($target.QualifiedReadBaud). Use -AllowUnqualifiedBaud only for a read-only experiment."
}
if ($Baud -ne $target.QualifiedReadBaud) {
    Write-Host "READ RATE: $Baud baud supported by the reader; not an exact-board-qualified rate. Full independent comparison remains mandatory."
}
$boardEntry = if ($Board) { Find-Tlsr8359BoardEntry -Board $Board } else { $null }
$verifiedBoardSize = 0
$verifiedWindowSize = 0
if ($null -ne $boardEntry -and $null -ne $boardEntry.PSObject.Properties["physicalFlashSizeBytes"]) {
    [uint64]$verifiedBoardSize = $boardEntry.physicalFlashSizeBytes
}
if ($null -ne $boardEntry -and $null -ne $boardEntry.PSObject.Properties["verifiedFlashWindowSizeBytes"]) {
    [uint64]$verifiedWindowSize = $boardEntry.verifiedFlashWindowSizeBytes
}
$observedJedec = ""
if (!$Board) {
    # Reuse the existing hardware-only identifier. No Flash unlock/program or
    # BLE implementation. Require three actual identical IDs, not a guessed PCB.
    $repoRoot = Get-Tlsr8359RepoRoot
    if ($OutputDir -eq "") {
        $OutputDir = Join-Path $repoRoot (".local\backups\{0}-{1}" -f $Chip, (Get-Date -Format "yyyyMMdd-HHmmss-fff"))
    } else { $OutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir) }
    if (Test-Path -LiteralPath $OutputDir) { throw "Chip-only capture requires a new output directory." }
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    $geometryLog = Join-Path $OutputDir "jedec-identification.log"
    $geometryArgs = @((Resolve-Tlsr825xComFlasher -RequestedPath $ToolPath), "--port", $Port,
        "--baud", "$Baud", "--activation-ms", "$ActivationMs", "--clock-mhz", "$ClockMHz", "--samples", "3")
    if ($RetryDetect) { $geometryArgs += @("--retry-detect", "--detect-timeout", "$DetectTimeoutSeconds", "--detect-interval", "$DetectIntervalSeconds") }
    Write-Host "BACKUP STAGE: identifying actual Flash JEDEC; array capture has not started."
    $geometryLines = New-Object 'System.Collections.Generic.List[string]'
    & (Resolve-TlsrPython -RequestedPython $Python) -u `
        (Join-Path $PSScriptRoot "tlsr825x-flash-identify.py") @geometryArgs 2>&1 |
        ForEach-Object { $geometryLines.Add([string]$_); $_ } |
        Tee-Object -FilePath $geometryLog
    $geometryExitCode = $LASTEXITCODE
    if ($geometryExitCode -ne 0) { throw "JEDEC identification failed; no full backup or writer started. See $geometryLog" }
    $geometryText = $geometryLines -join "`n"
    if ($geometryText -notmatch 'Flash JEDEC samples:\s+([0-9A-F]{6})\s+\1\s+\1(?:\r?\n|$)') {
        throw "Three matching actual JEDEC responses required; no backup range inferred."
    }
    $observedJedec = $Matches[1]
    $geometry = @{ "EB6013" = [uint32]0x80000; "EB6014" = [uint32]0x100000; "856013" = [uint32]0x80000 }
    if (!$geometry.ContainsKey($observedJedec)) { throw "Unregistered Flash JEDEC $observedJedec; capacity is not guessed." }
    $SizeBytes = $geometry[$observedJedec]
}
if ($SizeBytes -eq 0) {
    $SizeBytes = if ($verifiedBoardSize -gt $Address) {
        [uint32]($verifiedBoardSize - $Address)
    } elseif ($Address -eq 0 -and $verifiedWindowSize -ne 0) {
        [uint32]$verifiedWindowSize
    } else {
        $target.DefaultReadSizeBytes
    }
}
if ($SizeBytes -lt 2) { throw "SizeBytes must be at least 2 bytes." }
if ([uint64]$Address + [uint64]$SizeBytes -gt 0x1000000) {
    throw "Address + SizeBytes exceeds the backend's 24-bit Flash address space."
}
$isFullBoardRange = $verifiedBoardSize -ne 0 -and $Address -eq 0 -and `
    [uint64]$SizeBytes -eq $verifiedBoardSize
$isNebularUpperHalf = $Board -eq "hs_nebular420" -and `
    $verifiedBoardSize -eq 0x100000 -and $Address -eq 0x80000 -and $SizeBytes -eq 0x80000
$isRegisteredWindow = $verifiedBoardSize -eq 0 -and $verifiedWindowSize -ne 0 -and `
    $Address -eq 0 -and [uint64]$SizeBytes -eq $verifiedWindowSize
if (($verifiedBoardSize -ne 0 -or $verifiedWindowSize -ne 0) -and
    !$isFullBoardRange -and !$isNebularUpperHalf -and !$isRegisteredWindow) {
    throw "Requested range is not a registered backup boundary for board '$Board'."
}

$resolvedTool = Resolve-Tlsr825xComFlasher -RequestedPath $ToolPath
$pythonExe = Resolve-TlsrPython -RequestedPython $Python
$repoRoot = Get-Tlsr8359RepoRoot
if ($OutputDir -eq "") {
    $OutputDir = Join-Path $repoRoot (".local\backups\{0}-{1}" -f $Board, (Get-Date -Format "yyyyMMdd-HHmmss-fff"))
} else {
    $OutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
}
if (Test-Path -LiteralPath $OutputDir) {
    $existingFiles = @(Get-ChildItem -LiteralPath $OutputDir -Force)
    $onlyFreshGeometry = !$Board -and $existingFiles.Count -eq 1 -and $existingFiles[0].Name -eq "jedec-identification.log"
    if ($existingFiles.Count -ne 0 -and !$onlyFreshGeometry) {
        throw "Refusing to overwrite non-empty backup directory: $OutputDir"
    }
} else {
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
}

$backupFile = Join-Path $OutputDir "tlsr8359-flash.bin"
$verifyFile = Join-Path $OutputDir "tlsr8359-flash-verify.bin"
$infoFile = Join-Path $OutputDir "backup-info.txt"
$analysisFile = Join-Path $OutputDir "analysis.txt"
$firstLog = Join-Path $OutputDir "read-1.log"
$secondLog = Join-Path $OutputDir "read-2.log"
$invoke = Join-Path $PSScriptRoot "invoke-tlsr8359-uart-sws.ps1"
$common = @{
    Operation = "read"
    Port = $Port
    Address = $Address
    SizeBytes = $SizeBytes
    Baud = $Baud
    ActivationBaud = $ActivationBaud
    ActivationMs = $ActivationMs
    ClockMHz = $ClockMHz
    ToolPath = $resolvedTool
    Python = $pythonExe
    Run = $true
}
if ($TlsrDebug) { $common["TlsrDebug"] = $true }
if ($ShowCommand) { $common["ShowCommand"] = $true }
if ($RetryDetect) {
    $common["RetryDetect"] = $true
    $common["DetectTimeoutSeconds"] = $DetectTimeoutSeconds
    $common["DetectIntervalSeconds"] = $DetectIntervalSeconds
}
$common["ReportFlashJedec"] = $true
if ($observedJedec) {
    $common["ExpectedFlashJedec"] = $observedJedec
} elseif ($null -ne $boardEntry -and $null -ne $boardEntry.firstFlash.PSObject.Properties["flashJedecId"]) {
    $observedJedec = [string]$boardEntry.firstFlash.flashJedecId
    $common["ExpectedFlashJedec"] = $observedJedec
} else { throw "No admitted actual/registered JEDEC; no array read." }

Write-Host "InkTag TLSR8359 stock-backup capture"
Write-Host ("  Board:       {0}" -f $Board)
Write-Host ("  PCB marking: {0} (user metadata, not hardware qualification)" -f $PcbSilkscreen)
Write-Host ("  Target:      {0}" -f $target.DisplayName)
Write-Host ("  Device:      {0}" -f $(if ($DeviceLabel -eq "") { "unlabeled" } else { $DeviceLabel }))
Write-Host ("  Port:        {0}" -f $Port)
Write-Host ("  Read window: 0x{0:X6} + {1} bytes" -f $Address, $SizeBytes)
Write-Host ("  Output:      {0}" -f $OutputDir)
Write-Host "  Verification: mandatory independent second read and structural analysis"

Write-TlsrResetPrompt -Action "Read 1 of 2: start the primary stock capture." -ActivationMs $ActivationMs
Write-Host "BACKUP READ 1/2: primary stock capture."
& $invoke @common -File $backupFile -SinglePassRead 2>&1 |
    Tee-Object -FilePath $firstLog
if ((Get-Item -LiteralPath $backupFile).Length -ne [int64]$SizeBytes) {
    throw "First backup size mismatch."
}
$firstHash = (Get-FileHash -LiteralPath $backupFile -Algorithm SHA256).Hash.ToUpperInvariant()

Write-TlsrResetPrompt -Action "Read 2 of 2: start the independent verification capture." -ActivationMs $ActivationMs
Write-Host "BACKUP READ 2/2: independent verification against the first capture."
& $invoke @common -File $verifyFile -ExpectedReadFile $backupFile 2>&1 |
    Tee-Object -FilePath $secondLog
if ((Get-Item -LiteralPath $verifyFile).Length -ne [int64]$SizeBytes) {
    throw "Second backup size mismatch."
}
$secondHash = (Get-FileHash -LiteralPath $verifyFile -Algorithm SHA256).Hash.ToUpperInvariant()
if ($firstHash -ne $secondHash) {
    throw "TLSR8359 reads differ. Keep both files and fix wiring, adapter, baud, or activation timing before any write. First=$firstHash second=$secondHash"
}

$flashBytes = [IO.File]::ReadAllBytes($backupFile)
$halvesIdentical = $false
$upperHalfHasData = $false
if (($SizeBytes % 2) -eq 0) {
    $halfSize = [int]($SizeBytes / 2)
    $firstHalf = [byte[]]$flashBytes[0..($halfSize - 1)]
    $secondHalf = [byte[]]$flashBytes[$halfSize..($SizeBytes - 1)]
    $halvesIdentical = [Linq.Enumerable]::SequenceEqual($firstHalf, $secondHalf)
    $upperHalfHasData = [Linq.Enumerable]::Any($secondHalf, [Func[byte, bool]] { param($value) $value -ne 0xFF })
}

$capacityStatus = if (!$Board -and $observedJedec) {
    "known-jedec-boundary-double-read; exact-board-and-alias-qualification-separate"
} elseif ($isFullBoardRange) {
    "physical-flash-verified-board-registry"
} elseif ($isNebularUpperHalf) {
    "registered-upper-half-preservation-range"
} elseif ($Address -eq 0 -and $SizeBytes -eq 0x80000 -and
    !$halvesIdentical -and $upperHalfHasData) {
    "non-mirrored-512k-window-with-upper-data"
} else {
    "caller-specified-read-window-unverified"
}
$analyzer = Join-Path $repoRoot "tools\hardware\analyze-tlsr8359-backup.py"
if (!(Test-Path -LiteralPath $analyzer -PathType Leaf)) {
    throw "TLSR8359 backup analyzer not found: $analyzer"
}
& $pythonExe $analyzer $backupFile --base-address $Address --output $analysisFile | Out-Null
if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $analysisFile -PathType Leaf)) {
    throw "TLSR8359 backup analyzer failed with exit code $LASTEXITCODE"
}
$toolHash = (Get-FileHash -LiteralPath $resolvedTool -Algorithm SHA256).Hash.ToUpperInvariant()
$metadata = @(
    "schema=inktag-tlsr8359-backup-v2",
    "target=TLSR8359",
    "board_id=$Board",
    "pcb_silkscreen=$PcbSilkscreen",
    "requested_chip_family=$Chip",
    "flash_jedec=$observedJedec",
    "target_name=$($target.DisplayName)",
    "preset_capacity_evidence=$($target.CapacityEvidence)",
    ("device_label={0}" -f $(if ($DeviceLabel -eq "") { "unlabeled" } else { $DeviceLabel })),
    "method=USB-UART SWS",
    "port=$Port",
    "baud=$Baud",
    ("exact_board_read_rate_qualified={0}" -f ($Baud -eq $target.QualifiedReadBaud -and $Board -ne '')),
    "activation_ms=$ActivationMs",
    "clock_mhz=$ClockMHz",
    ("start_address=0x{0:X6}" -f $Address),
    "size_bytes=$SizeBytes",
    "capacity_status=$capacityStatus",
    "halves_identical=$($halvesIdentical.ToString().ToLowerInvariant())",
    "upper_half_has_non_ff_data=$($upperHalfHasData.ToString().ToLowerInvariant())",
    "flash_sha256=$firstHash",
    "verify_sha256=$secondHash",
    "verification=matched",
    "primary_log=read-1.log",
    "verification_log=read-2.log",
    "analysis_file=analysis.txt",
    "backend_path=$resolvedTool",
    "backend_sha256=$toolHash",
    ("captured_utc={0}" -f (Get-Date).ToUniversalTime().ToString("o"))
)
Set-Content -LiteralPath $infoFile -Value $metadata -Encoding UTF8

Write-Host "Backup capture OK."
Write-Host ("  SHA256:         {0}" -f $firstHash)
Write-Host ("  Capacity status: {0}" -f $capacityStatus)
Write-Host ("  Halves identical: {0}" -f $halvesIdentical)
Write-Host ("  Upper-half data:  {0}" -f $upperHalfHasData)
Write-Host ("  Analysis:         {0}" -f $analysisFile)
if ($verifiedBoardSize -eq 0) {
    Write-Warning "This proves the registered address window, not the package's exact physical Flash capacity. First flash remains experimental and requires an explicit acknowledgement."
}
