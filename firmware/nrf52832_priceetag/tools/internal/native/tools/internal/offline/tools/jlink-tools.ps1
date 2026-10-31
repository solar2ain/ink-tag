function Get-InkTagJLinkCandidateDirectories {
    param([string]$RepoRoot = "")

    $directories = [System.Collections.Generic.List[string]]::new()
    if ($env:INKTAG_JLINK_HOME) { $directories.Add($env:INKTAG_JLINK_HOME) }

    $programFiles = [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFiles)
    $programFilesX86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
    foreach ($root in @($programFiles, $programFilesX86)) {
        if ($root) {
            $directories.Add((Join-Path $root "SEGGER\JLink_V788j"))
            $directories.Add((Join-Path $root "SEGGER\JLink"))
        }
    }

    if ($RepoRoot) {
        $directories.Add((Join-Path $RepoRoot ".tools\jlink\7.88j"))
        $directories.Add((Join-Path $RepoRoot ".tools\jlink"))
    }

    foreach ($registryPath in @("HKLM:\SOFTWARE\SEGGER\J-Link", "HKLM:\SOFTWARE\WOW6432Node\SEGGER\J-Link")) {
        $entry = Get-ItemProperty -LiteralPath $registryPath -ErrorAction SilentlyContinue
        if ($null -ne $entry -and $entry.InstallPath) { $directories.Add([string]$entry.InstallPath) }
    }

    return @($directories | Where-Object { $_ } | Select-Object -Unique)
}

function Resolve-InkTagJLinkTool {
    param(
        [ValidateSet("JLink.exe", "JFlash.exe")]
        [string]$ToolName = "JLink.exe",
        [string]$ExplicitPath = "",
        [string]$RepoRoot = ""
    )

    if ($ExplicitPath) {
        if (!(Test-Path -LiteralPath $ExplicitPath -PathType Leaf)) {
            throw "SEGGER tool not found: $ExplicitPath"
        }
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    foreach ($directory in Get-InkTagJLinkCandidateDirectories -RepoRoot $RepoRoot) {
        $candidate = Join-Path $directory $ToolName
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $command = Get-Command $ToolName -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw "$ToolName was not found. Install SEGGER J-Link Software and Documentation Pack 7.88j, or set INKTAG_JLINK_HOME."
}

function Assert-InkTagJLink788j {
    param([Parameter(Mandatory = $true)][string]$JLinkExe)

    $verified = Get-Variable -Name InkTagVerifiedJLink788j -Scope Script -ErrorAction SilentlyContinue
    if ($null -ne $verified -and $verified.Value -eq $JLinkExe) { return }
    $temporary = Join-Path ([System.IO.Path]::GetTempPath()) ("inktag-jlink-version-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $temporary | Out-Null
    try {
        $commandFile = Join-Path $temporary "version.jlink"
        $stdoutFile = Join-Path $temporary "stdout.log"
        $stderrFile = Join-Path $temporary "stderr.log"
        Set-Content -LiteralPath $commandFile -Encoding ASCII -Value "exit"
        $process = Start-Process -FilePath $JLinkExe -WindowStyle Hidden -PassThru -Wait `
            -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile `
            -ArgumentList @("-NoGui", "1", "-AutoConnect", "0", "-CommanderScript", $commandFile)
        $text = ((Get-Content -LiteralPath $stdoutFile -Raw -ErrorAction SilentlyContinue) + "`n" +
                 (Get-Content -LiteralPath $stderrFile -Raw -ErrorAction SilentlyContinue))
        if ($process.ExitCode -ne 0 -or $text -notmatch "(?i)J-Link Commander V7\.88j") {
            throw "InkTag requires J-Link Commander 7.88j. Actual executable: $JLinkExe"
        }
        $script:InkTagVerifiedJLink788j = $JLinkExe
    }
    finally {
        Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Get-InkTagJLinkDeviceRoot {
    $isLinuxValue = Get-Variable -Name IsLinux -ErrorAction SilentlyContinue
    $isMacValue = Get-Variable -Name IsMacOS -ErrorAction SilentlyContinue
    if ($null -ne $isLinuxValue -and $isLinuxValue.Value) { return (Join-Path $HOME ".config/SEGGER/JLinkDevices") }
    if ($null -ne $isMacValue -and $isMacValue.Value) { return (Join-Path $HOME "Library/Application Support/SEGGER/JLinkDevices") }
    if (!$env:APPDATA) { throw "APPDATA is unavailable; cannot resolve the SEGGER user device-pack directory." }
    return (Join-Path $env:APPDATA "SEGGER\JLinkDevices")
}
