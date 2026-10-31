function Get-Tlsr8359RepoRoot {
    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..\..\..\..\..")).Path
}

function Get-Tlsr8359BackupRoots {
    # Include the primary checkout without relying on a worktree's ignored
    # captures or on git.exe being installed. Never search unrelated worktrees.
    $repoRoot = Get-Tlsr8359RepoRoot
    $roots = @($repoRoot)
    $pointer = Join-Path $repoRoot ".git"
    if (Test-Path -LiteralPath $pointer -PathType Leaf) {
        $line = Get-Content -LiteralPath $pointer | Select-Object -First 1
        if ($line -match '^gitdir:\s*(.+)$') {
            $admin = $Matches[1].Trim()
            if (![IO.Path]::IsPathRooted($admin)) { $admin = Join-Path $repoRoot $admin }
            $commonFile = Join-Path $admin "commondir"
            if (Test-Path -LiteralPath $commonFile -PathType Leaf) {
                $common = (Get-Content -LiteralPath $commonFile -Raw).Trim()
                if (![IO.Path]::IsPathRooted($common)) { $common = Join-Path $admin $common }
                if (Test-Path -LiteralPath $common -PathType Container) {
                    $primary = Split-Path -Parent (Resolve-Path -LiteralPath $common).Path
                    if ($primary -notin $roots) { $roots += $primary }
                }
            }
        }
    }
    return $roots
}

function ConvertTo-Tlsr8359CanonicalBoardId {
    param([Parameter(Mandatory = $true)][string]$Board)

    switch ($Board.Trim().ToLowerInvariant()) {
        "hsel4q_04_20m_33" { return "hs_nebular420" }
        "hsel4q_02_13m_30" { return "hs_nebular213" }
        "hs_el5101_5m_66_05" { return "hs_nowa154" }
        "hs_el5102_1m_67_33" { return "hs_nowa213" }
        "tlsr8359_nowa154r" { return "hs_nowa154" }
        "tlsr8359_nowa213r" { return "hs_nowa213" }
        "tlsr8359_nebular213r" { return "hs_nebular213" }
        "tlsr8359_nebular420r" { return "hs_nebular420" }
        "hs_nowa154" { return "hs_nowa154" }
        "hs_nowa213" { return "hs_nowa213" }
        "hs_nebular213" { return "hs_nebular213" }
        "hs_nebular420" { return "hs_nebular420" }
        "hsel5q_01_54m_00" { return "hs_stellarpro154q" }
        "tlsr8359_stellarpro154q" { return "hs_stellarpro154q" }
        "hs_stellarpro154q" { return "hs_stellarpro154q" }
        default { return $Board.Trim().ToLowerInvariant() }
    }
}

function Get-Tlsr8359ResearchTarget {
    param([Parameter(Mandatory = $true)][string]$Board)

    $canonicalBoard = ConvertTo-Tlsr8359CanonicalBoardId -Board $Board
    switch ($canonicalBoard) {
        "hs_stellarpro154q" {
            return [pscustomobject]@{
                BoardId=$canonicalBoard; DisplayName='Hanshow StellarPro-154Q-N / HSEL5Q_01_54M_00'
                DefaultReadSizeBytes=[uint32]0x80000
                QualifiedProbeBaud=921600; QualifiedReadBaud=460800; QualifiedWriteBaud=460800
                ProbeActivationMs=30000; ProbeClockMHz=0; WriteActivationMs=30000; WriteClockMHz=0
                CapacityEvidence='exact-stock-512k-independent-pair; experimental-no-panel-Full'
                BaselineFlashSha256='B30853E383EE5FB0AC08E1F74224CC40345B2585349FB055B1A14CAAC90B3F38'
            }
        }
        "hs_nebular420" {
            return [pscustomobject]@{
                BoardId = $canonicalBoard
                DisplayName = "Hanshow Nebular-420R-N / HS9118"
                DefaultReadSizeBytes = [uint32]0x80000
                QualifiedProbeBaud = 921600
                QualifiedReadBaud = 460800
                QualifiedWriteBaud = 460800
                ProbeActivationMs = 30000
                ProbeClockMHz = 0
                WriteActivationMs = 60000
                WriteClockMHz = 24
                CapacityEvidence = "physical-1m-verified-first-flash-bounded-512k"
                BaselineFlashSha256 = "86271B4DA80A1C30E9D3B6E82ADEEF081BF0CD18580D74CAE9C8BABAA2DC3DB3"
            }
        }
        "hs_nebular213" {
            # Two independently matching lower/upper-window pairs and a
            # byte-for-byte upper alias qualify this exact 512-KiB unit.
            return [pscustomobject]@{
                BoardId = $canonicalBoard
                DisplayName = "Hanshow Nebular-213R-N / TLSR8359 candidate"
                DefaultReadSizeBytes = [uint32]0x80000
                QualifiedProbeBaud = 460800
                QualifiedReadBaud = 460800
                QualifiedWriteBaud = 460800
                ProbeActivationMs = 20000
                ProbeClockMHz = 24
                WriteActivationMs = 20000
                WriteClockMHz = 24
                CapacityEvidence = "verified-512k-alias-and-eb6013"
                BaselineFlashSha256 = "0B7C7491E5078851A4657F6E2EB95CFC3639A433D29D3AF2AB9F7D6EA347B3E3"
            }
        }
        "hs_nowa154" {
            return [pscustomobject]@{
                BoardId = $canonicalBoard
                DisplayName = "Hanshow Nowa-154R-N / TLSR8359"
                DefaultReadSizeBytes = [uint32]0x80000
                QualifiedProbeBaud = 921600
                QualifiedReadBaud = 921600
                QualifiedWriteBaud = 576000
                ProbeActivationMs = 20000
                ProbeClockMHz = 24
                WriteActivationMs = 20000
                WriteClockMHz = 0
                CapacityEvidence = "verified-non-mirrored-512k-read-window"
                BaselineFlashSha256 = "E70D15BF930087FCA297B9EB6E5031BF84908D7672012A7B01B81C3932A5CA04"
            }
        }
        "hs_nowa213" {
            # Two matching whole-window reads qualify the backup gate. Write
            # speed/layout remain explicit experimental first-flash gates.
            return [pscustomobject]@{
                BoardId = $canonicalBoard
                DisplayName = "Hanshow Nowa-213R-N / TLSR8359 candidate"
                DefaultReadSizeBytes = [uint32]0x80000
                QualifiedProbeBaud = 921600
                QualifiedReadBaud = 921600
                QualifiedWriteBaud = 460800
                ProbeActivationMs = 20000
                ProbeClockMHz = 24
                WriteActivationMs = 20000
                WriteClockMHz = 0
                CapacityEvidence = "verified-non-mirrored-512k-read-window-and-856013"
                BaselineFlashSha256 = "E11F55F32B4ECDF1798C80B78A22B1A52AF0DC8000B711C94B640AE66A703DB2"
            }
        }
        default {
            throw "Unknown TLSR8359 research target '$Board'."
        }
    }
}

function Get-Tlsr8359FullTarget {
    param([Parameter(Mandatory=$true)][object]$ConfigReview)

    # Full consumes the validated Config -> numeric selector/registry mapping,
    # never the independent historical research-preset switch above. This pure
    # admission is shared by DryRun and the writer, before port/tool resolution.
    $pcb=[string]$ConfigReview.board_marking
    if (!$pcb -or !$ConfigReview.write_admitted) { throw 'Full requires an admitted exact-PCB configuration.' }
    $entry=Get-Tlsr8359BoardEntry -Board $ConfigReview.board
    # The planner resolves selected-layout fields and advisory historical hints.
    # Fresh supported Flash identification remains required before any erase.
    $contract=$ConfigReview.flash_contract
    if ($contract.selectorId -ne $ConfigReview.board_id -or $contract.startAddress -ne 0 -or
        $contract.imageSizeBytes -ne 524288 -or $contract.selectorAddress -ne 0x7F100 -or
        ($contract.flashJedecId -and [string]$contract.flashJedecId -notmatch '^[0-9A-Fa-f]{6}$')) {
        throw "PCB '$pcb' has an invalid Full Flash/selector/JEDEC contract; no device accessed."
    }
    $property=$contract.PSObject.Properties['uartSws']
    if ($null -eq $property) { throw "PCB '$pcb' has no registered Full UART/SWS parameters; no device accessed." }
    $transport=$property.Value
    $names=@('writeBaud','readBaud','activationMs','clockMHz')
    if ($null -eq $transport -or @($transport.PSObject.Properties).Count -ne $names.Count) {
        throw "PCB '$pcb' has invalid Full UART/SWS parameters; no device accessed."
    }
    foreach ($name in $names) {
        $value=$transport.PSObject.Properties[$name]
        if ($null -eq $value -or ($value.Value -isnot [int] -and $value.Value -isnot [long])) {
            throw "PCB '$pcb' has invalid Full UART/SWS parameter '$name'; no device accessed."
        }
    }
    if ($transport.writeBaud -notin @(460800,576000,921600) -or
        $transport.readBaud -notin @(460800,921600) -or
        $transport.activationMs -lt 1000 -or $transport.activationMs -gt 120000 -or
        $transport.clockMHz -notin @(0,24,29)) {
        throw "PCB '$pcb' has out-of-range Full UART/SWS parameters; no device accessed."
    }
    return [pscustomobject]@{
        BoardId=[string]$ConfigReview.board; PcbMarking=$pcb; Entry=$entry; Contract=$contract
        WriteBaud=$transport.writeBaud; ReadBaud=$transport.readBaud
        ActivationMs=$transport.activationMs; ClockMHz=$transport.clockMHz
    }
}

function Get-TlsrSerialPortNames {
    # Do not use Win32_SerialPort as the presence authority. Windows can omit a
    # present CH340 from that class even though the Ports PnP node and COM name
    # are already usable. Merge the .NET inventory with present Ports-class PnP
    # names so both explicit -Port validation and automatic selection see it.
    $ports = @([IO.Ports.SerialPort]::GetPortNames())
    $isWindowsHost = [Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT
    if ($isWindowsHost -and $null -ne (Get-Command Get-PnpDevice -ErrorAction SilentlyContinue)) {
        $pnpPorts = @(Get-PnpDevice -PresentOnly -Class Ports -ErrorAction SilentlyContinue)
        foreach ($item in $pnpPorts) {
            if ($item.FriendlyName -match '\((COM[0-9]+)\)') {
                $ports += $Matches[1].ToUpperInvariant()
            }
        }
    }

    return @($ports | Sort-Object -Unique | Sort-Object {
        if ($_ -match '^COM([0-9]+)$') { [int]$Matches[1] } else { [int]::MaxValue }
    }, { $_ })
}

function Resolve-TlsrSerialPort {
    param(
        [string]$RequestedPort = "",
        [switch]$RequireQualifiedCh340
    )

    $available = @(Get-TlsrSerialPortNames)
    if ($RequestedPort -ne "" -and $RequestedPort -ne "auto") {
        $normalized = $RequestedPort.Trim().ToUpperInvariant()
        if ($normalized -notmatch '^COM[0-9]+$') {
            throw "Invalid serial port '$RequestedPort'. Expected COM<number> or auto."
        }
        if ($normalized -notin $available) {
            $description = if ($available.Count -eq 0) { "none" } else { $available -join ", " }
            throw "Serial port $normalized is not present. Available ports: $description"
        }
        return $normalized
    }

    if ($available.Count -eq 0) {
        throw "No serial ports found. Connect the 3.3 V USB-UART adapter and retry."
    }
    if ($available.Count -eq 1 -and !$RequireQualifiedCh340) { return $available[0] }

    $usbPorts = @()
    $qualifiedCh340Ports = @()
    $isWindowsHost = [Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT
    if ($isWindowsHost -and $null -ne (Get-Command Get-CimInstance -ErrorAction SilentlyContinue)) {
        $pnpPorts = @(Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue | Where-Object {
            $_.Name -match '\(COM[0-9]+\)' -and $_.PNPDeviceID -like 'USB\*'
        })
        foreach ($item in $pnpPorts) {
            if ($item.Name -match '\((COM[0-9]+)\)') {
                $candidate = $Matches[1].ToUpperInvariant()
                if ($candidate -in $available) {
                    $usbPorts += $candidate
                    if ($item.PNPDeviceID -match '^USB\\VID_1A86&PID_7523\\') {
                        $qualifiedCh340Ports += $candidate
                    }
                }
            }
        }
        $usbPorts = @($usbPorts | Sort-Object -Unique)
        $qualifiedCh340Ports = @($qualifiedCh340Ports | Sort-Object -Unique)
    }
    if ($qualifiedCh340Ports.Count -eq 1) { return $qualifiedCh340Ports[0] }
    if ($RequireQualifiedCh340) {
        $description = if ($available.Count -eq 0) { "none" } else { $available -join ", " }
        throw "Automatic first-flash requires exactly one qualified CH340 (VID_1A86&PID_7523). Available ports: $description. Connect the CH340 or pass its exact -Port COM<number>."
    }
    if ($usbPorts.Count -eq 1) { return $usbPorts[0] }

    $description = $available -join ", "
    throw "More than one serial port is available ($description). Pass -Port COM<number>."
}

function Write-TlsrResetPrompt {
    param(
        [Parameter(Mandatory = $true)][string]$Action,
        [Parameter(Mandatory = $true)][int]$ActivationMs
    )

    Write-Host ""
    Write-Host ("ACTION: {0}" -f $Action) -ForegroundColor Yellow
    if ($ActivationMs -ge 1000) {
        Write-Host ("Wait for 'Activate ({0} ms)...' below, then press and release RESET once." -f $ActivationMs)
        Write-Host "If RESET is not wired, power-cycle only the target VDD during that window."
        Write-Host "Keep the USB-UART powered/enumerated; a separate target supply or battery is preferred."
        Write-Host ("The activation window remains open for {0:N0} seconds." -f ($ActivationMs / 1000))
        Write-Host "A validated SWS response ends the activation window immediately; the full duration is only a timeout."
    } else {
        Write-Host "RESET must be wired to USB-UART RTS because the activation window is shorter than one second."
    }
    Write-Host "After activation succeeds, do not reset or disturb the power/SWS wiring."
}

function Resolve-Tlsr825xComFlasher {
    param([string]$RequestedPath = "")

    if ($RequestedPath -ne "") {
        $requestedTool = $RequestedPath
        if (Test-Path -LiteralPath $requestedTool -PathType Container) {
            $requestedTool = Join-Path $requestedTool "TLSR825xComFlasher.py"
        }
        if (!(Test-Path -LiteralPath $requestedTool -PathType Leaf)) {
            throw "TLSR COM-SWire tool not found: $RequestedPath (expected TLSR825xComFlasher.py or its checkout directory)"
        }
        $resolved = (Resolve-Path -LiteralPath $requestedTool).Path
    } elseif ($env:TLSR_COM_SWIRE_TOOL) {
        $environmentTool = $env:TLSR_COM_SWIRE_TOOL
        if (Test-Path -LiteralPath $environmentTool -PathType Container) {
            $environmentTool = Join-Path $environmentTool "TLSR825xComFlasher.py"
        }
        if (!(Test-Path -LiteralPath $environmentTool -PathType Leaf)) {
            throw "TLSR_COM_SWIRE_TOOL points to a missing file: $env:TLSR_COM_SWIRE_TOOL"
        }
        $resolved = (Resolve-Path -LiteralPath $environmentTool).Path
    } else {
        $repoRoot = Get-Tlsr8359RepoRoot
        $resolved = ""
        $searchRoots = @($repoRoot)

        # A linked Git worktree stores a text .git pointer instead of the
        # primary checkout's .git directory.  Resolve its commondir directly
        # so tool discovery does not depend on git.exe being available in the
        # Windows PowerShell process that launched this script.
        $worktreeGitPointer = Join-Path $repoRoot ".git"
        if (Test-Path -LiteralPath $worktreeGitPointer -PathType Leaf) {
            $gitPointerLine = Get-Content -LiteralPath $worktreeGitPointer |
                Select-Object -First 1
            if ($gitPointerLine -match '^gitdir:\s*(.+)$') {
                $worktreeAdminDir = $Matches[1].Trim()
                if (![IO.Path]::IsPathRooted($worktreeAdminDir)) {
                    $worktreeAdminDir = Join-Path $repoRoot $worktreeAdminDir
                }
                $commonDirFile = Join-Path $worktreeAdminDir "commondir"
                if (Test-Path -LiteralPath $commonDirFile -PathType Leaf) {
                    $commonDirPath = (Get-Content -LiteralPath $commonDirFile -Raw).Trim()
                    if (![IO.Path]::IsPathRooted($commonDirPath)) {
                        $commonDirPath = Join-Path $worktreeAdminDir $commonDirPath
                    }
                    if (Test-Path -LiteralPath $commonDirPath -PathType Container) {
                        $primaryRoot = Split-Path -Parent (Resolve-Path -LiteralPath $commonDirPath).Path
                        if ($primaryRoot -notin $searchRoots) { $searchRoots += $primaryRoot }
                    }
                }
            }
        }

        $gitCommand = Get-Command git -CommandType Application -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($null -ne $gitCommand) {
            $gitCommonOutput = @(& $gitCommand.Source -C $repoRoot rev-parse --git-common-dir 2>$null)
            if ($LASTEXITCODE -eq 0 -and $gitCommonOutput.Count -gt 0) {
                $gitCommonDir = [string]$gitCommonOutput[0]
                if (![IO.Path]::IsPathRooted($gitCommonDir)) {
                    $gitCommonDir = Join-Path $repoRoot $gitCommonDir
                }
                if (Test-Path -LiteralPath $gitCommonDir -PathType Container) {
                    $primaryRoot = Split-Path -Parent (Resolve-Path -LiteralPath $gitCommonDir).Path
                    if ($primaryRoot -notin $searchRoots) { $searchRoots += $primaryRoot }
                }
            }
        }
        foreach ($searchRoot in $searchRoots) {
            foreach ($candidate in @(
                (Join-Path $searchRoot ".tools\telink\TlsrComSwireWriter\TLSR825xComFlasher.py"),
                (Join-Path $searchRoot ".tools\telink\TLSR825xComFlasher.py"),
                (Join-Path $searchRoot ".tools\TlsrComSwireWriter\TLSR825xComFlasher.py")
            )) {
                if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                    $resolved = (Resolve-Path -LiteralPath $candidate).Path
                    break
                }
            }
            if ($resolved -ne "") { break }
        }
        if ($resolved -eq "") {
            throw @"
TLSR825xComFlasher.py was not found.
Clone pvvx/TlsrComSwireWriter below .tools\telink, pass -ToolPath, or set
TLSR_COM_SWIRE_TOOL. See firmware\ports\telink\tlsr8359\README.md.
"@
        }
    }

    if ([IO.Path]::GetFileName($resolved) -ne "TLSR825xComFlasher.py") {
        throw "Unsupported UART-SWS backend: $resolved. This wrapper only accepts TLSR825xComFlasher.py because other pvvx tools reverse the wf/we command meanings."
    }

    $source = Get-Content -LiteralPath $resolved -Raw
    $hasRead = $source -match '(?is)add_parser\(\s*[''"]rf[''"]'
    $hasWriteWithErase = $source -match '(?is)add_parser\(\s*[''"]wf[''"].{0,400}?with\s+sectors\s+erases'
    $hasWriteWithoutErase = $source -match '(?is)add_parser\(\s*[''"]we[''"].{0,400}?without\s+sectors\s+erases'
    if (!$hasRead -or !$hasWriteWithErase -or !$hasWriteWithoutErase) {
        throw "TLSR825xComFlasher.py command semantics are not recognized. Expected rf=read, wf=write with sector erase, and we=write without erase: $resolved"
    }
    return $resolved
}

function Resolve-TlsrPython {
    param([string]$RequestedPython = "python")

    if (Test-Path -LiteralPath $RequestedPython -PathType Leaf) {
        return (Resolve-Path -LiteralPath $RequestedPython).Path
    }
    $command = Get-Command -Name $RequestedPython -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $command) {
        throw "Python executable not found: $RequestedPython"
    }
    return $command.Source
}

function Get-Tlsr8359BoardEntry {
    param(
        [Parameter(Mandatory = $true)][string]$Board,
        [switch]$RequireFlashContract
    )

    $entry = Find-Tlsr8359BoardEntry -Board $Board
    if ($null -eq $entry) {
        throw "Unknown TLSR8359 board ID '$Board'; it is not registered in firmware/boards/boards.json."
    }

    $physicalFlashProperty = $entry.PSObject.Properties["physicalFlashSizeBytes"]
    $verifiedWindowProperty = $entry.PSObject.Properties["verifiedFlashWindowSizeBytes"]
    if ($RequireFlashContract -and $null -eq $physicalFlashProperty -and
        $null -eq $verifiedWindowProperty) {
        throw "Board '$Board' has neither a verified physical Flash size nor a verified writable address window in boards.json; first flash remains blocked."
    }
    $firstFlashProperty = $entry.PSObject.Properties["firstFlash"]
    if ($RequireFlashContract -and $null -eq $firstFlashProperty) {
        throw "Board '$Board' has no firstFlash layout contract in boards.json; first flash remains blocked."
    }

    if ($RequireFlashContract) {
        $firstFlash = $firstFlashProperty.Value
        foreach ($name in @("format", "startAddress", "imageSizeBytes", "selectorAddress", "selectorId")) {
            if ($null -eq $firstFlash.PSObject.Properties[$name]) {
                throw "Board '$Board' firstFlash contract is missing '$name'."
            }
        }
        $format = [string]$firstFlash.format
        if ($format -notin @("full-flash-bin", "bounded-flash-window-bin")) {
            throw "Board '$Board' firstFlash format is unsupported by the current UART-SWS flow."
        }
        [uint64]$flashSize = if ($null -ne $physicalFlashProperty -and $null -ne $physicalFlashProperty.Value -and [uint64]$physicalFlashProperty.Value -gt 0) {
            $physicalFlashProperty.Value
        } else {
            $verifiedWindowProperty.Value
        }
        [uint64]$start = $firstFlash.startAddress
        [uint64]$imageSize = $firstFlash.imageSizeBytes
        if ($flashSize -eq 0 -or $flashSize -gt 0x1000000 -or $imageSize -eq 0 -or ($start + $imageSize) -gt $flashSize) {
            throw "Board '$Board' has an invalid TLSR8359 first-flash range in boards.json."
        }
        if ($format -eq "full-flash-bin" -and $null -eq $physicalFlashProperty) {
            throw "Board '$Board' cannot claim a full-Flash image without verified physicalFlashSizeBytes."
        }
        if ($format -eq "bounded-flash-window-bin" -and $null -eq $verifiedWindowProperty) {
            throw "Board '$Board' bounded first-flash image lacks verifiedFlashWindowSizeBytes."
        }
        if ([uint64]$firstFlash.selectorAddress -ge $flashSize -or
            [uint64]$firstFlash.selectorId -lt 1 -or [uint64]$firstFlash.selectorId -gt 255) {
            throw "Board '$Board' has an invalid first-flash selector contract."
        }
    }
    return $entry
}

function Find-Tlsr8359BoardEntry {
    param([Parameter(Mandatory = $true)][string]$Board)

    $canonicalBoard = ConvertTo-Tlsr8359CanonicalBoardId -Board $Board
    $repoRoot = Get-Tlsr8359RepoRoot
    $registryPath = Join-Path $repoRoot "firmware\boards\boards.json"
    $registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
    $property = $registry.boards.PSObject.Properties[$canonicalBoard]
    if ($null -eq $property) { return $null }
    $entry = $property.Value
    if ([string]$entry.port -ne "telink/tlsr8359") {
        throw "Board '$Board' belongs to port '$($entry.port)', not telink/tlsr8359."
    }
    return $entry
}

function Read-TlsrBackupInfo {
    param([Parameter(Mandatory = $true)][string]$Path)

    $values = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^([^=]+)=(.*)$') {
            $values[$Matches[1]] = $Matches[2]
        }
    }
    return $values
}

function ConvertFrom-TlsrIntegerText {
    param([Parameter(Mandatory = $true)][string]$Value)

    if ($Value -match '^0[xX]([0-9a-fA-F]+)$') {
        return [Convert]::ToUInt64($Matches[1], 16)
    }
    return [Convert]::ToUInt64($Value, 10)
}

function Get-Tlsr8359CanonicalBackupDirectory {
    param([string]$Root, [string]$Board)
    $marking = switch ($Board) {
        'hs_nowa213' { 'P213124-MF1-A' }
        'hs_nebular213' { 'HS-E2.13ME-V1.0-M' }
        default { $Board }
    }
    return Join-Path $Root ("factory_backups\tlsr8359\{0}" -f $marking)
}

function Get-VerifiedTlsrBackup {
    param(
        [Parameter(Mandatory = $true)][string]$StockBackup,
        [Parameter(Mandatory = $true)][string]$Board,
        [Parameter(Mandatory = $true)][uint64]$ExpectedSizeBytes,
        [switch]$AllowVerifiedWindow
    )

    if (!(Test-Path -LiteralPath $StockBackup)) {
        throw "Stock backup path not found: $StockBackup"
    }
    $item = Get-Item -LiteralPath $StockBackup
    $backupDir = if ($item.PSIsContainer) { $item.FullName } else { $item.Directory.FullName }
    $requestedBoard = ConvertTo-Tlsr8359CanonicalBoardId -Board $Board
    $canonicalPath = Join-Path $backupDir "stock-flash.bin"
    if ($item.PSIsContainer -or $item.Name -eq "stock-flash.bin") {
        foreach ($root in @(Get-Tlsr8359BackupRoots)) {
            $canonicalDir = Get-Tlsr8359CanonicalBackupDirectory -Root $root -Board $requestedBoard
            if (!(Test-Path -LiteralPath $canonicalDir -PathType Container) -or
                (Resolve-Path -LiteralPath $canonicalDir).Path -ne $backupDir -or
                !(Test-Path -LiteralPath $canonicalPath -PathType Leaf)) { continue }
            # This is a promoted, immutable double-read baseline, NOT a newly
            # verified live read. Exact path + board-pinned hash + registry range
            # are all required; an arbitrary single-file copy is not accepted.
            $target = Get-Tlsr8359ResearchTarget -Board $requestedBoard
            $entry = Get-Tlsr8359BoardEntry -Board $requestedBoard -RequireFlashContract
            $contract = $entry.firstFlash
            if ([uint64]$contract.startAddress -ne 0 -or
                [uint64]$contract.imageSizeBytes -ne $ExpectedSizeBytes -or
                ($contract.format -eq "bounded-flash-window-bin" -and !$AllowVerifiedWindow) -or
                (Get-Item -LiteralPath $canonicalPath).Length -ne [int64]$ExpectedSizeBytes) {
                throw "Canonical TLSR8359 stock backup range/size gate failed: $canonicalPath"
            }
            $hash = (Get-FileHash -LiteralPath $canonicalPath -Algorithm SHA256).Hash.ToUpperInvariant()
            if ($hash -ne $target.BaselineFlashSha256) {
                throw "Canonical TLSR8359 stock backup board-bound SHA-256 gate failed: $canonicalPath"
            }
            return [pscustomobject]@{
                Directory = $backupDir
                InfoPath = $null
                FlashPath = (Resolve-Path -LiteralPath $canonicalPath).Path
                VerifyPath = $null
                FlashSha256 = $hash
                Format = "canonical-promoted-double-read-baseline"
                Info = @{ verification = "promoted-pinned-baseline"; board_id = $requestedBoard }
            }
        }
    }
    $infoPath = Join-Path $backupDir "backup-info.txt"
    $flashPath = Join-Path $backupDir "tlsr8359-flash.bin"
    $verifyPath = Join-Path $backupDir "tlsr8359-flash-verify.bin"
    foreach ($required in @($infoPath, $flashPath, $verifyPath)) {
        if (!(Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "Incomplete TLSR8359 stock backup; missing: $required"
        }
    }

    $info = Read-TlsrBackupInfo -Path $infoPath
    foreach ($key in @("target", "board_id", "start_address", "size_bytes", "flash_sha256", "verify_sha256", "verification", "capacity_status")) {
        if (!$info.ContainsKey($key)) { throw "Incomplete TLSR8359 backup metadata; missing '$key': $infoPath" }
    }
    if ($info.target -ne "TLSR8359") {
        throw "Backup target is not TLSR8359: $infoPath"
    }
    [uint64]$captureSize = ConvertFrom-TlsrIntegerText -Value $info.size_bytes
    $captureEntry = Get-Tlsr8359BoardEntry -Board $requestedBoard -RequireFlashContract
    # A complete physical capture preserves more than the bounded write image.
    # Keep the original metadata and verify BOTH whole files; never fabricate
    # a shorter backup or expand erase/write beyond the registered container.
    $largerPhysicalCapture = $AllowVerifiedWindow -and
        $captureSize -gt $ExpectedSizeBytes -and
        $captureSize -eq [uint64]$captureEntry.physicalFlashSizeBytes -and
        $ExpectedSizeBytes -eq [uint64]$captureEntry.firstFlash.imageSizeBytes -and
        $info.capacity_status -eq "physical-flash-verified-board-registry" -and
        $info.ContainsKey("halves_identical") -and $info.halves_identical -eq "false"
    if ((ConvertFrom-TlsrIntegerText -Value $info.start_address) -ne 0 -or
        ($captureSize -ne $ExpectedSizeBytes -and !$largerPhysicalCapture)) {
        throw "TLSR8359 backup range does not match the board-qualified full Flash size."
    }
    if ((Get-Item -LiteralPath $flashPath).Length -ne [int64]$captureSize -or
        (Get-Item -LiteralPath $verifyPath).Length -ne [int64]$captureSize) {
        throw "TLSR8359 stock backup file size gate failed."
    }
    $flashHash = (Get-FileHash -LiteralPath $flashPath -Algorithm SHA256).Hash.ToUpperInvariant()
    $verifyHash = (Get-FileHash -LiteralPath $verifyPath -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($flashHash -ne $info.flash_sha256.ToUpperInvariant() -or
        $verifyHash -ne $info.verify_sha256.ToUpperInvariant() -or $flashHash -ne $verifyHash) {
        throw "TLSR8359 stock backup SHA-256 gate failed: $backupDir"
    }

    $capacityAccepted = $info.capacity_status -eq "physical-flash-verified-board-registry" -or
        ($AllowVerifiedWindow -and $info.capacity_status -eq "non-mirrored-512k-window-with-upper-data")
    $backupBoard = ConvertTo-Tlsr8359CanonicalBoardId -Board $info.board_id
    $requestedBoard = ConvertTo-Tlsr8359CanonicalBoardId -Board $Board
    $currentBackupAccepted = $backupBoard -eq $requestedBoard -and $capacityAccepted
    $target = Get-Tlsr8359ResearchTarget -Board $Board
    $schema = if ($info.ContainsKey("schema")) { $info.schema } else { "" }
    $legacyBaselineAccepted = $schema -eq "inktag-tlsr8359-backup-v1" -and
        $info.board_id -eq "unassigned" -and
        $info.capacity_status -eq "caller-specified-read-window-unverified" -and
        $flashHash -eq $target.BaselineFlashSha256
    # The qualified Nebular double-read predates the final short board ID.
    # Accept only that immutable evidence hash; this does not restore the old
    # ID as a callable board alias anywhere in the public tool surface.
    $historicalNebularEvidenceAccepted =
        $schema -eq "inktag-tlsr8359-backup-v2" -and
        $info.board_id -eq "hanshow_nebular_420r_n" -and
        $requestedBoard -eq "hs_nebular420" -and $capacityAccepted -and
        $flashHash -eq $target.BaselineFlashSha256
    if ($info.verification -ne "matched" -or
        (!$currentBackupAccepted -and !$legacyBaselineAccepted -and
         !$historicalNebularEvidenceAccepted)) {
        throw "TLSR8359 backup is not qualified for board '$Board'; require exact board metadata, or the documented legacy baseline hash, plus a matched second read and qualified range evidence."
    }

    return [pscustomobject]@{
        Directory = $backupDir
        InfoPath = $infoPath
        FlashPath = $flashPath
        VerifyPath = $verifyPath
        FlashSha256 = $flashHash
        Format = if ($legacyBaselineAccepted) { "legacy-documented-baseline" } elseif ($historicalNebularEvidenceAccepted) { "historical-board-bound" } else { "board-bound" }
        Info = $info
    }
}

function Find-VerifiedTlsrBackup {
    param(
        [Parameter(Mandatory = $true)][string]$Board,
        [Parameter(Mandatory = $true)][uint64]$ExpectedSizeBytes,
        [switch]$AllowVerifiedWindow
    )

    $roots = @(Get-Tlsr8359BackupRoots)
    $target = Get-Tlsr8359ResearchTarget -Board $Board
    $candidates = @()
    foreach ($root in $roots) {
        $canonicalDir = Get-Tlsr8359CanonicalBackupDirectory -Root $root -Board $target.BoardId
        if (Test-Path -LiteralPath (Join-Path $canonicalDir "stock-flash.bin") -PathType Leaf) {
            # Fail loudly for a corrupted promoted baseline rather than silently
            # falling back to an older ignored capture.
            $arguments = @{ StockBackup = $canonicalDir; Board = $Board; ExpectedSizeBytes = $ExpectedSizeBytes }
            if ($AllowVerifiedWindow) { $arguments["AllowVerifiedWindow"] = $true }
            $null = Get-VerifiedTlsrBackup @arguments
            Write-Host ("Auto-selected canonical stock backup: {0}" -f $canonicalDir)
            return $canonicalDir
        }
        $searchRoot = Join-Path $root ".local\backups"
        if (Test-Path -LiteralPath $searchRoot -PathType Container) {
            $candidates += @(Get-ChildItem -LiteralPath $searchRoot -Filter "backup-info.txt" -File -Recurse -ErrorAction SilentlyContinue)
        }
    }
    $candidates = @($candidates | Sort-Object LastWriteTimeUtc -Descending)
    foreach ($candidate in $candidates) {
        try {
            $arguments = @{
                StockBackup = $candidate.Directory.FullName
                Board = $Board
                ExpectedSizeBytes = $ExpectedSizeBytes
            }
            if ($AllowVerifiedWindow) { $arguments["AllowVerifiedWindow"] = $true }
            $backup = Get-VerifiedTlsrBackup @arguments
            if ($backup.FlashSha256 -eq $target.BaselineFlashSha256) {
                Write-Host ("Auto-selected verified stock backup: {0}" -f $backup.Directory)
                return $backup.Directory
            }
        } catch {
            # Ignore unrelated, incomplete and test backups while searching.
        }
    }
    throw "No verified baseline stock backup for '$Board' was found in canonical backups or checkout/primary .local/backups. Pass -StockBackup explicitly or run backup.ps1 first."
}
