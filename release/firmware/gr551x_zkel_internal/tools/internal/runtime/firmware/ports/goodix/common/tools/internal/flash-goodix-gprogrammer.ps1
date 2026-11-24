param(
    [string]$Firmware = "",

    [string]$Target = "gr5331",

    [string]$Mode = "generate-program",

    [string]$Layout = "default",

    [Alias("Uid")]
    [string]$Snr = "",

    [string]$GProgrammerDir = "",

    [string]$ConsoleExe = "",

    [string]$GeneratedFirmware = "",

    [string]$StockBackup = "",

    [string]$Python = "python",

    [string]$FlashStart = "",

    [int]$FlashSizeKB = 0,

    [int]$ProductType = -1,

    [string]$DownloadAddress = "",

    [string]$Notes = "",

    [switch]$EraseAll,

    [switch]$NoRunImmediately,

    [switch]$GenerateOnly,

    [switch]$Wait,

    [switch]$ShowCommand,

    [int]$IntervalSeconds = 1,

    [int]$TimeoutSeconds = 0,

    [int]$AttemptTimeoutSeconds = 20,

    [int]$OperationTimeoutSeconds = 300,

    [Alias("h")]
    [switch]$Help,

    [Parameter(ValueFromRemainingArguments = $true)]
    [object[]]$RemainingArguments
)

. (Join-Path $PSScriptRoot "..\..\..\..\..\..\tools\powershell-cli.ps1")
. (Join-Path $PSScriptRoot "..\..\..\..\..\..\tools\flash-log.ps1")
$cliResult = Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help -RemainingArguments $RemainingArguments -Summary "Generate and program InkTag Goodix GR5331/GR5515 firmware"
if ($null -ne $cliResult) { exit $cliResult }
if ($Target -notin @("gr5515", "gr5331")) {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "Target must be gr5331 or gr5515; got: $Target")
}
if ($Mode -notin @("generate-program", "program", "download", "eraseall", "device")) {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "Mode must be generate-program, program, download, eraseall, or device; got: $Mode")
}
if ($Layout -notin @("default", "ota", "direct", "stock")) {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "Layout must be default, ota, direct, or stock; got: $Layout")
}

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ($IntervalSeconds -lt 1) {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "IntervalSeconds must be >= 1")
}
if ($TimeoutSeconds -lt 0) {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "TimeoutSeconds must be >= 0")
}
if ($AttemptTimeoutSeconds -lt 1) {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "AttemptTimeoutSeconds must be >= 1")
}
if ($OperationTimeoutSeconds -lt $AttemptTimeoutSeconds) {
    exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message "OperationTimeoutSeconds must be >= AttemptTimeoutSeconds")
}

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path -Path $PSScriptRoot -ChildPath "..\..\..\..\..\..")).Path

$TargetConfig = @{
    gr5331 = @{
        Firmware = Join-Path -Path $RepoRoot -ChildPath ".sdk\goodix\GR533x.SDK\projects\ble\ble_peripheral\ble_app_inktag_ota\GCC\out\InkTag-gr5331_zkc21v-ota.hex"
        RawBin = Join-Path -Path $RepoRoot -ChildPath "_build\goodix\gr5331_zkc21v\InkTag-gr5331_zkc21v-factory.bin"
        FlashStart = "0x00200000"
        DownloadAddress = "0x00202000"
        FlashSizeKB = 512
        ProductType = 2
        Layout = "ota"
        Notes = "GR5331/ZKC21V board-qualified OTA Swap full-flash image."
    }
    gr5515 = @{
        Firmware = Join-Path -Path $RepoRoot -ChildPath ".sdk\goodix\GR551x.SDK\projects\ble\ble_peripheral\ble_app_inktag_ota\GCC\out\InkTag-gr551x_zkc42v-ota.hex"
        RawBin = Join-Path -Path $RepoRoot -ChildPath "_build\goodix\gr551x_zkc42v\InkTag-gr5515_zkc42vm-factory.bin"
        FlashStart = "0x01000000"
        DownloadAddress = "0x01000000"
        FlashSizeKB = 1024
        ProductType = 0
        Layout = "ota"
        Notes = "GR5515/ZKC42VM board-qualified OTA Swap full-flash image."
    }
}

$Config = $TargetConfig[$Target]
if ($Layout -eq "default") {
    $Layout = $Config.Layout
}
if ($Notes -eq "") {
    $Notes = $Config.Notes
}

if ($GProgrammerDir -eq "") {
    $GProgrammerDir = Join-Path -Path $RepoRoot -ChildPath ".tools\gprogrammer\_._"
}
if ($ConsoleExe -eq "") {
    $ConsoleExe = Join-Path -Path $GProgrammerDir -ChildPath "GR5xxx_console.exe"
}
if ($Firmware -eq "") {
    if ($Mode -eq "download") {
        $Firmware = $Config.RawBin
    } else {
        $Firmware = $Config.Firmware
    }
}
if ($FlashStart -eq "") {
    $FlashStart = $Config.FlashStart
}
if ($FlashSizeKB -eq 0) {
    $FlashSizeKB = $Config.FlashSizeKB
}
if ($ProductType -lt 0) {
    $ProductType = $Config.ProductType
}
if ($DownloadAddress -eq "") {
    $DownloadAddress = if (($Target -eq "gr5331") -and ($Layout -eq "stock")) {
        "0x00208000"
    } else {
        $Config.DownloadAddress
    }
}

if (!(Test-Path -LiteralPath $GProgrammerDir -PathType Container)) {
    throw "GProgrammer directory not found: $GProgrammerDir"
}
if (!(Test-Path -LiteralPath $ConsoleExe -PathType Leaf)) {
    throw "GR5xxx_console.exe not found: $ConsoleExe"
}

$ConsolePath = (Resolve-Path -LiteralPath $ConsoleExe).Path
$GProgrammerRoot = (Resolve-Path -LiteralPath $GProgrammerDir).Path

# GProgrammer uses -1 internally when no probe serial number is supplied.
# That auto-selection has proved unreliable with the older J-Link DLL bundled
# by GProgrammer.  Resolve the serial number when exactly one probe is present
# so every erase/program/reset invocation addresses the same emulator.
if ($Snr -eq "") {
    $DeviceOutput = @()
    Push-Location -LiteralPath $GProgrammerRoot
    try {
        $DeviceOutput = & $ConsolePath device 1 2>&1
    } finally {
        Pop-Location
    }
    $DeviceText = (@($DeviceOutput) | ForEach-Object { $_.ToString() }) -join "`n"
    $DetectedSnrs = @([regex]::Matches($DeviceText, '(?m)^\s*\d+\s+\S+\s+(\d{6,})\s*$') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
    if ($DetectedSnrs.Count -eq 1) {
        $Snr = $DetectedSnrs[0]
    }
}

$Timestamp = Get-Date -Format "yyyyMMdd-HHmmss-fff"
$RunId = [System.Guid]::NewGuid().ToString("N").Substring(0, 8)
$LogDir = Join-Path -Path ([System.IO.Path]::GetTempPath()) -ChildPath "inktag-$Target-gprogrammer-$Timestamp-$RunId"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$LogFile = Join-Path -Path $LogDir -ChildPath "$Target-gprogrammer.log"
$PreparedGeneratedPath = ""
$PreparedDownloadPath = ""
$EraseCompleted = $false

function Format-InkTagPath {
    param([string]$PathValue)

    if ($PathValue -eq "") {
        return ""
    }

    $FullPath = $PathValue
    try {
        if (Test-Path -LiteralPath $PathValue) {
            $FullPath = (Resolve-Path -LiteralPath $PathValue).Path
        }
    } catch {
        $FullPath = $PathValue
    }

    if ($FullPath.StartsWith($RepoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        $Relative = $FullPath.Substring($RepoRoot.Length).TrimStart("\", "/")
        return ".\$Relative"
    }

    return $FullPath
}

function Write-InkTagField {
    param(
        [string]$Name,
        [string]$Value
    )

    Write-Host ("  {0,-12} {1}" -f ($Name + ":"), $Value)
}

function Resolve-InkTagPath {
    param([string]$PathValue)

    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return (Resolve-Path -LiteralPath $PathValue).Path
    }

    return (Resolve-Path -LiteralPath (Join-Path -Path (Get-Location).Path -ChildPath $PathValue)).Path
}

function Test-GoodixStockBackup {
    param(
        [string]$PathValue,
        [string]$TargetName
    )

    try {
        $Item = Get-Item -LiteralPath $PathValue -ErrorAction Stop
        $ValidLength = if ($TargetName -eq "gr5515") {
            $Item.Length -eq 0x00100000
        } else {
            ($Item.Length -eq 0x00080000) -or ($Item.Length -eq 0x00100000)
        }
        if (-not $ValidLength) {
            return $false
        }
        $Stream = [System.IO.File]::OpenRead($Item.FullName)
        try {
            $ExpectedRun = if ($TargetName -eq "gr5515") { @(0x00, 0x30, 0x00, 0x01) } else { @(0x00, 0x30, 0x20, 0x00) }
            $Header = New-Object byte[] 16
            [void]$Stream.Read($Header, 0, $Header.Length)
            return ($Header[8] -eq $ExpectedRun[0] -and $Header[9] -eq $ExpectedRun[1] -and $Header[10] -eq $ExpectedRun[2] -and $Header[11] -eq $ExpectedRun[3] -and
                    $Header[12] -eq $ExpectedRun[0] -and $Header[13] -eq $ExpectedRun[1] -and $Header[14] -eq $ExpectedRun[2] -and $Header[15] -eq $ExpectedRun[3])
        } finally {
            $Stream.Close()
        }
    } catch {
        return $false
    }
}

function Find-GoodixStockBackup {
    param([string]$TargetName)

    $BackupRoot = Join-Path -Path $RepoRoot -ChildPath ".local\backups"
    if (!(Test-Path -LiteralPath $BackupRoot -PathType Container)) {
        return ""
    }

    $Candidates = Get-ChildItem -LiteralPath $BackupRoot -Recurse -File -Filter "$TargetName-xip-flash.bin" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending
    foreach ($Candidate in $Candidates) {
        if (Test-GoodixStockBackup -PathValue $Candidate.FullName -TargetName $TargetName) {
            return $Candidate.FullName
        }
    }
    return ""
}

function New-GProgrammerArgs {
    param([string[]]$CommandArgs)

    if ($Snr -ne "") {
        return $CommandArgs + @($Snr)
    }

    return $CommandArgs
}

function Invoke-GProgrammer {
    param(
        [string]$Label,
        [string[]]$CommandArgs
    )

    $Display = "$ConsolePath " + (($CommandArgs | ForEach-Object {
        if ($_ -match "\s") { '"' + $_ + '"' } else { $_ }
    }) -join " ")

    Add-Content -LiteralPath $LogFile -Encoding UTF8 -Value ""
    Add-Content -LiteralPath $LogFile -Encoding UTF8 -Value "[$(Get-Date -Format o)] $Label"
    Add-Content -LiteralPath $LogFile -Encoding UTF8 -Value $Display

    Write-Host ""
    Write-Host "== $Label =="
    if ($CommandArgs.Count -gt 0) {
        Write-InkTagField -Name "Command" -Value "GR5xxx_console.exe $($CommandArgs[0])"
    }
    if ($ShowCommand) {
        Write-InkTagField -Name "Full" -Value $Display
    }

    $InvocationId = [System.Guid]::NewGuid().ToString("N")
    $StdoutFile = Join-Path -Path $LogDir -ChildPath "$InvocationId.stdout.tmp"
    $StderrFile = Join-Path -Path $LogDir -ChildPath "$InvocationId.stderr.tmp"
    $NativeArgs = @($CommandArgs | ForEach-Object {
        $ArgText = [string]$_
        if ($ArgText -match '[\s"]') {
            '"' + ($ArgText -replace '"', '\"') + '"'
        } else {
            $ArgText
        }
    })

    $Process = $null
    $TimedOut = $false
    $HasOutput = $false
    $CommandName = if ($CommandArgs.Count -gt 0) { $CommandArgs[0].ToLowerInvariant() } else { "" }
    # eraseall/program/download/reset can remain silent while the probe is
    # actively operating. Treat these as operations immediately; the former
    # 20-second pre-operation watchdog killed valid flashes halfway through.
    $OperationStarted = $CommandName -in @("eraseall", "program", "download", "reset")
    $TerminalFailureSeen = $false
    $HeartbeatShown = $false
    $StartedAt = Get-Date
    $LastHeartbeatAt = $StartedAt
    $LastRenderedPercent = -1
    $ConsoleOffsets = @{}
    try {
        $Process = Start-Process -FilePath $ConsolePath -ArgumentList $NativeArgs -WorkingDirectory $GProgrammerRoot `
            -WindowStyle Hidden -PassThru -RedirectStandardOutput $StdoutFile -RedirectStandardError $StderrFile
        # Windows PowerShell 5 can lose ExitCode for an asynchronously started
        # process unless its native handle is opened before it exits.
        [void]$Process.Handle

        while (!$Process.HasExited) {
            Start-Sleep -Milliseconds 200
            Show-InkTagLogTail -Paths @($StdoutFile, $StderrFile) -Offsets $ConsoleOffsets
            $CurrentOutput = ""
            if (Test-Path -LiteralPath $StdoutFile) {
                $CurrentOutput += Get-Content -LiteralPath $StdoutFile -Raw -ErrorAction SilentlyContinue
            }
            if (Test-Path -LiteralPath $StderrFile) {
                $CurrentOutput += Get-Content -LiteralPath $StderrFile -Raw -ErrorAction SilentlyContinue
            }
            $StdoutLength = if (Test-Path -LiteralPath $StdoutFile) { (Get-Item -LiteralPath $StdoutFile).Length } else { 0 }
            $StderrLength = if (Test-Path -LiteralPath $StderrFile) { (Get-Item -LiteralPath $StderrFile).Length } else { 0 }
            if (($StdoutLength + $StderrLength) -gt 0) {
                $HasOutput = $true
            }
            if ($CurrentOutput -match '(?i)(?:Programming|Downloading)\s+\d+%|\b(generate|eraseall|program|download|reset)\s+success\b') {
                $OperationStarted = $true
            }
            $ProgressMatches = [regex]::Matches($CurrentOutput, '(?i)(?:Programming|Downloading)\s+(\d+)%')
            if ($ProgressMatches.Count -gt 0) {
                $Percent = [int]$ProgressMatches[$ProgressMatches.Count - 1].Groups[1].Value
                if ($Percent -ne $LastRenderedPercent) {
                    Write-Progress -Activity $Label -Status "$Percent%" -PercentComplete $Percent
                    $LastRenderedPercent = $Percent
                }
            }
            if ($CurrentOutput -match '(?i)failed to connect|reset:\s*failed|Can not find JLink|conflicts\s+with\s+the\s+(?:IMAGE|BOOT_INFO)\s+area') {
                $TerminalFailureSeen = $true
                Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
                break
            }

            $Elapsed = ((Get-Date) - $StartedAt).TotalSeconds
            $Limit = if ($OperationStarted) { $OperationTimeoutSeconds } else { $AttemptTimeoutSeconds }
            if ($Elapsed -ge $Limit) {
                $TimedOut = $true
                Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
                break
            }

            if (!$OperationStarted -and ((Get-Date) - $LastHeartbeatAt).TotalSeconds -ge 1) {
                Write-Host -NoNewline "."
                $HeartbeatShown = $true
                $LastHeartbeatAt = Get-Date
            }
            $Process.Refresh()
        }
        if (!$Process.HasExited) {
            try { [void]$Process.WaitForExit(2000) } catch { }
        }
    } finally {
        if ($null -ne $Process -and !$Process.HasExited) {
            Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
            try { [void]$Process.WaitForExit(1000) } catch { }
        }
        if ($HeartbeatShown) { Write-Host "" }
        if ($LastRenderedPercent -ge 0) { Write-Progress -Activity $Label -Completed }
    }

    $Output = @()
    Show-InkTagLogTail -Paths @($StdoutFile, $StderrFile) -Offsets $ConsoleOffsets
    if (Test-Path -LiteralPath $StdoutFile) {
        $Output += Get-Content -LiteralPath $StdoutFile -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $StderrFile) {
        $Output += Get-Content -LiteralPath $StderrFile -ErrorAction SilentlyContinue
    }
    if ($TimedOut) {
        $Limit = if ($OperationStarted) { $OperationTimeoutSeconds } else { $AttemptTimeoutSeconds }
        $Output += "INKTAG_ATTEMPT_TIMEOUT after $Limit second(s)"
    }
    if ($TerminalFailureSeen) {
        $Output += "INKTAG_TERMINAL_CONNECT_FAILURE"
    }
    $ProcessStillRunning = $null -ne $Process -and !$Process.HasExited
    $ExitCode = if ($TimedOut -or $TerminalFailureSeen -or $null -eq $Process -or $ProcessStillRunning) { -1 } else { $Process.ExitCode }

    $OutputText = @()
    foreach ($Line in @($Output)) {
        # Windows PowerShell wraps native stderr lines in ErrorRecord objects.
        # Converting those objects directly can lose the actual GProgrammer
        # message, which previously made IMAGE conflicts look retryable.
        if ($Line -is [System.Management.Automation.ErrorRecord]) {
            $Text = $Line.Exception.Message
            if ([string]::IsNullOrWhiteSpace($Text)) { $Text = $Line.ToString() }
        } else {
            $Text = [string]$Line
        }
        $OutputText += $Text
        Add-Content -LiteralPath $LogFile -Encoding UTF8 -Value $Text
    }
    Remove-Item -LiteralPath $StdoutFile, $StderrFile -Force -ErrorAction SilentlyContinue

    $Joined = $OutputText -join "`n"
    $LooksFailed = ($Joined -match "(?i)\bfailed\b|\bfail\b|\berror\b|\bexception\b|NOT_SUPPORTED")
    $ImageAreaConflict = ($Joined -match "(?i)conflicts\s+with\s+the\s+(?:IMAGE|BOOT_INFO)\s+area")
    $NoJLinkDevice = ($Joined -match "(?i)Can\s+not\s+find\s+JLink\s+device")

    return [pscustomobject]@{
        ExitCode = $ExitCode
        Output = $Joined
        LooksFailed = $LooksFailed
        ImageAreaConflict = $ImageAreaConflict
        NoJLinkDevice = $NoJLinkDevice
        TimedOut = $TimedOut
        ProcessStillRunning = $ProcessStillRunning
    }
}

function Test-GProgrammerSuccess {
    param([pscustomobject]$Result)

    return ($Result.ExitCode -eq 0) -and (-not $Result.LooksFailed)
}

function Invoke-StepChecked {
    param(
        [string]$Label,
        [string[]]$CommandArgs
    )

    $Result = Invoke-GProgrammer -Label $Label -CommandArgs $CommandArgs
    if ($Result.ImageAreaConflict) {
        throw "NON_RETRYABLE: $Label reached the chip, but GProgrammer refused an IMAGE/BOOT_INFO overlap. This is not a contact/reset problem. Check the qualified image/erase/conflict-override arguments. Check log: $LogFile"
    }
    if ($Result.NoJLinkDevice) {
        throw "NO_JLINK: GProgrammer cannot find the J-Link probe. Check USB/J-Link driver, close J-Link Commander/J-Flash if open, or pass -Snr when multiple probes are attached. Check log: $LogFile"
    }
    if ($Result.ProcessStillRunning) {
        throw "NON_RETRYABLE: $Label timed out and the GProgrammer process could not be terminated. Replug J-Link before retrying. Check log: $LogFile"
    }
    if ($Result.TimedOut) {
        throw "$Label did not reach or finish with the Goodix target before the per-attempt timeout."
    }
    if (-not (Test-GProgrammerSuccess -Result $Result)) {
        throw "$Label failed. Check log: $LogFile"
    }
    return $Result
}

function Invoke-EraseAllOnce {
    if ($EraseAll) {
        if (-not $script:EraseCompleted) {
            Invoke-StepChecked -Label "Erase all Goodix flash" -CommandArgs (New-GProgrammerArgs -CommandArgs @("eraseall", "$ProductType")) | Out-Null
            $script:EraseCompleted = $true
        }
    }
}

function Get-TailTrimmedDownloadPath {
    param([string]$ImagePath)

    if (-not $EraseAll) {
        return $ImagePath
    }
    if ($script:PreparedDownloadPath -ne "") {
        return $script:PreparedDownloadPath
    }

    $Bytes = [System.IO.File]::ReadAllBytes($ImagePath)
    $LastUsed = $Bytes.Length - 1
    while (($LastUsed -ge 0) -and ($Bytes[$LastUsed] -eq 0xFF)) {
        $LastUsed--
    }
    if ($LastUsed -lt 0) {
        throw "Refusing to download an entirely erased image: $ImagePath"
    }

    $ErasePageBytes = 4096
    $ProgramLength = [int](([math]::Floor($LastUsed / $ErasePageBytes) + 1) * $ErasePageBytes)
    if ($ProgramLength -ge $Bytes.Length) {
        $script:PreparedDownloadPath = $ImagePath
        return $ImagePath
    }

    $TrimmedPath = Join-Path -Path $LogDir -ChildPath (([System.IO.Path]::GetFileNameWithoutExtension($ImagePath)) + ".program.bin")
    $Trimmed = New-Object byte[] $ProgramLength
    [System.Array]::Copy($Bytes, 0, $Trimmed, 0, $ProgramLength)
    [System.IO.File]::WriteAllBytes($TrimmedPath, $Trimmed)
    $script:PreparedDownloadPath = $TrimmedPath

    Write-Host ""
    Write-Host "== Prepare erased-tail-optimized download =="
    Write-InkTagField -Name "Source" -Value (Format-InkTagPath -PathValue $ImagePath)
    Write-InkTagField -Name "Program" -Value (Format-InkTagPath -PathValue $TrimmedPath)
    Write-InkTagField -Name "Bytes" -Value ("{0} of {1}; the erased 0xFF tail is already covered by eraseall" -f $ProgramLength, $Bytes.Length)
    Add-Content -LiteralPath $LogFile -Encoding UTF8 -Value ("Trimmed erased tail for download: {0} -> {1} bytes ({2})" -f $Bytes.Length, $ProgramLength, $TrimmedPath)
    return $TrimmedPath
}

function Invoke-ProgramOnce {
    if ($Mode -eq "device") {
        Invoke-StepChecked -Label "List connected J-Link emulators" -CommandArgs @("device", "1") | Out-Null
        return $true
    }

    if ($Mode -eq "eraseall") {
        Invoke-StepChecked -Label "Erase all Goodix flash" -CommandArgs (New-GProgrammerArgs -CommandArgs @("eraseall", "$ProductType")) | Out-Null
        return $true
    }

    if (!(Test-Path -LiteralPath $Firmware -PathType Leaf)) {
        throw "Firmware not found: $Firmware"
    }
    $FirmwarePath = Resolve-InkTagPath -PathValue $Firmware

    if ((($Target -eq "gr5515") -or ($Target -eq "gr5331")) -and ($Mode -eq "generate-program") -and ($Layout -eq "stock")) {
        $AppBin = $Config.RawBin
        if (!(Test-Path -LiteralPath $AppBin -PathType Leaf)) {
            throw "$Target app bin not found: $AppBin"
        }

        if ($PreparedGeneratedPath -eq "") {
            if ($GeneratedFirmware -eq "") {
                $FirmwareItem = Get-Item -LiteralPath $FirmwarePath
                $OutputDir = Join-Path -Path $FirmwareItem.DirectoryName -ChildPath "gprogrammer"
                New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
                $GeneratedFirmware = Join-Path -Path $OutputDir -ChildPath ($FirmwareItem.BaseName + ".xip.bin")
            } else {
                $GeneratedParent = Split-Path -Parent $GeneratedFirmware
                if ($GeneratedParent -ne "") {
                    New-Item -ItemType Directory -Force -Path $GeneratedParent | Out-Null
                }
            }

            $GeneratedPath = if ([System.IO.Path]::IsPathRooted($GeneratedFirmware)) {
                $GeneratedFirmware
            } else {
                Join-Path -Path (Get-Location).Path -ChildPath $GeneratedFirmware
            }

            $StockPath = $StockBackup
            if ($StockPath -eq "") {
                $StockPath = Find-GoodixStockBackup -TargetName $Target
            } else {
                $StockPath = Resolve-InkTagPath -PathValue $StockPath
            }
            if ($StockPath -eq "" -or !(Test-GoodixStockBackup -PathValue $StockPath -TargetName $Target)) {
                throw "$Target flashing needs a stock XIP backup with second_boot. Run savebin-$Target.ps1 first, or pass -StockBackup <$Target-xip-flash.bin>."
            }

            $Maker = Join-Path -Path $RepoRoot -ChildPath "tools\sdk\make-goodix-stock-xip-image.py"
            if (!(Test-Path -LiteralPath $Maker -PathType Leaf)) {
                throw "Goodix stock-XIP image helper not found: $Maker"
            }

            Write-Host ""
            Write-Host "== Build $Target stock-boot XIP image =="
            Write-InkTagField -Name "Stock" -Value (Format-InkTagPath -PathValue $StockPath)
            Write-InkTagField -Name "App bin" -Value (Format-InkTagPath -PathValue $AppBin)
            Write-InkTagField -Name "Output" -Value (Format-InkTagPath -PathValue $GeneratedPath)
            Add-Content -LiteralPath $LogFile -Encoding UTF8 -Value ""
            Add-Content -LiteralPath $LogFile -Encoding UTF8 -Value "[$(Get-Date -Format o)] Build $Target stock-boot XIP image"

            $PyOutput = & $Python $Maker --target $Target --stock-backup $StockPath --app-bin $AppBin --out $GeneratedPath 2>&1
            $PyExitCode = $LASTEXITCODE
            foreach ($Line in @($PyOutput)) {
                Add-Content -LiteralPath $LogFile -Encoding UTF8 -Value $Line
                Write-Host $Line
            }
            if ($PyExitCode -ne 0) {
                throw "Building $Target XIP image failed. Check log: $LogFile"
            }
            if (!(Test-Path -LiteralPath $GeneratedPath -PathType Leaf)) {
                throw "$Target XIP image was not created: $GeneratedPath"
            }
            $ExpectedImageLength = $FlashSizeKB * 1024
            if ((Get-Item -LiteralPath $GeneratedPath).Length -ne $ExpectedImageLength) {
                throw "$Target XIP image must be $ExpectedImageLength bytes: $GeneratedPath"
            }
            $script:PreparedGeneratedPath = $GeneratedPath
        } else {
            $GeneratedPath = $PreparedGeneratedPath
            Write-Host ""
            Write-Host "== Reuse $Target stock-boot XIP image =="
            Write-InkTagField -Name "Image" -Value (Format-InkTagPath -PathValue $GeneratedPath)
        }

        if ($GenerateOnly) {
            Write-Host ""
            Write-Host "Generate OK"
            Write-InkTagField -Name "Image" -Value (Format-InkTagPath -PathValue $GeneratedPath)
            return $true
        }

        Invoke-EraseAllOnce

        Invoke-StepChecked -Label "Download $Target stock-boot XIP image" -CommandArgs (New-GProgrammerArgs -CommandArgs @("download", $GeneratedPath, $FlashStart, "y", $FlashStart, "$FlashSizeKB", "$ProductType")) | Out-Null
        if (-not $NoRunImmediately) {
            Invoke-StepChecked -Label "Reset Goodix target" -CommandArgs (New-GProgrammerArgs -CommandArgs @("reset", "$ProductType")) | Out-Null
        }
        return $true
    }

    if ($Mode -eq "download") {
        if ([System.IO.Path]::GetExtension($FirmwarePath).ToLowerInvariant() -ne ".bin") {
            throw "Mode download writes a raw image and requires a .bin firmware: $FirmwarePath"
        }
        Invoke-EraseAllOnce
        $DownloadPath = Get-TailTrimmedDownloadPath -ImagePath $FirmwarePath
        # download's y/n is the IMAGE-conflict override, NOT program's run flag.
        # NoRunImmediately suppresses the separate reset below. Do not conflate
        # the vendor command grammars when preparing a physical factory step.
        Invoke-StepChecked -Label "Download raw image to Goodix flash" -CommandArgs (New-GProgrammerArgs -CommandArgs @("download", $DownloadPath, $DownloadAddress, "y", $FlashStart, "$FlashSizeKB", "$ProductType")) | Out-Null
        if (-not $NoRunImmediately) {
            Invoke-StepChecked -Label "Reset Goodix target" -CommandArgs (New-GProgrammerArgs -CommandArgs @("reset", "$ProductType")) | Out-Null
        }
        return $true
    }

    if ($Mode -eq "program") {
        Invoke-EraseAllOnce
        $RunFlag = if ($NoRunImmediately) { "n" } else { "y" }
        Invoke-StepChecked -Label "Program Goodix firmware package" -CommandArgs (New-GProgrammerArgs -CommandArgs @("program", $FirmwarePath, $RunFlag, $FlashStart, "$FlashSizeKB", "$ProductType")) | Out-Null
        return $true
    }

    if ($Mode -ne "generate-program") {
        throw "Unsupported mode: $Mode"
    }

    if ($PreparedGeneratedPath -eq "") {
        if ($GeneratedFirmware -eq "") {
            $FirmwareItem = Get-Item -LiteralPath $FirmwarePath
            $OutputDir = Join-Path -Path $FirmwareItem.DirectoryName -ChildPath "gprogrammer"
            New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
            $GeneratedSuffix = ".gprogrammer.bin"
            $GeneratedFirmware = Join-Path -Path $OutputDir -ChildPath ($FirmwareItem.BaseName + $GeneratedSuffix)
        } else {
            $GeneratedParent = Split-Path -Parent $GeneratedFirmware
            if ($GeneratedParent -ne "") {
                New-Item -ItemType Directory -Force -Path $GeneratedParent | Out-Null
            }
        }

        $GeneratedPath = if ([System.IO.Path]::IsPathRooted($GeneratedFirmware)) {
            $GeneratedFirmware
        } else {
            Join-Path -Path (Get-Location).Path -ChildPath $GeneratedFirmware
        }

        $FirmwareExt = [System.IO.Path]::GetExtension($FirmwarePath).ToLowerInvariant()
        if ($FirmwareExt -eq ".bin") {
            Write-Warning "generate-program with a raw .bin has no address records. Prefer the InkTag .hex, or use -Mode download for a raw app bin."
        }

        Invoke-StepChecked -Label "Generate GProgrammer firmware package" -CommandArgs @("generate", $FirmwarePath, $GeneratedPath, $FlashStart, "$FlashSizeKB", "$ProductType") | Out-Null
        if (!(Test-Path -LiteralPath $GeneratedPath -PathType Leaf)) {
            throw "GProgrammer did not create generated firmware: $GeneratedPath"
        }
        if ((Get-Item -LiteralPath $GeneratedPath).Length -le 0) {
            throw "Generated firmware is empty: $GeneratedPath"
        }
        $script:PreparedGeneratedPath = $GeneratedPath
    } else {
        $GeneratedPath = $PreparedGeneratedPath
        Write-Host ""
        Write-Host "== Reuse GProgrammer firmware package =="
        Write-InkTagField -Name "Package" -Value (Format-InkTagPath -PathValue $GeneratedPath)
    }

    if ($GenerateOnly) {
        Write-Host ""
        Write-Host "Generate OK"
        Write-InkTagField -Name "Package" -Value (Format-InkTagPath -PathValue $GeneratedPath)
        return $true
    }

    Invoke-EraseAllOnce

    $RunFlag = if ($NoRunImmediately) { "n" } else { "y" }
    Invoke-StepChecked -Label "Program generated Goodix firmware package" -CommandArgs (New-GProgrammerArgs -CommandArgs @("program", $GeneratedPath, $RunFlag, $FlashStart, "$FlashSizeKB", "$ProductType")) | Out-Null
    if (($Target -eq "gr5515") -and (-not $NoRunImmediately)) {
        Invoke-StepChecked -Label "Reset Goodix target" -CommandArgs (New-GProgrammerArgs -CommandArgs @("reset", "$ProductType")) | Out-Null
    }
    return $true
}

Write-Host ""
Write-Host "InkTag Goodix flash"
Write-Host "--------------------"
Write-InkTagField -Name "Target" -Value $Target
Write-InkTagField -Name "Mode" -Value $Mode
Write-InkTagField -Name "Layout" -Value $Layout
Write-InkTagField -Name "Firmware" -Value (Format-InkTagPath -PathValue $Firmware)
Write-InkTagField -Name "GProgrammer" -Value (Format-InkTagPath -PathValue $GProgrammerRoot)
Write-InkTagField -Name "J-Link S/N" -Value $(if ($Snr -ne "") { $Snr } else { "auto (-1); pass -Snr when detection is unreliable" })
Write-InkTagField -Name "SWD speed" -Value "managed internally by GProgrammer (its J-Link CLI has no frequency option)"
Write-InkTagField -Name "Flash base" -Value $FlashStart
Write-InkTagField -Name "Flash size" -Value "$FlashSizeKB KB"
Write-InkTagField -Name "Product" -Value "$ProductType"
Write-InkTagField -Name "Erase all" -Value "$EraseAll"
Write-InkTagField -Name "Run after" -Value "$(!$NoRunImmediately)"
Write-InkTagField -Name "Wait" -Value "$Wait, interval=${IntervalSeconds}s, timeout=${TimeoutSeconds}s"
Write-InkTagField -Name "Log" -Value $LogFile
Write-InkTagField -Name "Notes" -Value $Notes

$Started = Get-Date
while ($true) {
    try {
        if (Invoke-ProgramOnce) {
            $Duration = ((Get-Date) - $Started).TotalSeconds
            Write-Host ""
            Write-Host ("GProgrammer OK ({0:N1}s)" -f $Duration)
            Write-InkTagField -Name "Log" -Value $LogFile
            break
        }
    } catch {
        $Message = $_.Exception.Message
        $LogHasImageConflict = $false
        if (Test-Path -LiteralPath $LogFile -PathType Leaf) {
            $LogHasImageConflict = [bool](Select-String -LiteralPath $LogFile -Pattern "conflicts\s+with\s+the\s+(?:IMAGE|BOOT_INFO)\s+area" -Quiet)
        }
        $LogHasNoJLink = $false
        if (Test-Path -LiteralPath $LogFile -PathType Leaf) {
            $LogHasNoJLink = [bool](Select-String -LiteralPath $LogFile -Pattern "Can not find JLink device" -Quiet)
        }

        if (($Message -like "NON_RETRYABLE:*") -or $LogHasImageConflict) {
            if ($LogHasImageConflict -and ($Message -notlike "NON_RETRYABLE:*")) {
                throw "GProgrammer reached the chip, but refused an IMAGE/BOOT_INFO overlap. This is not a contact/reset problem. Check qualified image/erase/conflict-override arguments. Check log: $LogFile"
            }
            throw ($Message -replace "^NON_RETRYABLE:\s*", "")
        }

        if (-not $Wait) {
            throw
        }

        Write-Host ""
        if (($Message -like "NO_JLINK:*") -or $LogHasNoJLink) {
            Write-Warning ($Message -replace "^NO_JLINK:\s*", "")
            Write-Host "Will retry when the J-Link probe is visible. Replug J-Link, check driver, close other SEGGER tools, or pass -Snr for a specific probe."
        } else {
            Write-Warning "$Message"
            Write-Host "Will retry when the Goodix target is contactable. Touch VCC/GND/SWDIO/SWCLK and pulse RST if needed..."
        }
    }

    if ($TimeoutSeconds -gt 0) {
        $Elapsed = ((Get-Date) - $Started).TotalSeconds
        if ($Elapsed -ge $TimeoutSeconds) {
            throw "Timed out after $TimeoutSeconds seconds. Check log: $LogFile"
        }
    }

    Start-Sleep -Seconds $IntervalSeconds
}
