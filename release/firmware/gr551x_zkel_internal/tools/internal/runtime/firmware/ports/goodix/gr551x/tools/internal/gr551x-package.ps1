function Test-InkTagGr551xMetadataLayout {
    param([byte[]]$Metadata, [int]$ExpectedBoardSelector,
          [ValidateSet(4, 5, 6)][int]$LayoutVersion)
    if ($Metadata.Length -lt 0x2030) { return $false }
    if ($ExpectedBoardSelector -notin @(0, 1)) { return $false }
    [uint32]$LayoutMarker = switch ($LayoutVersion) { 6 { 0x364C5247 } 5 { 0x354C5247 } 4 { 0x344C5247 } }
    $SeedOffset = 0xF00
    $ExpectedBoardId = if ($ExpectedBoardSelector -eq 1) { 2 } else { 3 }
    if ([System.Text.Encoding]::ASCII.GetString($Metadata, $SeedOffset, 4) -ne "IBP1" -or
        $Metadata[$SeedOffset + 4] -ne 1 -or $Metadata[$SeedOffset + 5] -ne 0x55 -or
        $Metadata[$SeedOffset + 6] -ne $ExpectedBoardId -or $Metadata[$SeedOffset + 7] -ne 0) {
        return $false
    }
    [uint32]$Hash = 2166136261
    for ($Index = 0; $Index -lt 8; $Index++) {
        $Hash = [uint32]((([uint64]$Hash -bxor $Metadata[$SeedOffset + $Index]) * 16777619) -band 4294967295)
    }
    return [System.BitConverter]::ToUInt32($Metadata, $SeedOffset + 8) -eq $Hash -and
        [System.BitConverter]::ToUInt32($Metadata, 8) -eq 0x01004000 -and
        [System.BitConverter]::ToUInt16($Metadata, 0x2000) -eq 0x4744 -and
        [System.BitConverter]::ToUInt16($Metadata, 0x2002) -eq 1 -and
        [System.BitConverter]::ToUInt32($Metadata, 0x200C) -eq 0x01020000 -and
        [System.BitConverter]::ToUInt32($Metadata, 0x2028) -eq $LayoutMarker -and
        [System.BitConverter]::ToUInt32($Metadata, 0x202C) -eq ([uint32]::MaxValue -bxor $LayoutMarker)
}

# V4 recognition is only for an installed InkTag maintenance source. It is
# never admission for a new Full image or an OTA across the moved image base.
function Test-InkTagGr551xMetadataV4 {
    param([byte[]]$Metadata, [int]$ExpectedBoardSelector)
    return Test-InkTagGr551xMetadataLayout -Metadata $Metadata -ExpectedBoardSelector $ExpectedBoardSelector -LayoutVersion 4
}

function Test-InkTagGr551xMetadataV5 {
    param([byte[]]$Metadata, [int]$ExpectedBoardSelector)
    return Test-InkTagGr551xMetadataLayout -Metadata $Metadata -ExpectedBoardSelector $ExpectedBoardSelector -LayoutVersion 5
}

function Test-InkTagGr551xMetadataV6 {
    param([byte[]]$Metadata, [int]$ExpectedBoardSelector)
    return Test-InkTagGr551xMetadataLayout -Metadata $Metadata -ExpectedBoardSelector $ExpectedBoardSelector -LayoutVersion 6
}

function Get-InkTagGr551xPackage {
    param(
        [string]$JLinkExe = "",
        [string]$Snr = "",
        [int]$FrequencyHz = 500000,
        [string]$RepoRoot = "",
        [switch]$RequireNoResetAttach,
        [switch]$LeaveHalted,
        [switch]$McuOnly,
        [switch]$CaptureWindow,
        [switch]$ReadProductionCache,
        [string]$EvidenceDir = ""
    )

    if ($FrequencyHz -lt 1000) {
        throw "FrequencyHz must be >= 1000"
    }
    if ($RepoRoot -eq "") {
        $RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..\..\..\..\..")).Path
    }

    . (Join-Path $RepoRoot "tools\jlink-tools.ps1")
    $ResolvedJLink = Resolve-InkTagJLinkTool -ToolName "JLink.exe" -ExplicitPath $JLinkExe -RepoRoot $RepoRoot
    Assert-InkTagJLink788j -JLinkExe $ResolvedJLink

    $SpeedKHz = [Math]::Max(1, [int][Math]::Ceiling($FrequencyHz / 1000.0))
    $WorkDir = Join-Path ([System.IO.Path]::GetTempPath()) ("inktag-gr551x-package-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
    try {
        $CommandFile = Join-Path $WorkDir "read-package.jlink"
        $StdoutFile = Join-Path $WorkDir "stdout.log"
        $StderrFile = Join-Path $WorkDir "stderr.log"
        $Commands = @(
            "si SWD",
            "speed $SpeedKHz",
            "SWDSelect",
            "connect",
            "halt",
            "exec SetRestartOnClose = 0",
            "mem32 0xE000ED00 1",
            # This is the SDK efuse_trim RAM cache, NOT a live silicon register.
            # sys_device_package_get first checks its 0x4744 validity marker.
            "mem8 0x00803214 1",
            "mem16 0x0080322E 1"
        )
        $FactorySnapshot = Join-Path $WorkDir 'factory-cache.bin'
        if ($ReadProductionCache) {
            # Capture while this very connection still owns the halted MCU.
            # A later Commander attach may see a reset/cleared ROM cache.
            $Commands += ('savebin "'+$FactorySnapshot+'" 0x00803212 0x3c')
        }
        if (!$LeaveHalted) { $Commands += "go" }
        $Commands += "qc"
        $Commands | Set-Content -LiteralPath $CommandFile -Encoding ASCII

        $Arguments = @(
            "-NoGui", "1",
            "-Device", "Cortex-M4",
            "-If", "SWD",
            "-Speed", "$SpeedKHz",
            "-AutoConnect", "1",
            "-ExitOnError", "1",
            "-CommanderScript", ('"'+$CommandFile+'"')
        )
        if ($Snr -ne "") { $Arguments = @("-SelectEmuBySN", $Snr) + $Arguments }

        if($CaptureWindow) {
            . (Join-Path $RepoRoot 'firmware/ports/goodix/common/tools/internal/goodix-capture-window.ps1')
            $NativeExit=Invoke-InkTagGoodixCaptureWindow -Tool $ResolvedJLink -Arguments $Arguments `
                -Stdout $StdoutFile -Stderr $StderrFile -EvidenceDir $WorkDir -Gr551xPackageCache:(!$McuOnly)
        } else {
            $Process = Start-Process -FilePath $ResolvedJLink -WindowStyle Hidden -PassThru -Wait `
                -RedirectStandardOutput $StdoutFile -RedirectStandardError $StderrFile `
                -ArgumentList $Arguments
            $NativeExit=$Process.ExitCode
        }
        $Output = ((Get-Content -LiteralPath $StdoutFile -Raw -ErrorAction SilentlyContinue) + "`n" +
                   (Get-Content -LiteralPath $StderrFile -Raw -ErrorAction SilentlyContinue))
        if($CaptureWindow) { Assert-InkTagGoodixCaptureResult -Output $Output }
        if ($NativeExit -ne 0 -or $Output -match "(?i)Could not read memory") {
            throw "Unable to read the Goodix package ID over SWD. Check J-Link, GND, VTref and the selected probe."
        }
        $ResetFallbackObserved = $Output -match "(?i)Connect fallback:.*Reset|Reset via Reset pin\s*&\s*Connect"
        if ($RequireNoResetAttach -and $ResetFallbackObserved) {
            throw "J-Link read the package ID only through a RESET fallback. This does not satisfy the direct running-state attach gate."
        }
        if ($Output -notmatch "(?im)00803214\s*=\s*([0-9A-Fa-f]{2})") {
            throw "J-Link connected, but the Goodix package byte at 0x00803214 was not returned. Refusing to guess the MCU."
        }

        $PackageRaw = [Convert]::ToByte($Matches[1], 16)
        if($Output -notmatch '(?im)E000ED00\s*=\s*([0-9A-Fa-f]{8})') { throw 'Missing Cortex-M4 CPUID; refusing to guess the MCU' }
        $CpuId=[Convert]::ToUInt32($Matches[1],16)
        if(($CpuId -band 4278255600L) -ne 1090568768L) { throw 'Non-Cortex-M4 identity refused' }
        $CacheValid=$Output -match '(?im)0080322E\s*=\s*4744(?:\s|$)'
        if(!$CacheValid) {
            if(!$McuOnly) { throw 'MCU package cache is not initialized; exact chip/capacity unconfirmed; no Flash operation' }
            return [pscustomobject]@{
                Chip='UNIDENTIFIED-MCU';ChipConfirmed=$false;CpuId=('0x{0:X8}'-f $CpuId)
                PackageId=$null;PackageRaw=('0x{0:X2}'-f $PackageRaw);FlashSizeKB=$null
                IdentityReason='SDK eFuse cache is not initialized; no valid MCU package result'
                ResetFallbackObserved=[bool]$ResetFallbackObserved
            }
        }
        $PackageId = $PackageRaw -band 0x1F
        $Known = @{
            1 = @{ Chip = "gr5515"; Name = "GR5515RGBD"; FlashSizeKB = $null }
            2 = @{ Chip = "gr5515"; Name = "GR5515GGBD"; FlashSizeKB = $null }
            3 = @{ Chip = "gr5515"; Name = "GR5515IGND"; FlashSizeKB = 1024 }
            4 = @{ Chip = "gr5515"; Name = "GR5515I0ND"; FlashSizeKB = $null; FactoryConfig = "gr5515" }
            5 = @{ Chip = "gr5513"; Name = "GR5513BEND"; FlashSizeKB = 512; FactoryConfig = "gr5513" }
            6 = @{ Chip = "gr5515"; Name = "GR5515BEND"; FlashSizeKB = 512 }
            7 = @{ Chip = "gr5513"; Name = "GR5513BENDU"; FlashSizeKB = 512; FactoryConfig = "gr5513" }
            8 = @{ Chip = "gr5515"; Name = "GR5515I0NDA"; FlashSizeKB = $null }
            9 = @{ Chip = "gr5515"; Name = "GR5515IENDU"; FlashSizeKB = 512 }
        }
        if (!$Known.ContainsKey([int]$PackageId)) {
            if(!$McuOnly) { throw "Unknown MCU package ID $PackageId; no qualified Flash operation" }
            return [pscustomobject]@{
                Chip='UNIDENTIFIED-MCU';ChipConfirmed=$false;CpuId=('0x{0:X8}'-f $CpuId)
                PackageId=$null;PackageRaw=('0x{0:X2}'-f $PackageRaw);FlashSizeKB=$null
                IdentityReason="Unknown MCU package value $PackageId; not defined by the pinned SDK"
                ResetFallbackObserved=[bool]$ResetFallbackObserved
            }
        }
        if(!$McuOnly -and $PackageId -notin @(4,5,7)) { throw "MCU package $PackageId is identified but not qualified for the maintained Flash operation" }
        $Entry = $Known[[int]$PackageId]
        $FactorySnapshotSha = ''
        if($ReadProductionCache) {
            $hasher=[Security.Cryptography.SHA256]::Create()
            try {$FactorySnapshotSha=[BitConverter]::ToString($hasher.ComputeHash([IO.File]::ReadAllBytes($FactorySnapshot))).Replace('-','').ToLowerInvariant()}
            finally {$hasher.Dispose()}
        }
        return [pscustomobject]@{
            PackageId = [int]$PackageId
            PackageName = [string]$Entry.Name
            Chip = [string]$Entry.Chip
            FlashSizeKB = $(if(!$McuOnly -and $PackageId -eq 4){1024}else{$Entry.FlashSizeKB})
            ChipConfirmed = $true
            CpuId = ('0x{0:X8}'-f $CpuId)
            PackageRaw = ('0x{0:X2}'-f $PackageRaw)
            IdentitySource = 'SDK eFuse cache with valid 0x4744 marker'
            FactoryConfig = [string]$Entry.FactoryConfig
            JLinkExe = $ResolvedJLink
            ProbeSerial = $(if($Output -match '(?im)^\s*S/N:\s*(\d+)\s*$'){$Matches[1]}else{''})
            ResetFallbackObserved = [bool]$ResetFallbackObserved
            FactorySnapshotFile = $(if($ReadProductionCache){Join-Path $EvidenceDir 'factory-cache.bin'}else{''})
            FactorySnapshotSha256 = $FactorySnapshotSha
        }
    }
    finally {
        if ($EvidenceDir) {
            . (Join-Path $RepoRoot 'firmware/ports/goodix/common/tools/internal/goodix-backup-archive.ps1')
            Assert-InkTagGoodixArchiveDirectory -Path $EvidenceDir
            if (Test-Path -LiteralPath $EvidenceDir) { throw 'Identity evidence collision; refusing overwrite' }
            New-Item -ItemType Directory -Path $EvidenceDir -Force | Out-Null
            # Retain exact commands/stdout/stderr even on package refusal.
            Get-ChildItem -LiteralPath $WorkDir -File | Copy-Item -Destination $EvidenceDir
        }
        $ExactWorkDir = [IO.Path]::GetFullPath($WorkDir)
        $ExactTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
        if (!$ExactWorkDir.StartsWith($ExactTempRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
            (Split-Path $ExactWorkDir -Leaf) -notmatch '^inktag-gr551x-package-[0-9a-f]{32}$' -or
            ((Get-Item -LiteralPath $ExactWorkDir).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Unsafe temporary package directory; preserving it instead of deleting'
        }
        Remove-Item -LiteralPath $ExactWorkDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Get-InkTagGr551xInstallation {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Detected,
        [string]$Snr = "",
        [int]$FrequencyHz = 500000,
        [string]$RepoRoot = "",
        [string]$GProgrammerDir = "",
        [string]$ConsoleExe = "",
        [switch]$PreserveHaltForProgram,
        [ValidateRange(0, 255)]
        [int]$ExpectedBoardSelector = 0
    )

    if ($RepoRoot -eq "") {
        $RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..\..\..\..\..")).Path
    }
    if ($GProgrammerDir -eq "") {
        $GProgrammerDir = Join-Path $RepoRoot ".tools\gprogrammer\_._"
    }
    if ($ConsoleExe -eq "") {
        $ConsoleExe = Join-Path $GProgrammerDir "GR5xxx_console.exe"
    }
    if (!(Test-Path -LiteralPath $ConsoleExe -PathType Leaf)) {
        throw "GR5xxx_console.exe not found: $ConsoleExe"
    }

    $ConsolePath = (Resolve-Path -LiteralPath $ConsoleExe).Path
    $GProgrammerRoot = (Resolve-Path -LiteralPath $GProgrammerDir).Path
    $DescriptorAddress = if ($Detected.FlashSizeKB -eq 512) { 0x0107B000 } else { 0x010FB000 }
    $WorkDir = Join-Path ([System.IO.Path]::GetTempPath()) ("inktag-gr551x-install-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
    try {
        $MetadataFile = Join-Path $WorkDir "metadata.bin"
        $DescriptorFile = Join-Path $WorkDir "descriptor.bin"

        # Generic Cortex-M4 J-Link savebin reads do not drive the GR551x
        # external-Flash controller and return repeated ROM values while the
        # application is sleeping. GProgrammer's readFlash command uses the
        # vendor's read-only SWD path and returns the physical Flash bytes.
        $ResetExitCode = -1
        $ResetOutput = @()
        Push-Location -LiteralPath $GProgrammerRoot
        try {
            $DeviceOutput = @(& $ConsolePath device 1 2>&1)
            if ($LASTEXITCODE -ne 0) {
                throw "GProgrammer could not enumerate J-Link probes"
            }
            $DeviceText = ($DeviceOutput | ForEach-Object { $_.ToString() }) -join "`n"
            $DeviceRows = @([regex]::Matches(
                $DeviceText, '(?m)^\s*(\d+)\s+.+?\s+(\d{6,})\s*$'))
            if ($Snr -ne "") {
                $SelectedRows = @($DeviceRows | Where-Object { $_.Groups[2].Value -eq $Snr })
                if ($SelectedRows.Count -ne 1) {
                    throw "GProgrammer did not find the selected J-Link S/N $Snr"
                }
                $JLinkIndex = $SelectedRows[0].Groups[1].Value
                $SelectedSnr = $SelectedRows[0].Groups[2].Value
            } else {
                if ($DeviceRows.Count -ne 1) {
                    throw "GProgrammer found $($DeviceRows.Count) J-Link probes; pass -Snr to select one"
                }
                $JLinkIndex = $DeviceRows[0].Groups[1].Value
                $SelectedSnr = $DeviceRows[0].Groups[2].Value
            }

            $ChipName = $Detected.Chip.ToUpperInvariant()
            try {
                $MetadataOutput = @(& $ConsolePath readFlash --chip $ChipName `
                    --flashSize $Detected.FlashSizeKB --address 0x01000000 `
                    --size 8240 --save $MetadataFile --jlink $JLinkIndex 2>&1)
                $MetadataExitCode = $LASTEXITCODE
                $DescriptorOutput = @(& $ConsolePath readFlash --chip $ChipName `
                    --flashSize $Detected.FlashSizeKB `
                    --address ("0x{0:X8}" -f $DescriptorAddress) `
                    --size 32 --save $DescriptorFile --jlink $JLinkIndex 2>&1)
                $DescriptorExitCode = $LASTEXITCODE
            } finally {
                # readFlash leaves GR551x halted. Restore ordinary application
                # service before returning from a read-only detector, even
                # after a read failure. A Full caller immediately erases and
                # programs this already-verified target, so keep it halted:
                # an intermediate vendor reset can hang here and can also let
                # the application re-enter SLEEP before the erase reconnect.
                if ($PreserveHaltForProgram) {
                    $ResetExitCode = 0
                } else {
                    # This normal reset does not erase or write Flash, option
                    # bytes, OTP, eFuse or security state.
                    $ResetOutput = @(& $ConsolePath reset 0 $SelectedSnr 2>&1)
                    $ResetExitCode = $LASTEXITCODE
                }
            }
        } finally {
            Pop-Location
        }
        if ($ResetExitCode -ne 0) {
            $ResetDetail = (@($ResetOutput) |
                ForEach-Object { $_.ToString().Trim() } |
                Where-Object { $_ -ne "" } |
                Select-Object -Last 1) -join ""
            throw "GProgrammer readFlash completed but target reset failed: $ResetDetail"
        }
        if ($MetadataExitCode -ne 0 -or $DescriptorExitCode -ne 0 -or
            !(Test-Path -LiteralPath $MetadataFile) -or !(Test-Path -LiteralPath $DescriptorFile)) {
            $ReadDetail = (@($MetadataOutput + $DescriptorOutput) |
                ForEach-Object { $_.ToString().Trim() } |
                Where-Object { $_ -ne "" } |
                Select-Object -Last 1) -join ""
            return [pscustomobject]@{
                IsInkTag = $false
                Reason = "GProgrammer readFlash failed: $ReadDetail"
            }
        }

        $Metadata = [System.IO.File]::ReadAllBytes($MetadataFile)
        $Descriptor = [System.IO.File]::ReadAllBytes($DescriptorFile)
        if ($Metadata.Length -ne 0x2030 -or $Descriptor.Length -ne 0x20) {
            return [pscustomobject]@{ IsInkTag = $false; Reason = "installation metadata length is invalid" }
        }
        $ExpectedChipCode = if ($Detected.Chip -eq "gr5513") { 0x13 } else { 0x15 }
        $ExpectedConfig = if ($Detected.Chip -eq "gr5513") { 1 } else { 2 }
        $ExpectedFlashBytes = $Detected.FlashSizeKB * 1024
        $BootLoad = [System.BitConverter]::ToUInt32($Metadata, 8)
        $AppPattern = [System.BitConverter]::ToUInt16($Metadata, 0x2000)
        $AppLoad = [System.BitConverter]::ToUInt32($Metadata, 0x200C)
        if (Test-InkTagGr551xMetadataV6 -Metadata $Metadata -ExpectedBoardSelector $ExpectedBoardSelector) {
            return [pscustomobject]@{
                IsInkTag = $true
                Reason = "matching InkTag v6 metadata selector and signed layout marker"
            }
        }
        if (Test-InkTagGr551xMetadataV5 -Metadata $Metadata -ExpectedBoardSelector $ExpectedBoardSelector) {
            return [pscustomobject]@{
                IsInkTag = $true
                Reason = "matching InkTag v5 metadata selector and signed layout marker"
            }
        }
        if (Test-InkTagGr551xMetadataV4 -Metadata $Metadata -ExpectedBoardSelector $ExpectedBoardSelector) {
            return [pscustomobject]@{
                IsInkTag = $true
                Reason = "matching InkTag v4 metadata selector and signed layout marker"
            }
        }
        $Magic = [System.Text.Encoding]::ASCII.GetString($Descriptor, 0, 8)
        [uint32]$Crc = [uint32]::MaxValue
        [uint32]$Polynomial = [Convert]::ToUInt32("EDB88320", 16)
        for ($Index = 0; $Index -lt 28; $Index++) {
            $Crc = [uint32]($Crc -bxor [uint32]$Descriptor[$Index])
            for ($Bit = 0; $Bit -lt 8; $Bit++) {
                if (($Crc -band 1) -ne 0) {
                    $Crc = [uint32](($Crc -shr 1) -bxor $Polynomial)
                } else {
                    $Crc = [uint32]($Crc -shr 1)
                }
            }
        }
        $Crc = [uint32]($Crc -bxor [uint32]::MaxValue)
        $StoredCrc = [System.BitConverter]::ToUInt32($Descriptor, 28)
        $Matches = ($BootLoad -eq 0x01004000) -and ($AppPattern -eq 0x4744) -and
                   ($AppLoad -eq 0x01020000) -and ($Magic -eq "IKGRFAC1") -and
                   ($Descriptor[8] -eq 1) -and ($Descriptor[9] -eq $ExpectedChipCode) -and
                   ($Descriptor[10] -eq $ExpectedConfig) -and
                   ($Descriptor[11] -eq $ExpectedBoardSelector) -and
                   ($StoredCrc -eq $Crc) -and
                   ([System.BitConverter]::ToUInt32($Descriptor, 12) -eq $ExpectedFlashBytes)
        $Reason = if ($Matches) {
            "matching InkTag factory descriptor and executable layout"
        } else {
            ("Flash layout mismatch at descriptor 0x{0:X8}: boot=0x{1:X8}, appPattern=0x{2:X4}, " +
             "app=0x{3:X8}, magic='{4}', format={5}, chip=0x{6:X2}, config={7}, board={8}, " +
             "flashBytes={9}, crc=0x{10:X8}/0x{11:X8}") -f
                $DescriptorAddress, $BootLoad, $AppPattern, $AppLoad, $Magic,
                $Descriptor[8], $Descriptor[9], $Descriptor[10], $Descriptor[11],
                [System.BitConverter]::ToUInt32($Descriptor, 12), $StoredCrc, $Crc
        }
        return [pscustomobject]@{
            IsInkTag = $Matches
            Reason = $Reason
        }
    }
    finally {
        $ResolvedTemp = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
        $ResolvedWorkDir = [System.IO.Path]::GetFullPath($WorkDir)
        if ($ResolvedWorkDir.StartsWith($ResolvedTemp, [System.StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $ResolvedWorkDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}
