[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("probe", "backup", "preserve-full", "flash")]
    [string]$Operation,
    [ValidateSet("daplink", "jlink")]
    [string]$Programmer = "jlink",
    [string]$Target = "nrf52832",
    [string]$Device = "nRF52832_xxAA",
    [string]$Uid = "",
    [string]$Snr = "",
    [string]$JLinkExe = "",
    [int]$FrequencyHz = 500000,
    [ValidateSet("halt", "pre-reset", "under-reset", "attach")]
    [string]$ConnectMode = "halt",
    [bool]$Wait = $false,
    [int]$IntervalSeconds = 1,
    [int]$TimeoutSeconds = 0,
    [ValidateRange(1,86400)][int]$OperationTimeoutSeconds = 120,
    [string]$OutputDir = "",
    [string]$Board = '',
    [string]$BoardNote = '',
    [string]$Firmware = "",
    [bool]$EraseImageStore = $false,
    [string]$ExpectedDeviceId = "",
    [string]$FullPlanDirectory = "",
    [string]$CurrentCapture = "",
    [string]$SourceFirmware = "",
    [string]$SourceConfig = ''
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'runtime.ps1')
$RepoRoot = Get-NordicRuntimeRoot
. (Join-Path $RepoRoot "tools\jlink-tools.ps1")
. (Join-Path $PSScriptRoot 'terminal-log.ps1')
$terminalState = Start-InkTagHardwareTerminal

$ExpectedFlashBytes = 0x80000
$FicrBase = 0x10000000
$UicrBase = 0x10001000
$InfoPageBytes = 0x1000
# Commander 7.88j acknowledges EoE with this exact informational line. Keep raw
# output intact and reject every other error line, including banner-like suffixes.
$ToolFailurePattern = '(?im)^(?!J-Link Commander will now exit on Error\r?$).*?(Cannot|Could not|Unable to|Failed|Error|Traceback|No ACK|communication failure|INKTAG_OPERATION_TIMEOUT)'
$script:VerifiedNordicJLink = ''

function Resolve-PyOcdCommand {
    # Prefer the interpreter module entry point on Windows. The generated
    # pyocd.exe launcher can return while its Python child still owns the
    # CMSIS-DAP handle after a failed transfer; the next retry then sees
    # "already open" and starts accumulating orphan commander processes.
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) {
        $oldErrorActionPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = "Continue"
            & $python.Source -c "import pyocd" 2>$null
            if ($LASTEXITCODE -eq 0) { return @{ Exe = $python.Source; Prefix = @("-m", "pyocd") } }
        } finally {
            $ErrorActionPreference = $oldErrorActionPreference
        }
    }
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        $oldErrorActionPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = "Continue"
            & $py.Source -c "import pyocd" 2>$null
            if ($LASTEXITCODE -eq 0) { return @{ Exe = $py.Source; Prefix = @("-m", "pyocd") } }
        } finally {
            $ErrorActionPreference = $oldErrorActionPreference
        }
    }
    $command = Get-Command pyocd -ErrorAction SilentlyContinue
    if ($command) { return @{ Exe = $command.Source; Prefix = @() } }
    throw "pyOCD not found. Install it with: python -m pip install pyocd"
}

function Resolve-JLinkCommand {
    $resolved = Resolve-InkTagJLinkTool -ToolName "JLink.exe" -ExplicitPath $JLinkExe -RepoRoot $RepoRoot
    # Same exit-only / AutoConnect=0 version check, supervised too; a hung
    # version process must not evade the public single-operation deadline.
    if ($script:VerifiedNordicJLink -ne $resolved) {
        $versionDir = Join-Path $env:TEMP ('inktag-nrf-version-' + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $versionDir | Out-Null
        $versionFile = Join-Path $versionDir 'version.jlink'
        Set-Content -LiteralPath $versionFile -Value 'exit' -Encoding ASCII
        $version = Invoke-NordicLoggedProcess -Exe $resolved -Arguments @('-NoGui','1','-AutoConnect','0','-CommanderScript',$versionFile) `
            -LogPath (Join-Path $versionDir 'version.log') -OperationTimeoutSeconds $OperationTimeoutSeconds
        if ($version.ExitCode -ne 0 -or $version.Text -notmatch '(?i)J-Link Commander V7\.88j') { throw "InkTag requires J-Link Commander 7.88j. Version check failed; evidence: $versionDir" }
        $script:VerifiedNordicJLink = $resolved
    }
    return $resolved
}

function Invoke-LoggedTool {
    param(
        [Parameter(Mandatory = $true)][string]$Exe,
        [Parameter(Mandatory = $true)][object[]]$Args,
        [string]$LogPath = "",
        [switch]$ShowProgress
    )
    return Invoke-NordicLoggedProcess -Exe $Exe -Arguments $Args -LogPath $LogPath `
        -OperationTimeoutSeconds $OperationTimeoutSeconds -ShowProgress:$ShowProgress
}

function Invoke-InteractiveTool {
    param(
        [Parameter(Mandatory = $true)][string]$Exe,
        [Parameter(Mandatory = $true)][object[]]$Args
    )
    # Do not pipe or capture the programming process. pyOCD checks whether
    # stdout is a terminal and only renders its live erase/program bar when it
    # inherits the console directly. Start-Process also keeps child stdout out
    # of this function's PowerShell success stream, so the caller receives one
    # integer exit code instead of an array containing progress-bar text.
    $process = Start-Process -FilePath $Exe -ArgumentList $Args -NoNewWindow -Wait -PassThru
    return [int]$process.ExitCode
}

function New-JLinkCommandFile {
    param([string[]]$Commands, [string]$Path)
    Set-Content -LiteralPath $Path -Value $Commands -Encoding ASCII
    return $Path
}

function Invoke-JLinkCommands {
    param([string[]]$Commands, [string]$WorkDir, [string]$LogPath = "", [switch]$ShowProgress)
    if ($Operation -in @('preserve-full','flash')) {
        # Commander normally resumes the CPU on close, even without an explicit
        # go. Keep the new App halted across programming, host verification and
        # the independent final identity read. Only the guarded final r/g starts
        # it; otherwise that reset can interrupt its one-shot first-page refresh.
        $haltIndex = [Array]::IndexOf($Commands, 'halt')
        if ($haltIndex -lt 0 -or $haltIndex -ge $Commands.Length - 1) {
            throw 'Full requires an explicit halt before native commands.'
        }
        # Enable error-stop after halt so the qualified connect->halt recovery
        # remains available, but a later failed reset cannot fall through to go.
        $Commands = @('exec SetRestartOnClose = 0') + @($Commands[0..$haltIndex]) + @('EoE 1') +
            @($Commands[($haltIndex + 1)..($Commands.Length - 1)])
    }
    $exe = Resolve-JLinkCommand
    $cmdFile = Join-Path $WorkDir ("nrf52832-{0}.jlink" -f ([Guid]::NewGuid().ToString("N")))
    New-JLinkCommandFile -Commands $Commands -Path $cmdFile | Out-Null
    $speedKHz = [Math]::Max(1, [int][Math]::Ceiling($FrequencyHz / 1000.0))
    $autoConnect = if ($Operation -in @('preserve-full','flash')) { '0' } else { '1' }
    $args = @("-NoGui", "1", "-Device", $Device, "-If", "SWD", "-Speed", "$speedKHz", "-AutoConnect", $autoConnect, "-CommanderScript", $cmdFile)
    if ($Snr -ne "") { $args = @("-SelectEmuBySN", $Snr) + $args }
    $result = Invoke-LoggedTool -Exe $exe -Args $args -LogPath $LogPath -ShowProgress:$ShowProgress
    if ($Operation -in @('preserve-full','flash')) {
        $serials = @([regex]::Matches($result.Text, '(?im)^\s*S/N:\s*([0-9]+)\s*\r?$') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
        if ($serials.Count -ne 1 -or ($Snr -and $serials[0] -cne $Snr)) {
            throw 'Missing/changed exact J-Link serial; no following capture/write/reset.'
        }
        # Pin the actual first selected probe for every remaining invocation.
        Set-Variable -Name Snr -Value $serials[0] -Scope Script
    }
    return $result
}

function Invoke-PyOcdCommands {
    param([string[]]$Commands, [string]$WorkDir, [string]$LogPath = "", [switch]$ShowProgress)
    $pyocd = Resolve-PyOcdCommand
    $cmdFile = Join-Path $WorkDir ("nrf52832-{0}.pyocd" -f ([Guid]::NewGuid().ToString("N")))
    Set-Content -LiteralPath $cmdFile -Value $Commands -Encoding ASCII
    $args = @($pyocd.Prefix) + @("commander", "--target", $Target, "--frequency", "$FrequencyHz", "--connect", $ConnectMode,
        "-O", "auto_unlock=false")
    if ($Uid -ne "") { $args += @("--uid", $Uid) }
    $args += @("--execute", $cmdFile)
    return Invoke-LoggedTool -Exe $pyocd.Exe -Args $args -LogPath $LogPath -ShowProgress:$ShowProgress
}

function Test-Target {
    $tempDir = Join-Path $env:TEMP ('inktag-nrf52832-readiness-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tempDir | Out-Null
    $detectLog = Join-Path $tempDir 'readiness.log'
    Write-InkTagHardwareMessage "Readiness evidence: $detectLog"
    if ($Programmer -eq "jlink") {
        $result = Invoke-JLinkCommands -WorkDir $tempDir -LogPath $detectLog -Commands @(
            "si SWD", "speed $([Math]::Max(1, [int]($FrequencyHz / 1000)))", "connect", "halt",
            "mem32 0x10000060 2", "q"
        )
        # J-Link can fail the initial connect, then connect successfully when
        # halt requires a target. Judge the actual two-word DEVICEID response,
        # not the echoed command or an earlier connection diagnostic. This is
        # readiness only; exact silicon/capacity validation still follows.
        $reads = [regex]::Matches($result.Text, '(?im)^[ \t]*10000060[ \t]*=[ \t]*[0-9a-f]{8}[ \t]+[0-9a-f]{8}[ \t]*\r?$')
        $responses = [regex]::Matches($result.Text, '(?im)^[ \t]*10000060[ \t]*=')
        $readErrors = '(?im)^(?!J-Link Commander will now exit on Error\r?$).*?(Could not|Cannot(?![ \t]+connect\b)|Unable to|Failed|\bfailure\b|Error(?! while connecting)|Traceback|INKTAG_OPERATION_TIMEOUT)'
        if ($result.ExitCode -eq 0 -and $reads.Count -eq 1 -and $responses.Count -eq 1 -and
            $result.Text -notmatch $readErrors) {
            $afterRead = $result.Text.Substring($reads[0].Index + $reads[0].Length)
            if ($afterRead -notmatch '(?i)(Cannot connect|Could not|Failed|Error|INKTAG_OPERATION_TIMEOUT)') { return $true }
        }
        if ($result.Text -match '(?i)(Cannot connect|Error while connecting)' -and
            $result.Text -notmatch '(?i)10000060\s*=' -and $result.Text -notmatch $readErrors) { return $false }
        throw 'SWD readiness failed/unknown; no detection retry. Preserve the tool diagnostics.'
    }
    # pyOCD read32 takes a byte length, unlike J-Link mem32 whose second
    # operand is a word count. Read the two 32-bit DEVICEID words as 8 bytes.
    $result = Invoke-PyOcdCommands -WorkDir $tempDir -LogPath $detectLog -Commands @("read32 0x10000060 8")
    if ($result.ExitCode -eq 0 -and $result.Text -match '10000060' -and
        $result.Text -notmatch '(?i)(No ACK|communication failure|Error while initing|Traceback|INKTAG_OPERATION_TIMEOUT)') { return $true }
    if ($result.Text -match '(?i)(No ACK|Error while initing)' -and
        $result.Text -notmatch '(?i)(10000060\s*[:=]|Traceback|INKTAG_OPERATION_TIMEOUT)') { return $false }
    throw 'SWD readiness failed/unknown; no detection retry. Preserve the tool diagnostics.'
}

function Wait-Target {
    if (!$Wait) {
        if (!(Test-Target)) { throw "nRF52832 target is not readable over $Programmer SWD." }
        Write-InkTagHardwareMessage 'SWD target detected; exact silicon identity check follows.'
        return
    }
    if ($Programmer -eq "jlink") {
        Write-InkTagHardwareMessage 'Waiting for J-Link SWD target. Connect VCC/GND/SWDIO/SWCLK...'
    } else {
        Write-InkTagHardwareMessage 'Waiting for DAPLink/CMSIS-DAP SWD target. Connect VCC/GND/SWDIO/SWCLK...'
    }
    $started = [Diagnostics.Stopwatch]::StartNew()
    while (!(Test-Target)) {
        if ($TimeoutSeconds -gt 0 -and $started.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
            throw "Timed out waiting for nRF52832 after $TimeoutSeconds seconds."
        }
        Write-InkTagHardwareMessage 'Target absent; retrying readiness only.' -Level WARNING
        $delay = 1000 * $IntervalSeconds
        if ($TimeoutSeconds -gt 0) { $delay = [Math]::Min($delay, [Math]::Max(0, 1000 * $TimeoutSeconds - $started.Elapsed.TotalMilliseconds)) }
        Start-Sleep -Milliseconds ([int]$delay)
        if ($TimeoutSeconds -gt 0 -and $started.Elapsed.TotalSeconds -ge $TimeoutSeconds) { throw "Timed out waiting for nRF52832 after $TimeoutSeconds seconds." }
    }
    Write-InkTagHardwareMessage 'SWD target detected; exact silicon identity check follows.'
}

function Invoke-ReadCommands {
    param([string[]]$Commands, [string]$WorkDir, [string]$LogPath, [switch]$ShowProgress)
    if ($Programmer -eq "jlink") {
        return Invoke-JLinkCommands -Commands (@("si SWD", "speed $([Math]::Max(1, [int]($FrequencyHz / 1000)))", "connect", "halt") + $Commands + "q") `
            -WorkDir $WorkDir -LogPath $LogPath -ShowProgress:$ShowProgress
    }
    return Invoke-PyOcdCommands -Commands $Commands -WorkDir $WorkDir -LogPath $LogPath -ShowProgress:$ShowProgress
}

function Get-PortablePath([string]$Path) { return $Path.Replace("\", "/") }

function Test-NordicReadResult {
    param($Result, [string[]]$Commands)
    if ($Result.ExitCode -ne 0) { return $false }
    if ($Result.Text -notmatch $ToolFailurePattern) { return $true }
    if ($Programmer -ne 'jlink') { return $false }
    # Only the observed initial connect failure may be superseded. Keep the
    # original log, never retry a read, and never apply this exception to a
    # download/reset. Every requested read needs its own ordered success proof.
    $halt = [regex]::Matches($Result.Text, '(?m)^J-Link>halt\r?$')
    $initial = [regex]::Matches($Result.Text, '(?m)^Cannot connect to target\.\r?$')
    if ($halt.Count -ne 1 -or $initial.Count -ne 1 -or $initial[0].Index -ge $halt[0].Index) { return $false }
    $prefix = $Result.Text.Substring(0, $halt[0].Index)
    $prefix = [regex]::Replace($prefix, '(?m)^Cannot connect to target\.\r?\n?', '')
    $tail = $Result.Text.Substring($halt[0].Index)
    if ($prefix -match $ToolFailurePattern -or $tail -match $ToolFailurePattern -or
        $tail -match '(?im)^J-Link>(loadfile|r|g)\b' -or
        $prefix -match '(?im)^J-Link>(savebin|loadfile|r|g)\b') { return $false }
    $connected = [regex]::Matches($tail, '(?m)^Cortex-M4 identified\.\r?$')
    $echoes = [regex]::Matches($tail, '(?m)^J-Link>savebin\b[^\r\n]*')
    $reads = [regex]::Matches($tail, '(?m)^Reading [^\r\n]*')
    if ($connected.Count -ne 1 -or $Commands.Count -eq 0 -or
        $echoes.Count -ne $Commands.Count -or $reads.Count -ne $Commands.Count) { return $false }
    $previous = $connected[0].Index + $connected[0].Length
    for ($i = 0; $i -lt $Commands.Count; $i++) {
        if ($Commands[$i] -notmatch '^savebin "[^"]+" 0x([0-9A-Fa-f]+) 0x([0-9A-Fa-f]+)$') { return $false }
        $address = [Convert]::ToUInt32($Matches[1], 16)
        $length = [Convert]::ToUInt32($Matches[2], 16)
        $pattern = '^Reading ' + $length + ' bytes from addr 0x0*' + $address.ToString('X') + ' into file\.\.\.O\.K\.$'
        if ($echoes[$i].Value -cne ('J-Link>' + $Commands[$i]) -or
            $echoes[$i].Index -lt $previous -or $reads[$i].Index -lt $echoes[$i].Index -or
            $reads[$i].Value -notmatch $pattern) { return $false }
        $previous = $reads[$i].Index + $reads[$i].Length
    }
    return $true
}

function Save-IdentityPages {
    param([string]$WorkDir, [string]$FicrPath, [string]$UicrPath, [string]$LogPath)
    if ($Programmer -eq "jlink") {
        $commands = @(
            ('savebin "{0}" 0x{1:X8} 0x{2:X}' -f $FicrPath, $FicrBase, $InfoPageBytes),
            ('savebin "{0}" 0x{1:X8} 0x{2:X}' -f $UicrPath, $UicrBase, $InfoPageBytes)
        )
    } else {
        $commands = @(
            ('savemem 0x{0:X8} 0x{1:X} "{2}"' -f $FicrBase, $InfoPageBytes, (Get-PortablePath $FicrPath)),
            ('savemem 0x{0:X8} 0x{1:X} "{2}"' -f $UicrBase, $InfoPageBytes, (Get-PortablePath $UicrPath))
        )
    }
    Set-NordicProgressLabel 'Read identity' 8192
    $result = Invoke-ReadCommands -Commands $commands -WorkDir $WorkDir -LogPath $LogPath -ShowProgress
    if (!(Test-NordicReadResult -Result $result -Commands $commands) -or
        !(Test-Path $FicrPath) -or !(Test-Path $UicrPath) -or
        (Get-Item -LiteralPath $FicrPath).Length -ne $InfoPageBytes -or
        (Get-Item -LiteralPath $UicrPath).Length -ne $InfoPageBytes) {
        throw "Could not preserve nRF52832 FICR/UICR. The target may be read-protected. Check $LogPath"
    }
}

function Read-U32([byte[]]$Bytes, [int]$Offset) { return [BitConverter]::ToUInt32($Bytes, $Offset) }

try {
if ($FrequencyHz -lt 1000) { throw "FrequencyHz must be >= 1000" }
if ($IntervalSeconds -lt 1) { throw "IntervalSeconds must be >= 1" }
if ($TimeoutSeconds -lt 0) { throw "TimeoutSeconds must be >= 0" }
if ($Board.Length -gt 128 -or $BoardNote.Length -gt 512 -or $Board -match '[\p{Cc}\p{Zl}\p{Zp}]' -or $BoardNote -match '[\p{Cc}\p{Zl}\p{Zp}]') {
    throw 'Board/BoardNote must be bounded single-line operator labels.'
}

# Validate the complete immutable plan before resolving any hardware tool.
if ($Operation -eq 'flash') {
    if ($Programmer -ne 'jlink' -or $EraseImageStore -or !$FullPlanDirectory -or !$CurrentCapture -or !$SourceFirmware -or !$SourceConfig) {
        throw 'Flash requires a scoped J-Link Full plan; the implicit migration erase/replay recipe is retired.'
    }
    $planner = Join-Path $RepoRoot 'tools/sdk/nordic-full-settings.py'
    Invoke-NordicPython $planner check --capture $CurrentCapture --firmware $SourceFirmware --config $SourceConfig --output $FullPlanDirectory
    if ($LASTEXITCODE -ne 0) { throw 'Full plan validation failed before hardware access.' }
    $plan = Get-Content -LiteralPath (Join-Path $FullPlanDirectory 'plan.json') -Raw | ConvertFrom-Json
    if ($plan.deviceId -ne $ExpectedDeviceId -or
        (Get-FileHash -LiteralPath $Firmware).Hash.ToLowerInvariant() -ne $plan.preparedHexSha256) {
        throw 'Full plan device/payload mismatch before hardware access.'
    }
    if (([DateTime]::UtcNow - (Get-Item -LiteralPath (Join-Path $CurrentCapture 'preservation-info.json')).LastWriteTimeUtc).TotalMinutes -gt 5) {
        throw 'Preservation capture expired; no automatic capture/programming replay.'
    }
    $probe = Get-Content -LiteralPath (Join-Path $CurrentCapture 'probe.json') -Raw | ConvertFrom-Json
    if ($probe.serial -notmatch '^[0-9]+$' -or !$Snr -or $Snr -cne $probe.serial) {
        throw 'Full requires the same captured J-Link serial; no hardware accessed.'
    }
}

if ($Operation -eq 'preserve-full') {
    if ($Programmer -ne 'jlink' -or !$OutputDir -or !$SourceFirmware -or !$SourceConfig -or (Test-Path -LiteralPath $OutputDir)) {
        throw 'Full preservation requires J-Link, selected package/config and a new evidence directory.'
    }
    $planner = Join-Path $RepoRoot 'tools/sdk/nordic-full-settings.py'
    $descriptionText = Invoke-NordicPython $planner describe --firmware $SourceFirmware --config $SourceConfig
    if ($LASTEXITCODE -ne 0) { throw 'Bounded Full preservation plan invalid; no device accessed.' }
    $description = ($descriptionText -join "`n") | ConvertFrom-Json
}

Wait-Target

if ($Operation -eq 'preserve-full') {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
    Write-InkTagHardwareStage 'COMPLETE FLASH / NECESSARY PRESERVATION'
    $ficrPath = Join-Path $OutputDir 'nrf52832-priceetag-ficr.bin'
    $uicrPath = Join-Path $OutputDir 'nrf52832-priceetag-uicr.bin'
    Save-IdentityPages -WorkDir $OutputDir -FicrPath $ficrPath -UicrPath $uicrPath -LogPath (Join-Path $OutputDir 'identity.log')
    $ficr = [IO.File]::ReadAllBytes($ficrPath)
    if ((Read-U32 $ficr 0x100) -ne 0x52832 -or (Read-U32 $ficr 0x10) -ne 4096 -or
        (Read-U32 $ficr 0x14) -ne 128 -or (Read-U32 $ficr 0x10C) -ne 64 -or (Read-U32 $ficr 0x110) -ne 512) {
        throw 'Wrong silicon/capacity; no main-Flash preservation read or programming.'
    }
    foreach ($part in $description.ranges) {
        $partial = Join-Path $OutputDir ($part.file+'.partial')
        $readCommands = @(('savebin "{0}" 0x{1:X8} 0x{2:X}' -f $partial, [int]$part.address, [int]$part.bytes))
        Set-NordicProgressLabel 'Read preservation' $part.bytes
        $read = Invoke-ReadCommands -WorkDir $OutputDir -LogPath (Join-Path $OutputDir 'low-gap.log') -Commands $readCommands -ShowProgress
        if (!(Test-NordicReadResult -Result $read -Commands $readCommands) -or
            !(Test-Path -LiteralPath $partial) -or (Get-Item -LiteralPath $partial).Length -ne $part.bytes) {
            throw 'Necessary native authority/low-Flash read failed; partial evidence retained, no replay.'
        }
        Move-Item -LiteralPath $partial -Destination (Join-Path $OutputDir $part.file)
    }
    if ($Snr -notmatch '^[0-9]+$') { throw 'Missing pinned capture probe serial; no programming.' }
    @{ serial=$Snr; kind='jlink'; sourceFirmwareSha256=(Get-FileHash $SourceFirmware).Hash.ToLowerInvariant() } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDir 'probe.json') -Encoding UTF8
    Invoke-NordicPython $planner capture --capture $OutputDir --firmware $SourceFirmware --config $SourceConfig
    if ($LASTEXITCODE -ne 0) { throw 'Necessary preservation validation failed; no programming.' }
    Write-InkTagHardwareMessage ("Captured FICR/UICR and {0} necessary native authority/low-Flash bytes; no whole-Flash backup." -f $description.mainFlashPreReadBytes)
    exit 0
}

if ($Operation -eq "probe") {
    $workDir = Join-Path $env:TEMP ("inktag-nrf52832-probe-{0}" -f ([Guid]::NewGuid().ToString("N")))
    New-Item -ItemType Directory -Force -Path $workDir | Out-Null
    $logPath = Join-Path $workDir "probe.log"
    $ficrPath = Join-Path $workDir "ficr.bin"
    $uicrPath = Join-Path $workDir "uicr.bin"
    Save-IdentityPages -WorkDir $workDir -FicrPath $ficrPath -UicrPath $uicrPath -LogPath $logPath
    $ficr = [IO.File]::ReadAllBytes($ficrPath)
    $uicr = [IO.File]::ReadAllBytes($uicrPath)
    $pageSize = Read-U32 $ficr 0x10
    $pageCount = Read-U32 $ficr 0x14
    $capacity = [uint64]$pageSize * [uint64]$pageCount
    $device0 = Read-U32 $ficr 0x60
    $device1 = Read-U32 $ficr 0x64
    $part = Read-U32 $ficr 0x100
    $variant = Read-U32 $ficr 0x104
    $package = Read-U32 $ficr 0x108
    $ramKib = Read-U32 $ficr 0x10C
    $flashKib = Read-U32 $ficr 0x110
    $bootloaderAddress = Read-U32 $uicr 0x14
    $mbrParameterAddress = Read-U32 $uicr 0x18
    $reset0 = Read-U32 $uicr 0x200
    $reset1 = Read-U32 $uicr 0x204
    $approtect = Read-U32 $uicr 0x208
    $nfcPins = Read-U32 $uicr 0x20C
    Write-InkTagHardwareMessage ("Device ID: {0:X8}{1:X8}; Flash {2} bytes; RAM {3} KiB" -f $device1, $device0, $capacity, $ramKib)
    Write-InkTagHardwareMessage ("Part=0x{0:X8}; variant=0x{1:X8}; package=0x{2:X8}; pages={3}x{4}" -f $part, $variant, $package, $pageCount, $pageSize) -Level DEBUG
    Write-InkTagHardwareMessage ("NRFFW boot=0x{0:X8}; mbrparam=0x{1:X8}; PSELRESET=0x{2:X8}/0x{3:X8}; NFCPINS=0x{4:X8}; APPROTECT=0x{5:X8}" -f $bootloaderAddress, $mbrParameterAddress, $reset0, $reset1, $nfcPins, $approtect) -Level DEBUG
    if ($part -ne 0x52832 -or $capacity -ne $ExpectedFlashBytes -or
        $flashKib -ne 512 -or $ramKib -ne 64) {
        throw ("Connected target is not the qualified nRF52832 xxAA: " +
            "part=0x{0:X8}, Flash={1} bytes, RAM={2} KiB. Probe was read-only." -f `
            $part, $capacity, $ramKib)
    }
    Write-InkTagHardwareMessage "Probe OK: exact nRF52832 FICR/UICR check completed. Evidence: $workDir" -Level SUCCESS
    exit 0
}

if ($Operation -eq "backup") {
    if ($OutputDir -eq "") { throw "Missing OutputDir for backup" }
    . (Join-Path $PSScriptRoot 'backup-archive.ps1')
    Assert-NordicArchivePath $OutputDir
    if (Test-Path -LiteralPath $OutputDir) { throw 'Refusing to overwrite an existing backup session' }
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
    $logPath = Join-Path $OutputDir "nrf52832-priceetag-backup.log"
    $ficrPath = Join-Path $OutputDir "nrf52832-priceetag-ficr.bin"
    $uicrPath = Join-Path $OutputDir "nrf52832-priceetag-uicr.bin"
    $flashPath = Join-Path $OutputDir "nrf52832-priceetag-flash.bin"
    $partialFlashPath = "$flashPath.partial"

    Write-InkTagHardwareMessage 'BACKUP 1/2: preserving FICR and UICR...'
    Save-IdentityPages -WorkDir $OutputDir -FicrPath $ficrPath -UicrPath $uicrPath -LogPath $logPath
    $ficr = [IO.File]::ReadAllBytes($ficrPath)
    $uicr = [IO.File]::ReadAllBytes($uicrPath)
    $pageSize = Read-U32 $ficr 0x10
    $pageCount = Read-U32 $ficr 0x14
    $capacity = [uint64]$pageSize * [uint64]$pageCount
    $part = Read-U32 $ficr 0x100
    $ramKib = Read-U32 $ficr 0x10C
    $flashKib = Read-U32 $ficr 0x110
    if ($part -ne 0x52832 -or $capacity -ne $ExpectedFlashBytes -or $flashKib -ne 512) {
        throw ("FICR does not report the expected nRF52832 xxAA identity " +
            "(part=0x{0:X8}, capacity={1}, INFO.FLASH={2} KiB). Identity pages were preserved, but Flash backup was stopped." -f `
            $part, $capacity, $flashKib)
    }

    if ($Programmer -eq "jlink") {
        $readCommand = ('savebin "{0}" 0x00000000 0x{1:X}' -f $partialFlashPath, $ExpectedFlashBytes)
    } else {
        $readCommand = ('savemem 0x00000000 0x{0:X} "{1}"' -f $ExpectedFlashBytes,
            (Get-PortablePath $partialFlashPath))
    }
    Write-InkTagHardwareMessage 'BACKUP 1/2: reading complete 512 KiB Flash. Keep all SWD contacts steady...'
    # RetryDetect ends at readiness. Never replay a capture or delete its
    # partial evidence, even when detection retries were explicitly requested.
    Set-NordicProgressLabel 'Read 1/2' $ExpectedFlashBytes
    $result = Invoke-ReadCommands -Commands @($readCommand) -WorkDir $OutputDir -LogPath $logPath -ShowProgress
    if (!(Test-NordicReadResult -Result $result -Commands @($readCommand)) -or
        !(Test-Path -LiteralPath $partialFlashPath) -or
        (Get-Item -LiteralPath $partialFlashPath).Length -ne $ExpectedFlashBytes) {
        throw "nRF52832 Flash backup was interrupted; partial evidence retained, no automatic replay. Check $logPath"
    }
    # A second independent acquisition, not a retry or a local-file copy.
    # Retain both passes and any partial evidence on failure. Publish the main
    # Flash BIN/metadata only after all three ranges match exactly.
    Write-InkTagHardwareMessage 'BACKUP 2/2: independent Flash/FICR/UICR acquisition and comparison...'
    $verifyDir = Join-Path $OutputDir 'second-read'
    New-Item -ItemType Directory -Path $verifyDir | Out-Null
    $verifyFicr = Join-Path $verifyDir 'ficr.bin'
    $verifyUicr = Join-Path $verifyDir 'uicr.bin'
    Save-IdentityPages -WorkDir $verifyDir -FicrPath $verifyFicr -UicrPath $verifyUicr -LogPath $logPath
    $verifyFlash = Join-Path $verifyDir 'flash.bin.partial'
    $verifyCommand = if ($Programmer -eq 'jlink') { 'savebin "{0}" 0x00000000 0x80000' -f $verifyFlash }
        else { 'savemem 0x00000000 0x80000 "{0}"' -f (Get-PortablePath $verifyFlash) }
    Set-NordicProgressLabel 'Read 2/2' $ExpectedFlashBytes
    $verify = Invoke-ReadCommands -Commands @($verifyCommand) -WorkDir $verifyDir -LogPath $logPath -ShowProgress
    if (!(Test-NordicReadResult -Result $verify -Commands @($verifyCommand)) -or
        !(Test-Path -LiteralPath $verifyFlash) -or (Get-Item -LiteralPath $verifyFlash).Length -ne $ExpectedFlashBytes) {
        throw 'Second independent backup read failed; both captures retained, no automatic replay.'
    }
    foreach ($pair in @(@($partialFlashPath,$verifyFlash), @($ficrPath,$verifyFicr), @($uicrPath,$verifyUicr))) {
        if ((Get-FileHash -LiteralPath $pair[0]).Hash -ne (Get-FileHash -LiteralPath $pair[1]).Hash) {
            throw 'Independent backup mismatch; both captures retained, no publication or automatic replay.'
        }
    }
    Move-Item -LiteralPath $verifyFlash -Destination (Join-Path $verifyDir 'flash.bin')
    Move-Item -LiteralPath $partialFlashPath -Destination $flashPath

    $device0 = Read-U32 $ficr 0x60
    $device1 = Read-U32 $ficr 0x64
    $variant = Read-U32 $ficr 0x104
    $package = Read-U32 $ficr 0x108
    $bootloaderAddress = Read-U32 $uicr 0x14
    $mbrParameterAddress = Read-U32 $uicr 0x18
    $reset0 = Read-U32 $uicr 0x200
    $reset1 = Read-U32 $uicr 0x204
    $approtect = Read-U32 $uicr 0x208
    $nfcPins = Read-U32 $uicr 0x20C
    $flashHash = (Get-FileHash -Algorithm SHA256 $flashPath).Hash.ToUpperInvariant()
    $ficrHash = (Get-FileHash -Algorithm SHA256 $ficrPath).Hash.ToUpperInvariant()
    $uicrHash = (Get-FileHash -Algorithm SHA256 $uicrPath).Hash.ToUpperInvariant()
    $infoPath = Join-Path $OutputDir "backup-info.txt"
    Set-Content -LiteralPath $infoPath -Encoding UTF8 -Value @(
        "board=$Board",
        "board_note=$BoardNote",
        "board_evidence=operator label only; no PCB qualification",
        "mcu=nRF52832_xxAA",
        ("device_id={0:X8}{1:X8}" -f $device1, $device0),
        ("ficr_part=0x{0:X8}" -f $part),
        ("ficr_variant=0x{0:X8}" -f $variant),
        ("ficr_package=0x{0:X8}" -f $package),
        "ficr_ram_kib=$ramKib",
        "ficr_flash_kib=$flashKib",
        "flash_size_bytes=$capacity",
        "code_page_size=$pageSize",
        "code_page_count=$pageCount",
        ("uicr_approtect=0x{0:X8}" -f $approtect),
        ("uicr_nrffw0=0x{0:X8}" -f $bootloaderAddress),
        ("uicr_nrffw1=0x{0:X8}" -f $mbrParameterAddress),
        ("uicr_pselreset0=0x{0:X8}" -f $reset0),
        ("uicr_pselreset1=0x{0:X8}" -f $reset1),
        ("uicr_nfcpins=0x{0:X8}" -f $nfcPins),
        "flash_file=$flashPath",
        "flash_sha256=$flashHash",
        "ficr_file=$ficrPath",
        "ficr_sha256=$ficrHash",
        "uicr_file=$uicrPath",
        "uicr_sha256=$uicrHash",
        "programmer=$Programmer",
        "frequency_hz=$FrequencyHz"
        'independent_read_count=2'
        'independent_read_match=true'
    )
    Write-InkTagHardwareMessage "Both independent Flash/FICR/UICR reads matched. SHA-256: $flashHash"
    Write-InkTagHardwareMessage "Capture: $flashPath; metadata: $infoPath" -Level DEBUG
    exit 0
}

if ($Operation -eq "flash") {
    Write-InkTagHardwareStage 'COMPLETE FLASH / PRE-WRITE IDENTITY AND VOLTAGE'
    if ($Firmware -eq "" -or !(Test-Path -LiteralPath $Firmware)) { throw "Firmware not found: $Firmware" }
    if ($ExpectedDeviceId -notmatch '^[0-9A-Fa-f]{16}$') {
        throw "Flash requires the immutable device ID from validated preservation reads."
    }
    $identityDir = Join-Path $env:TEMP ("inktag-nrf52832-flash-identity-{0}" -f ([Guid]::NewGuid().ToString("N")))
    New-Item -ItemType Directory -Force -Path $identityDir | Out-Null
    $identityFicr = Join-Path $identityDir "ficr.bin"
    $identityUicr = Join-Path $identityDir "uicr.bin"
    Save-IdentityPages -WorkDir $identityDir -FicrPath $identityFicr -UicrPath $identityUicr `
        -LogPath (Join-Path $identityDir "identity.log")
    $liveFicr = [IO.File]::ReadAllBytes($identityFicr)
    $liveUicr = [IO.File]::ReadAllBytes($identityUicr)
    $livePart = Read-U32 $liveFicr 0x100
    $liveDeviceId = "{0:X8}{1:X8}" -f (Read-U32 $liveFicr 0x64), (Read-U32 $liveFicr 0x60)
    $liveFlashBytes = [uint64](Read-U32 $liveFicr 0x10) * [uint64](Read-U32 $liveFicr 0x14)
    $liveRamKib = Read-U32 $liveFicr 0x10C
    $liveFlashKib = Read-U32 $liveFicr 0x110
    $liveBootloader = Read-U32 $liveUicr 0x14
    $liveMbrParameter = Read-U32 $liveUicr 0x18
    if ($livePart -ne 0x52832 -or $liveFlashBytes -ne $ExpectedFlashBytes -or
        $liveRamKib -ne 64 -or $liveFlashKib -ne 512 -or
        $liveDeviceId -ne $ExpectedDeviceId.ToUpperInvariant()) {
        throw ("Refusing Flash writes: connected target FICR/UICR is not the qualified " +
            "nRF52832 layout (part=0x{0:X8}, Flash={1} bytes, RAM={2} KiB, " +
            "boot=0x{3:X8}, MBR=0x{4:X8})." -f `
            $livePart, $liveFlashBytes, $liveRamKib, $liveBootloader, $liveMbrParameter)
    }
    $captureFicr = Join-Path $CurrentCapture 'nrf52832-priceetag-ficr.bin'
    $captureUicr = Join-Path $CurrentCapture 'nrf52832-priceetag-uicr.bin'
    if ((Get-FileHash $identityFicr).Hash -ne (Get-FileHash $captureFicr).Hash -or
        (Get-FileHash $identityUicr).Hash -ne (Get-FileHash $captureUicr).Hash) {
        throw 'Live immutable/vendor pages changed; no programming attempted.'
    }
    $identityLog = Get-Content -LiteralPath (Join-Path $identityDir 'identity.log') -Raw
    $voltageSamples = [regex]::Matches($identityLog, '(?i)VTref\s*=\s*([0-9]+(?:\.[0-9]+)?)\s*V')
    if (!$voltageSamples.Count -or [double]::Parse($voltageSamples[$voltageSamples.Count-1].Groups[1].Value,
            [Globalization.CultureInfo]::InvariantCulture) -lt 2.4) {
        throw 'Fresh target voltage unavailable/below 2.40V; no programming attempted.'
    }
    $preWriteText = $identityLog
    foreach ($part in $plan.preservationRanges) {
        $fresh = Join-Path $identityDir $part.file
        $liveCommands = @(('savebin "{0}" 0x{1:X8} 0x{2:X}' -f $fresh, [int]$part.address, [int]$part.bytes))
        Set-NordicProgressLabel 'Check preservation' $part.bytes
        $live = Invoke-ReadCommands -WorkDir $identityDir -LogPath (Join-Path $identityDir 'preservation-check.log') -Commands $liveCommands -ShowProgress
        if (!(Test-NordicReadResult -Result $live -Commands $liveCommands) -or
            !(Test-Path -LiteralPath $fresh) -or (Get-Item -LiteralPath $fresh).Length -ne $part.bytes -or
            (Get-FileHash $fresh).Hash -ne (Get-FileHash (Join-Path $CurrentCapture $part.file)).Hash) {
            throw 'Native authority/low-Flash bytes changed or read failed; no programming/retry attempted.'
        }
        $preWriteText = $live.Text
        $rangeVoltage = [regex]::Matches($preWriteText, '(?i)VTref\s*=\s*([0-9]+(?:\.[0-9]+)?)\s*V')
        if (!$rangeVoltage.Count -or [double]::Parse($rangeVoltage[$rangeVoltage.Count-1].Groups[1].Value,
                [Globalization.CultureInfo]::InvariantCulture) -lt 2.4) {
            throw 'Preservation reread voltage unavailable/below 2.40V; no programming attempted.'
        }
    }
    # Recheck the latest connection reference after necessary preservation reads,
    # not only the earlier identity acquisition. In-operation voltage sag
    # remains an electrical qualification gate, not proved by this snapshot.
    $writeVoltage = [regex]::Matches($preWriteText, '(?i)VTref\s*=\s*([0-9]+(?:\.[0-9]+)?)\s*V')
    if (!$writeVoltage.Count -or [double]::Parse($writeVoltage[$writeVoltage.Count-1].Groups[1].Value,
            [Globalization.CultureInfo]::InvariantCulture) -lt 2.4) {
        throw 'Pre-write target voltage unavailable/below 2.40V; no programming attempted.'
    }
    $readback = Join-Path $FullPlanDirectory 'readback'
    New-Item -ItemType Directory -Path $readback | Out-Null
    $logPath = Join-Path $readback 'program.log'
    # Whole synthesized sectors only; NO blanket erase, reset/go or write loop.
    $commands = @('si SWD', "speed $([Math]::Max(1, [int]($FrequencyHz / 1000)))", 'connect', 'halt',
        ('loadfile "{0}"' -f $Firmware),
        ('savebin "{0}" 0x00000000 0x80000' -f (Join-Path $readback 'flash.bin')),
        ('savebin "{0}" 0x10000000 0x1000' -f (Join-Path $readback 'ficr.bin')),
        ('savebin "{0}" 0x10001000 0x1000' -f (Join-Path $readback 'uicr.bin')), 'q')
    Write-InkTagHardwareStage 'COMPLETE FLASH / PROGRAM MAIN FLASH'
    Set-NordicProgressLabel 'Write'
    $result = Invoke-JLinkCommands -WorkDir $readback -LogPath $logPath -ShowProgress -Commands $commands
    if ($result.ExitCode -ne 0 -or $result.Text -match $ToolFailurePattern -or
        $result.Text -notmatch '(?i)(Downloading file|Flash download|Writing target memory)' -or
        $result.Text -notmatch '(?i)O\.K\.') {
        throw 'Programming result failed/unknown; retained capture, no reset or replay.'
    }
    Write-InkTagHardwareStage 'COMPLETE FLASH / VERIFY READBACK AND IDENTITY'
    Invoke-NordicPython $planner verify --capture $CurrentCapture --firmware $SourceFirmware --config $SourceConfig --output $FullPlanDirectory --readback $readback
    if ($LASTEXITCODE -ne 0) { throw 'Full independent readback failed; no reset or replay.' }
    # Re-read exact unit identity after verification, before allowing reset.
    # New paths prevent a zero/unknown tool result from reusing earlier files
    # as apparent final-read evidence.
    $finalFicr = Join-Path $readback 'final-ficr.bin'
    $finalUicr = Join-Path $readback 'final-uicr.bin'
    Save-IdentityPages -WorkDir $identityDir -FicrPath $finalFicr -UicrPath $finalUicr `
        -LogPath (Join-Path $identityDir 'final-identity.log')
    if ((Get-FileHash $finalFicr).Hash -ne (Get-FileHash $captureFicr).Hash -or
        (Get-FileHash $finalUicr).Hash -ne (Get-FileHash $captureUicr).Hash) {
        throw 'Unit identity changed after readback; no reset.'
    }
    Write-InkTagHardwareStage 'COMPLETE FLASH / CLEAR RETAINED DIAGNOSTICS'
    $cleanup = $plan.diagnosticCleanup
    $before = Join-Path $readback 'reset-before.bin'
    $observeCommands = @('si SWD', 'connect', 'halt',
        ('savebin "{0}" 0x40000400 0x4' -f $before), 'q')
    $observe = Invoke-JLinkCommands -WorkDir $readback -LogPath (Join-Path $readback 'cleanup-observe.log') -Commands $observeCommands
    if (!(Test-NordicReadResult -Result $observe -Commands @($observeCommands[3]))) { throw 'Cleanup observation failed; no reset.' }
    $observedBits = Invoke-NordicPython $planner cleanup-observe --capture $CurrentCapture --firmware $SourceFirmware --config $SourceConfig --output $FullPlanDirectory --readback $readback
    if ($LASTEXITCODE -ne 0 -or ($observedBits -join '') -notmatch '^[0-9]+$') { throw 'Cleanup ownership/reset-cause refused; no reset.' }
    $cleanupCommands = @('si SWD', 'connect', 'halt')
    # All Flash algorithms have returned and complete readback/final identity
    # passed. Only direct SWD debug writes/reads follow; no target worker or go
    # is allowed until the sole native reset. The old halted stack is abandoned.
    $writes = New-Object 'System.Collections.Generic.List[string]'
    for ($offset=0; $offset -lt [int]$cleanup.ramBytes; $offset+=4) {
        $writes.Add(('w4 0x{0:X8} 0x00000000' -f ([int]$cleanup.ramAddress+$offset)))
    }
    $cleanupCommands += $writes.ToArray()
    # RESETREAS is write-one-to-clear. Zero never clears old DOG/LOCKUP bits.
    $cleanupCommands += ('w4 0x40000400 0x{0:X8}' -f [uint32]($observedBits -join ''))
    $ramRead = 'savebin "{0}" 0x{1:X8} 0x{2:X}' -f (Join-Path $readback 'ram-after.bin'), [int]$cleanup.ramAddress, [int]$cleanup.ramBytes
    $resetRead = 'savebin "{0}" 0x40000400 0x4' -f (Join-Path $readback 'reset-after.bin')
    $cleanupReads = @($ramRead, $resetRead,
        ('savebin "{0}" 0x10000000 0x1000' -f (Join-Path $readback 'cleanup-ficr.bin')),
        ('savebin "{0}" 0x10001000 0x1000' -f (Join-Path $readback 'cleanup-uicr.bin')),
        ('savebin "{0}" 0x0005E000 0x2000' -f (Join-Path $readback 'cleanup-boot.bin')))
    $cleanupCommands += $cleanupReads + @('q')
    Set-NordicProgressLabel 'Clear diagnostics' 81924
    $cleaned = Invoke-JLinkCommands -WorkDir $readback -LogPath (Join-Path $readback 'cleanup-write.log') -Commands $cleanupCommands -ShowProgress
    if (!(Test-NordicReadResult -Result $cleaned -Commands $cleanupReads)) { throw 'Cleanup write/readback failed; no reset or replay.' }
    Invoke-NordicPython $planner cleanup-verify --capture $CurrentCapture --firmware $SourceFirmware --config $SourceConfig --output $FullPlanDirectory --readback $readback
    if ($LASTEXITCODE -ne 0) { throw 'Retained diagnostics cleanup verification failed; no reset or replay.' }
    Write-InkTagHardwareStage 'COMPLETE FLASH / GUARDED RESET'
    $reset = Invoke-JLinkCommands -WorkDir $readback -LogPath (Join-Path $readback 'reset.log') `
        -Commands @('si SWD', 'connect', 'halt', 'r', 'g', 'q')
    if ($reset.ExitCode -ne 0 -or $reset.Text -match $ToolFailurePattern) {
        throw 'Full readback passed but reset result failed/unknown; do not automatically replay.'
    }
    Write-InkTagHardwareMessage 'Congratulations! Full programmed and readback verified.' -Level SUCCESS
    Write-InkTagHardwareMessage ("Bluetooth: {0}; Recovery: {0}_OTA." -f $plan.generatedAppName)
    Write-InkTagHardwareMessage 'First startup is configured to show the device-information page.'
}
} catch {
    Write-InkTagHardwareMessage $_.Exception.Message -Level ERROR
    exit 1
} finally { Restore-InkTagHardwareTerminal $terminalState }
