[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$')]
    [string]$Board,
    [string]$BoardNote = '',
    [switch]$RetryDetect,
    [string]$OutputDir = "",
    [string]$JFlashExe = "",
    [string]$JLinkExe = "",
    [string]$JLinkDevicesXml = "",
    [string]$JLinkSerial = "",
    [ValidateScript({$_ -ge 1000 -and $_ % 1000 -eq 0})][int]$FrequencyHz = 100000,
    [ValidateRange(10, 180)]
    [int]$AttachWindowSeconds = 120,
    [ValidateRange(60, 900)]
    [int]$OperationTimeoutSeconds = 360,
    [ValidateRange(3,120)][int]$AttemptTimeoutSeconds = 12,
    [string]$ExistingPass1 = "",
    [string]$ExistingPass2 = "",
    [string]$ExistingPass1Log = "",
    [string]$ExistingPass2Log = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\..\..\.."))
. (Join-Path $PSScriptRoot '../terminal-log.ps1')
. (Join-Path $PSScriptRoot 'readonly-process.ps1')
$terminalState = Get-InkTagTerminalColour
try {
$project = Join-Path $PSScriptRoot "../om6626-readonly-v788j.jflash"
$expectedSize = 0x00100000
$base = 0x00400000
$end = $base + $expectedSize - 1
$existing = ($ExistingPass1 -ne "" -or $ExistingPass2 -ne "" -or
    $ExistingPass1Log -ne "" -or $ExistingPass2Log -ne "")
if ($existing -and ($ExistingPass1 -eq "" -or $ExistingPass2 -eq "" -or
    $ExistingPass1Log -eq "" -or $ExistingPass2Log -eq "")) {
    throw "Existing-pass validation requires both binary paths and both J-Flash log paths."
}
if (!$OutputDir) { $OutputDir = Join-Path $repoRoot ".local\backups" }
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)

function Assert-ReadResult([string]$Image, [string]$Log) {
    if (!(Test-Path -LiteralPath $Image -PathType Leaf)) { throw "Missing readout: $Image" }
    if (!(Test-Path -LiteralPath $Log -PathType Leaf)) { throw "Missing J-Flash log: $Log" }
    $length = (Get-Item -LiteralPath $Image).Length
    if ($length -ne $expectedSize) { throw "Readout has $length bytes, expected $expectedSize`: $Image" }
    $logText = Get-Content -LiteralPath $Log -Raw
    if ($logText -notmatch '256 \* 4 KB @ 0x00400000' -or
        $logText -notmatch 'Target memory read successfully\. \(1048576 bytes, 1 range\)' -or
        $logText -notmatch 'Data file saved successfully \(1048576 bytes, 1 range\)') {
        throw "J-Flash did not confirm the complete 1 MiB physical-bank read: $Log"
    }
}

function New-OmBackupSpeedProject([string]$Template, [string]$Destination, [int]$Hz) {
    if ($Hz -lt 1000 -or $Hz % 1000 -ne 0) { throw 'FrequencyHz requires positive whole kHz (Hz divisible by 1000).' }
    $text = [IO.File]::ReadAllText($Template)
    $speeds = [regex]::Matches($text, '(?m)^  Speed[01] = 100\r?$')
    if ($speeds.Count -ne 2) { throw 'Unexpected read-only project speed fields; refusing to rewrite.' }
    # Only host interface speeds change, not loader/init/erase/program settings.
    $khz = [int]($Hz / 1000)
    $text = [regex]::Replace($text, '(?m)^  Speed0 = 100(?=\r?$)', ('  Speed0 = '+[Math]::Min(100,$khz)))
    $text = [regex]::Replace($text, '(?m)^  Speed1 = 100(?=\r?$)', ('  Speed1 = '+$khz))
    if (Test-Path -LiteralPath $Destination) { throw 'Refusing to overwrite a speed project.' }
    [IO.File]::WriteAllText($Destination, $text, [Text.Encoding]::ASCII)
    return $Destination
}

function Invoke-ReadPass([string]$PassName, [string]$TargetDir, [bool]$TraceEnabled = $false) {
    $deadline = (Get-Date).AddSeconds($AttachWindowSeconds)
    $attempt = 0
    do {
        if($attempt -gt 0 -and (Get-Date) -ge $deadline){throw "Backup detection window expired; no new attempt. Logs: $TargetDir"}
        $attempt++
        $stem = "${PassName}-attempt{0:D3}" -f $attempt
        $image = Join-Path $TargetDir "$stem.bin"
        $jflashLog = Join-Path $TargetDir "$stem.jflash.log"
        $jlinkLog = Join-Path $TargetDir "$stem.jlink.log"
        $arguments = @(
            "-hide",
            "-jlinkdevicesxmlpath$JLinkDevicesXml",
            "-jflashlog$jflashLog",
            "-jlinklog$jlinkLog",
            "-openprj$project",
            ("-readrange0x{0:X8},0x{1:X8}" -f $base, $end),
            ("-saveas{0},0x{1:X8},0x{2:X8}" -f $image, $base, $end),
            "-exit"
        )
        if ($JLinkSerial) { $arguments = @("-usb$JLinkSerial") + $arguments }
        # Start-Process otherwise splits an argument containing a path with spaces.
        $argumentLine = ($arguments | ForEach-Object {
            if ($_ -match '"') { throw "Unsupported quote in J-Flash argument: $_" }
            '"' + $_ + '"'
        }) -join ' '
        Write-InkTagTerminalMessage "$PassName attempt $attempt`: waiting for OM6626 connection."
        Write-InkTagTerminalMessage "Raw logs: $jflashLog ; $jlinkLog" -Level DEBUG
        Write-InkTagTerminalMessage "$JFlashExe $argumentLine" -Level DEBUG
        $stdoutLog = Join-Path $TargetDir "$stem.stdout.log"
        $stderrLog = Join-Path $TargetDir "$stem.stderr.log"
        $session=$null
        try {
        $session=Start-OmReadWorker @{Kind='Backup';FilePath=$JFlashExe;Arguments=$arguments;Stdout=$stdoutLog;Stderr=$stderrLog} $TargetDir
        $process=$session.Process
        $null = $process.Handle # Retain the native handle for ExitCode on Windows PowerShell 5.1.
        Enable-OmReadWorker $session
        $readStarted = Get-Date
        $contactSeen=$false;$targetAccess=$false;$timedOut=$false;$operationStarted=$null;$lastNotice=-1
        $contactLimit=$AttemptTimeoutSeconds
        if($RetryDetect){$contactLimit=[Math]::Min($contactLimit,[Math]::Max(0,($deadline-(Get-Date)).TotalSeconds))}
        $consoleOffsets = @{'trace-enabled' = $TraceEnabled}
        while (!$process.HasExited) {
            Show-OmBackupLog -Paths @($jflashLog, $jlinkLog, $stdoutLog, $stderrLog) -State $consoleOffsets -Stage $PassName
            Show-OmBackupHeartbeat -State $consoleOffsets -Stage $PassName -ElapsedSeconds ((Get-Date)-$readStarted).TotalSeconds
            $raw=Get-OmReadText @($jflashLog,$jlinkLog)
            if($raw -match 'Connected successfully|Found SW-DP|Target memory read successfully'){
                if(!$contactSeen){$operationStarted=Get-Date};$contactSeen=$true
            }
            if($raw -match 'JLINK_(?:Connect|ReadMem|Write|Go|Halt|Reset)'){$targetAccess=$true}
            $elapsed=((Get-Date)-$readStarted).TotalSeconds
            if(!$contactSeen -and $elapsed -ge $contactLimit){$timedOut=$true;break}
            if($contactSeen -and ((Get-Date)-$operationStarted).TotalSeconds -ge $OperationTimeoutSeconds){
                throw "J-Flash read timed out after $OperationTimeoutSeconds seconds; partial output retained in $TargetDir"
            }
            $notice=[int][Math]::Floor($elapsed/5)
            if(!$contactSeen -and $notice -gt 0 -and $notice -ne $lastNotice){
                $lastNotice=$notice
                Write-InkTagTerminalMessage "$PassName attempt $attempt`: still waiting for probe/target connection ($([int]$elapsed)s); read has not started."
            }
            Start-Sleep -Milliseconds 250
            $process.Refresh()
        }
        if($timedOut){Stop-OmReadWorker $session}else{$process.WaitForExit()}
        Show-OmBackupLog -Paths @($jflashLog, $jlinkLog, $stdoutLog, $stderrLog) -State $consoleOffsets -Stage $PassName -Final
        $raw=Get-OmReadText @($jflashLog,$jlinkLog)
        if($raw -match 'Connected successfully|Found SW-DP|Target memory read successfully'){$contactSeen=$true}
        if($raw -match 'JLINK_(?:Connect|ReadMem|Write|Go|Halt|Reset)'){$targetAccess=$true}
        @{naturalExit=(!$timedOut);exitCode=$(if($timedOut){$null}else{$process.ExitCode});contactSeen=$contactSeen;targetAccess=$targetAccess;contactTimedOut=$timedOut} |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $TargetDir "$stem.native.json") -Encoding UTF8
        if($timedOut){
            $trace=Get-OmReadText @($jlinkLog)
            $workerErrors=Get-OmReadText @($session.Stderr)
            $errors=Get-OmReadText @($jflashLog,$jlinkLog,$stdoutLog,$stderrLog)
            $onlyOpen=$trace -match '(?m)JLINK_OpenEx\(\.\.\.\)\s*\z' -and
                $trace -notmatch 'JLINK_(?!SelectUSB\(|OpenEx\()' -and !$workerErrors.Trim() -and !(Test-Path -LiteralPath $image) -and
                $errors -notmatch '(?i)license[^\r\n]*(?:expired|invalid|missing|error)|access denied|unknown command|wrong SWD target|ERROR:'
            if(!$RetryDetect -or $contactSeen -or $targetAccess -or !$onlyOpen -or (Get-Date) -ge $deadline){
                throw "Probe/target contact timed out after $contactLimit seconds; stopped owned processes, no read replay. Logs: $TargetDir"
            }
            Write-InkTagTerminalMessage "$PassName : probe open stalled before target access; owned processes stopped, bounded detection retry." -Level WARNING
            Start-Sleep -Milliseconds 1000
            continue
        }
        $logText = if (Test-Path -LiteralPath $jflashLog) {
            Get-Content -LiteralPath $jflashLog -Raw
        } else { "" }
        if ($process.ExitCode -eq 0 -and
            $logText -match 'Target memory read successfully\. \(1048576 bytes, 1 range\)') {
            Assert-ReadResult $image $jflashLog
            Write-InkTagTerminalMessage "$PassName : complete 1048576-byte read and saved-file validation passed." -Level SUCCESS
            return @{ Image = $image; Log = $jflashLog; JLinkLog = $jlinkLog; Attempts = $attempt }
        }
        if ($logText -match 'Connected successfully') {
            throw "SWD attached, but the Flash read failed. Inspect $jflashLog; no automatic retry after an in-read failure."
        }
        if($process.ExitCode -ne 0 -and (Get-OmReadText @($session.Stderr)).Trim()){
            throw "Read worker failed; no automatic retry. Raw worker logs: $($session.Directory)"
        }
        # Natural failure is not pure absence if any channel records target/read
        # activity, a saved (even empty) image, or a fatal tool error. A failed
        # JLINK_Connect alone is still a legal no-contact attempt, not a read.
        $evidence=Get-OmReadText @($jflashLog,$jlinkLog,$stdoutLog,$stderrLog)
        $readAccess=$evidence -match 'JLINK(?:ARM)?_(?:ReadMem|ReadReg|Write|Go|Halt|Reset)'
        $readProgress=$consoleOffsets.ContainsKey('read-bytes') -or
            $evidence -match 'Connected successfully|Found SW-DP|Target memory read successfully|Data file saved successfully'
        $fatal=$evidence -match '(?i)license[^\r\n]*(?:expired|invalid|missing|error|failed|denied)|access denied|unknown command|wrong SWD target|could not read memory|failed to read|read failed|ERROR:'
        if($contactSeen -or $readAccess -or $readProgress -or $fatal -or (Test-Path -LiteralPath $image)){
            throw "Read/target evidence or tool refusal after natural failure; logs and any partial image retained, no automatic retry. Logs: $TargetDir"
        }
        if (!$RetryDetect -or $logText -notmatch '(?i)(failed to connect|could not connect|cannot connect|SW-DP.*(failed|not found))' -or (Get-Date) -ge $deadline) {
            throw "No SWD attach in $AttachWindowSeconds seconds ($attempt attempts). Reset/BOOT timing and logs: $TargetDir"
        }
        } finally {
            # An interrupted read must not leave a hidden vendor process holding
            # the probe. This never retries, resumes or mutates target Flash.
            Stop-OmReadWorker $session
        }
        Write-InkTagTerminalMessage "$PassName : target not detected; bounded detection retry." -Level WARNING
        Start-Sleep -Milliseconds 1000
    } while ($true)
}

if (!$existing) {
    . (Join-Path $repoRoot "tools\jlink-tools.ps1")
    $JFlashExe = Resolve-InkTagJLinkTool -ToolName "JFlash.exe" -ExplicitPath $JFlashExe -RepoRoot $repoRoot
    $jlinkExe = Resolve-InkTagJLinkTool -ToolName "JLink.exe" -ExplicitPath $JLinkExe -RepoRoot $repoRoot
    Assert-InkTagJLink788j -JLinkExe $jlinkExe
    if ((Split-Path -Parent $JFlashExe) -ne (Split-Path -Parent $jlinkExe)) {
        throw "J-Flash and J-Link must come from the same 7.88j installation."
    }
    if (!$JLinkDevicesXml) {
        $JLinkDevicesXml = Join-Path $env:APPDATA "SEGGER\JLinkDevices\InkTag\OM6626\JLinkDevices.xml"
    }
    if (!(Test-Path -LiteralPath $project -PathType Leaf)) { throw "Read-only project missing: $project" }
    if (!(Test-Path -LiteralPath $JLinkDevicesXml -PathType Leaf)) {
        throw "OM device pack missing: $JLinkDevicesXml. Run install-jlink-device-pack.ps1 first."
    }
    $loader = Join-Path (Split-Path -Parent $JLinkDevicesXml) "Devices\Onmicro\OM662XX_nombr.elf"
    if (!(Test-Path -LiteralPath $loader -PathType Leaf) -or
        !(Select-String -LiteralPath $JLinkDevicesXml -SimpleMatch 'Name="OM662XX_NOMBR"' -Quiet)) {
        throw "OM662XX_NOMBR device pack is incomplete: $JLinkDevicesXml"
    }
    # Only display incremental API-trace progress for the inspected loader.
    # An unknown loader keeps activity-only display; read/archive gates do not change.
    $readTraceQualified = (Get-FileHash -LiteralPath $loader -Algorithm SHA256).Hash -eq
        '9A53C01B9363371E8EFF44A7A9DE80E2942E96025AEDE3ADE8053875AB6AD640'
    if (@(Get-Process JLink,JFlash -ErrorAction SilentlyContinue).Count -ne 0) {
        throw "Another J-Link/J-Flash process is running; stop it before stock backup."
    }
}

$targetDir = Join-Path $OutputDir ("{0}-{1}" -f $Board, (Get-Date -Format "yyyyMMdd-HHmmss"))
if (Test-Path -LiteralPath $targetDir) { throw "Refusing to reuse backup directory: $targetDir" }
New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
if (!$existing -and $FrequencyHz -ne 100000) {
    $project = New-OmBackupSpeedProject $project (Join-Path $targetDir 'readonly-speed.jflash') $FrequencyHz
}
if ($existing) {
    Assert-ReadResult $ExistingPass1 $ExistingPass1Log
    Assert-ReadResult $ExistingPass2 $ExistingPass2Log
    $pass1 = @{ Image = $ExistingPass1; Log = $ExistingPass1Log; Attempts = "existing" }
    $pass2 = @{ Image = $ExistingPass2; Log = $ExistingPass2Log; Attempts = "existing" }
} else {
    Write-InkTagTerminalStage 'FACTORY BACKUP / TWO INDEPENDENT READS'
    Write-InkTagTerminalMessage 'Read-only J-Flash 7.88j backup. Power on InkTag normally during detection (30-second SWD window); BOOT low is only a stock/ROM ISP fallback.'
    Write-InkTagTerminalStage 'BACKUP CONNECT 1/2'
    $pass1 = Invoke-ReadPass "pass1" $targetDir $readTraceQualified
    Write-InkTagTerminalStage 'BACKUP CONNECT 2/2 (INDEPENDENT VERIFICATION)'
    $pass2 = Invoke-ReadPass "pass2" $targetDir $readTraceQualified
}

Write-InkTagTerminalStage 'VERIFY BOTH COMPLETE READS / SHA256'
$hash1 = (Get-FileHash -LiteralPath $pass1.Image -Algorithm SHA256).Hash.ToUpperInvariant()
$hash2 = (Get-FileHash -LiteralPath $pass2.Image -Algorithm SHA256).Hash.ToUpperInvariant()
if ($hash1 -ne $hash2) { throw "Independent 1 MiB reads differ; do not flash. $($pass1.Image) $($pass2.Image)" }
Write-InkTagTerminalStage 'VALIDATE PHYSICAL CAPACITY'
$data = [System.IO.File]::ReadAllBytes($pass1.Image)
$sha = [System.Security.Cryptography.SHA256]::Create()
try {
    $lowHash = ([BitConverter]::ToString($sha.ComputeHash($data, 0, 0x80000))).Replace("-", "")
    $highHash = ([BitConverter]::ToString($sha.ComputeHash($data, 0x80000, 0x80000))).Replace("-", "")
} finally { $sha.Dispose() }
if ($lowHash -eq $highHash) { throw "High 512 KiB mirrors low 512 KiB; physical capacity unproven." }
$hasHighData = $false
for ($i = 0x80000; $i -lt $data.Length; $i++) {
    if ($data[$i] -ne 0x00 -and $data[$i] -ne 0xFF) { $hasHighData = $true; break }
}
if (!$hasHighData) { throw "High 512 KiB contains no non-blank data; physical capacity unproven." }

Write-InkTagTerminalStage 'SAVE AND VALIDATE BACKUP ARCHIVE'
$canonical = Join-Path $targetDir 'stock-flash.bin'
Copy-Item -LiteralPath $pass1.Image -Destination $canonical
if ($existing) {
    Copy-Item -LiteralPath $pass1.Image -Destination (Join-Path $targetDir "pass1.bin")
    Copy-Item -LiteralPath $pass2.Image -Destination (Join-Path $targetDir "pass2.bin")
    Copy-Item -LiteralPath $pass1.Log -Destination (Join-Path $targetDir "pass1.jflash.log")
    Copy-Item -LiteralPath $pass2.Log -Destination (Join-Path $targetDir "pass2.jflash.log")
}
$canonicalHash = (Get-FileHash -LiteralPath $canonical -Algorithm SHA256).Hash.ToUpperInvariant()
if ($canonicalHash -ne $hash1) { throw "Canonical copy failed verification: $canonical" }
$info = Join-Path $targetDir "backup-info.txt"
Set-Content -LiteralPath $info -Encoding ASCII -Value @(
    "target=$Board",
    "board_note=$BoardNote",
    "capture_method=JFlash_7.88j_OM662XX_NOMBR_double_read",
    "flash_base=0x00400000",
    "flash_size_bytes=$expectedSize",
    "sha256=$hash1",
    "pass1_sha256=$hash1",
    "pass2_sha256=$hash2",
    "low_half_sha256=$lowHash",
    "high_half_sha256=$highHash",
    "pass1_attempts=$($pass1.Attempts)",
    "pass2_attempts=$($pass2.Attempts)",
    "jlink_serial=$JLinkSerial",
    "requested_frequency_hz=$(if($existing){'unknown-existing-capture'}else{$FrequencyHz})"
)
Write-InkTagTerminalMessage 'Congratulations! Your factory backup is complete: both independent reads matched and the archive was validated.' -Level SUCCESS
Write-InkTagTerminalMessage "Backup archive: $canonical"
Write-InkTagTerminalMessage "Metadata and raw logs: $targetDir"
Write-InkTagTerminalMessage "Size: $expectedSize bytes; SHA256: $hash1; independent reads matched. Metadata: $info" -Level DEBUG
exit 0
} finally { try { Complete-InkTagTerminalProgress } finally { Restore-InkTagTerminalColour $terminalState } }
