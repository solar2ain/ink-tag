param(
    [ValidateSet("probe", "read", "write", "clear-application-marker")]
    [string]$Operation = "probe",
    [string]$Port = "",
    [string]$File = "",
    [uint32]$Address = 0,
    [uint32]$SizeBytes = 0,
    [int]$Baud = 921600,
    [int]$ActivationBaud = 0,
    [int]$ActivationMs = 600,
    [int]$ClockMHz = 0,
    [string]$ExpectedFlashJedec = "",
    [string]$ExpectedReadFile = "",
    [string]$PostWriteReadbackFile = "",
    [string]$FullWritePlan = "",
    [string]$ReadUnitUid = "",
    [switch]$SkipReadbackVerify,
    [int]$VerifyStride = 1,
    [string]$ToolPath = "",
    [string]$Python = "python",
    [switch]$Run,
    [switch]$SinglePassRead,
    [switch]$ReportFlashJedec,
    [switch]$RetryDetect,
    [int]$DetectTimeoutSeconds = 0,
    [int]$DetectIntervalSeconds = 1,
    [switch]$HoldOnTimeout,
    [switch]$LegacyActivation,
    [switch]$TlsrDebug,
    [switch]$ShowCommand,
    [switch]$NoManualReset,
    [ValidateRange(50, 2000)][int]$ResetPulseMs = 500,
    [Alias("h")]
    [switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)]
    [object[]]$RemainingArguments
)

. (Join-Path $PSScriptRoot "..\..\..\..\..\..\tools\powershell-cli.ps1")
$cliResult = Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help `
    -RemainingArguments $RemainingArguments -Summary "Internal TLSR8359 USB-UART SWS transport helper"
if ($null -ne $cliResult) { exit $cliResult }

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "tlsr8359-uart-sws-common.ps1")

if ($Port -eq "") {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "Port is required, for example -Port COM5")
}
if ($Baud -lt 340000) {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "Baud must be >= 340000 for TLSR825xComFlasher.py")
}
if ($ActivationBaud -eq 0) { $ActivationBaud = $Baud }
if ($ActivationBaud -lt 340000) {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "ActivationBaud must be >= 340000 for TLSR825xComFlasher.py")
}
if ($ActivationMs -lt 0 -or $ClockMHz -lt 0) {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "ActivationMs and ClockMHz must be >= 0")
}
if ($ExpectedFlashJedec -ne "" -and $ExpectedFlashJedec -notmatch '^[0-9A-Fa-f]{6}$') {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "ExpectedFlashJedec must be exactly 6 hexadecimal digits")
}
if ($Operation -eq "write" -and $ExpectedFlashJedec -eq "") {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "Write requires a board-qualified -ExpectedFlashJedec")
}
if ($Operation -eq "clear-application-marker" -and $ExpectedFlashJedec -eq "") {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "Clearing the application marker requires a board-qualified -ExpectedFlashJedec")
}
if ($LegacyActivation -and $Operation -ne "probe") {
    throw "LegacyActivation is restricted to read-only probe operations."
}
if ($LegacyActivation -and $ActivationBaud -ne $Baud) {
    throw "LegacyActivation does not support a separate operation baud."
}
if ($HoldOnTimeout -and ($LegacyActivation -or $Operation -eq "write" -or
    $Operation -eq "clear-application-marker")) {
    throw "HoldOnTimeout is available only for adapter-based read/probe rescue attempts."
}
if ($ExpectedReadFile -ne "" -and $Operation -ne "read") {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "ExpectedReadFile is valid only for read operations")
}
if ($SkipReadbackVerify -and !$PostWriteReadbackFile) {
    throw 'SkipReadbackVerify is valid only for an explicit bounded Full operation.'
}
if ($PostWriteReadbackFile) {
    if ($Operation -ne 'write' -or $Address -ne 0 -or $ExpectedReadFile -or $LegacyActivation -or $VerifyStride -ne 1) {
        throw 'PostWriteReadbackFile requires one complete adapter-based zero-address Full write.'
    }
    if (!$FullWritePlan -or !(Test-Path -LiteralPath $FullWritePlan -PathType Leaf)) { throw 'Full requires a current bound sparse write plan.' }
    if (!(Test-Path -LiteralPath $File -PathType Leaf) -or (Get-Item -LiteralPath $File).Length -notin @(524288,1048576)) {
        throw 'Post-write Full requires a bounded 512-KiB image, or independently qualified 1-MiB clear-all.'
    }
    $PostWriteReadbackFile=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PostWriteReadbackFile)
    if (Test-Path -LiteralPath $PostWriteReadbackFile) { throw 'Readback output already exists; no write.' }
    if (!(Test-Path -LiteralPath (Split-Path -Parent $PostWriteReadbackFile) -PathType Container)) { throw 'Readback evidence directory is missing; no write.' }
}
if ($FullWritePlan -and !$PostWriteReadbackFile) { throw 'FullWritePlan requires the same-session Full operation.' }
if ($ReadUnitUid -and ($Operation -ne 'read' -or $ReadUnitUid -notmatch '^[0-9A-Fa-f]{32}$' -or $SinglePassRead)) { throw 'ReadUnitUid requires an exact UID and confirmed read-only operation.' }

$addressLimit = [uint64]0x1000000
if ($Operation -eq "read") {
    if ($File -eq "" -or $SizeBytes -eq 0) {
        exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "Read requires -File and non-zero -SizeBytes")
    }
    if (([uint64]$Address + [uint64]$SizeBytes) -gt $addressLimit) {
        throw "Read range exceeds the backend's 24-bit Flash address space."
    }
    $File = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($File)
    if (Test-Path -LiteralPath $File) { throw "Refusing to overwrite read output: $File" }
    $parent = Split-Path -Parent $File
    if (!(Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    if ($ExpectedReadFile -ne "") {
        if (!(Test-Path -LiteralPath $ExpectedReadFile -PathType Leaf)) {
            throw "Expected readback file not found: $ExpectedReadFile"
        }
        $ExpectedReadFile = (Resolve-Path -LiteralPath $ExpectedReadFile).Path
        if ((Get-Item -LiteralPath $ExpectedReadFile).Length -ne [int64]$SizeBytes) {
            throw "Expected readback file length must equal SizeBytes."
        }
    }
}
if ($Operation -eq "write") {
    if ($File -eq "" -or !(Test-Path -LiteralPath $File -PathType Leaf)) {
        throw "Write input file not found: $File"
    }
    $File = (Resolve-Path -LiteralPath $File).Path
    $fileLength = (Get-Item -LiteralPath $File).Length
    if ($fileLength -le 0 -or ([uint64]$Address + [uint64]$fileLength) -gt $addressLimit) {
        throw "Write file is empty or exceeds the backend's 24-bit Flash address space."
    }
}
if ($VerifyStride -lt 1) {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "VerifyStride must be >= 1")
}
if ($VerifyStride -ne 1 -and ($Operation -ne "read" -or $ExpectedReadFile -eq "")) {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "VerifyStride > 1 requires a read operation with ExpectedReadFile")
}
if ($SinglePassRead -and $Operation -ne "read") {
    throw "SinglePassRead is valid only for read operations."
}
if ($SinglePassRead -and $ExpectedReadFile -ne "") {
    throw "SinglePassRead and ExpectedReadFile are mutually exclusive."
}
if ($SinglePassRead -and $VerifyStride -ne 1) {
    throw "SinglePassRead requires VerifyStride 1."
}
if ($ReportFlashJedec -and $Operation -ne "read") {
    throw "ReportFlashJedec is valid only for read operations."
}
if ($Operation -eq "clear-application-marker") {
    if ($Address -ne 8) {
        throw "The bounded Recovery entry operation may only clear the TLSR application marker at address 0x000008."
    }
    if ($File -eq "" -or !(Test-Path -LiteralPath $File -PathType Leaf)) {
        throw "Clearing the application marker requires a one-byte zero input file."
    }
    $File = (Resolve-Path -LiteralPath $File).Path
    $markerBytes = [IO.File]::ReadAllBytes($File)
    if ($markerBytes.Length -ne 1 -or $markerBytes[0] -ne 0) {
        throw "The bounded Recovery entry input must contain exactly one zero byte."
    }
}

$resolvedTool = Resolve-Tlsr825xComFlasher -RequestedPath $ToolPath
$pythonExe = Resolve-TlsrPython -RequestedPython $Python
$activationAdapter = Join-Path $PSScriptRoot "tlsr825x-early-activate.py"
if (!(Test-Path -LiteralPath $activationAdapter -PathType Leaf)) {
    throw "TLSR825x early-activation adapter not found: $activationAdapter"
}
$args = if ($LegacyActivation) {
    @($resolvedTool, "-p", $Port, "-b", "$ActivationBaud", "-t", "$ActivationMs")
} else {
    @($activationAdapter, $resolvedTool, "-p", $Port, "-b", "$ActivationBaud", "-t", "$ActivationMs")
}
if ($ActivationBaud -ne $Baud) {
    $args += @("--inktag-operation-baud", "$Baud")
}
if ($LegacyActivation -and $ResetPulseMs -ne 500) {
    throw 'ResetPulseMs override requires the maintained early-activation adapter.'
}
if (!$LegacyActivation) { $args += @('--inktag-reset-pulse-ms', "$ResetPulseMs") }
if ($ExpectedFlashJedec -ne "") {
    $args += @("--inktag-flash-jedec", $ExpectedFlashJedec.ToUpperInvariant())
}
if ($ExpectedReadFile -ne "") {
    $args += @("--inktag-verify-file", $ExpectedReadFile)
}
if ($PostWriteReadbackFile) { $args += @('--inktag-post-write-verify-file', $PostWriteReadbackFile) }
if ($FullWritePlan) { $args += @('--inktag-full-write-plan', (Resolve-Path -LiteralPath $FullWritePlan).Path) }
if ($ReadUnitUid) { $args += @('--inktag-read-unit-uid',$ReadUnitUid) }
if ($SkipReadbackVerify) { $args += '--inktag-skip-post-write-readback' }
if ($VerifyStride -ne 1) {
    $args += @("--inktag-verify-stride", "$VerifyStride")
}
if ($HoldOnTimeout) { $args += "--inktag-hold-on-timeout" }
if ($RetryDetect) {
    $args += @("--inktag-retry-detect", "--inktag-detect-timeout", "$DetectTimeoutSeconds", "--inktag-detect-interval", "$DetectIntervalSeconds")
}
if ($ClockMHz -gt 0) { $args += @("-c", "$ClockMHz") }
if ($Run) { $args += "-r" }
if ($TlsrDebug) { $args += "-d" }
switch ($Operation) {
    "probe" { }
    "read" { $args += @("rf", ("0x{0:X}" -f $Address), ("0x{0:X}" -f $SizeBytes), $File) }
    "write" { $args += @("wf", ("0x{0:X}" -f $Address), $File) }
    "clear-application-marker" { $args += @("we", "0x8", $File) }
}
if ($SinglePassRead) { $args += "--inktag-single-pass-read" }
if ($ReportFlashJedec) { $args += "--inktag-report-flash-jedec" }

Write-Host "TLSR8359 UART-SWS transport"
Write-Host ("  Operation:  {0}" -f $Operation)
Write-Host ("  Port:       {0}" -f $Port)
Write-Host ("  Baud:       {0}" -f $Baud)
if ($ActivationBaud -ne $Baud) {
    Write-Host ("  Attach:     {0}" -f $ActivationBaud)
}
Write-Host ("  Activation: {0} ms" -f $ActivationMs)
if ($Operation -ne "probe") { Write-Host ("  Address:    0x{0:X6}" -f $Address) }
if ($SizeBytes -ne 0) { Write-Host ("  Size:       {0} bytes" -f $SizeBytes) }
if ($File -ne "") { Write-Host ("  File:       {0}" -f $File) }
if ($ExpectedFlashJedec -ne "") { Write-Host ("  Flash ID:   {0}" -f $ExpectedFlashJedec.ToUpperInvariant()) }
if ($ExpectedReadFile -ne "") { Write-Host ("  Verify:     {0}" -f $ExpectedReadFile) }
if ($VerifyStride -ne 1) { Write-Host ("  Sampling:   1 of every {0} 256-byte pages" -f $VerifyStride) }
Write-Host ("  Backend:    {0}" -f $resolvedTool)
$activationMode = if ($LegacyActivation) {
    "upstream continuous capture"
} else {
    "early SWS readback"
}
Write-Host ("  Activation: {0}, maximum {1} ms" -f $activationMode, $ActivationMs)
if ($ShowCommand) { Write-Host ("  Command:    {0} {1}" -f $pythonExe, ($args -join " ")) }

if (!$NoManualReset) {
    Write-TlsrResetPrompt -Action ("{0}: activate the exact target." -f $Operation) -ActivationMs $ActivationMs
}
$oldErrorActionPreference = $ErrorActionPreference
$oldPythonUnbuffered = $env:PYTHONUNBUFFERED
try {
    $ErrorActionPreference = "Continue"
    $env:PYTHONUNBUFFERED = "1"
    & $pythonExe @args
    $toolExitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $oldErrorActionPreference
    if ($null -eq $oldPythonUnbuffered) {
        Remove-Item Env:PYTHONUNBUFFERED -ErrorAction SilentlyContinue
    } else {
        $env:PYTHONUNBUFFERED = $oldPythonUnbuffered
    }
}
if ($toolExitCode -ne 0) {
    throw @"
TLSR825xComFlasher.py failed with exit code $toolExitCode.
If the traceback reports SerialTimeoutException during Activate, Flash access did not start. Keep the USB-UART powered and cycle only the target supply; wait for the COM port to recover, then run probe again before any write.
"@
}
