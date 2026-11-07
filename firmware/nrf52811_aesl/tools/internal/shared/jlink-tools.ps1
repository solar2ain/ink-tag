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

    $directories.Add("/opt/SEGGER/JLink")

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

    $names = @($ToolName)
    if ($ToolName -eq "JLink.exe") { $names += "JLinkExe" }
    foreach ($directory in Get-InkTagJLinkCandidateDirectories -RepoRoot $RepoRoot) {
        foreach ($name in $names) {
            $candidate = Join-Path $directory $name
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        }
    }

    foreach ($name in $names) {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($command) { return $command.Source }
    }
    throw "$ToolName was not found. Install SEGGER J-Link Software and Documentation Pack 7.88j, or set INKTAG_JLINK_HOME."
}

function Invoke-InkTagJLinkProcess {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$StdoutPath,
        [Parameter(Mandatory = $true)][string]$StderrPath,
        [string]$Label = "jlink",
        [ValidateRange(1, 86400)][int]$OperationTimeoutSeconds = 120,
        [switch]$ShowProgress
    )
    # Commander 7.88j can silently reject an overlong Windows command-file
    # path. Fail explicitly before starting it, rather than polling the board.
    if ($env:OS -eq 'Windows_NT') {
        for ($index = 0; $index -lt $Arguments.Count - 1; $index++) {
            if ($Arguments[$index] -eq '-CommanderScript' -and
                ($Arguments[$index + 1].Length -ge 260 -or
                 ([System.IO.Path]::GetFullPath($Arguments[$index + 1])).Length -ge 260)) {
                throw 'J-Link 7.88j command path exceeds MAX_PATH; choose a shorter -OutputDir. No programmer started.'
            }
        }
    }
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $FilePath
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    if ($start.PSObject.Properties.Name -contains "ArgumentList") {
        foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    } else {
        # Windows PowerShell 5.1/.NET Framework has no ArgumentList. Quote each
        # argv value, including trailing backslashes and embedded quotes.
        $start.Arguments = (@($Arguments | ForEach-Object {
            '"' + [regex]::Replace([regex]::Replace($_, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
        }) -join " ")
    }
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $start
    $encoding = [System.Text.UTF8Encoding]::new($false)
    $outFile = [System.IO.StreamWriter]::new($StdoutPath, $false, $encoding)
    $errFile = [System.IO.StreamWriter]::new($StderrPath, $false, $encoding)
    $outFile.AutoFlush = $errFile.AutoFlush = $true
    $nativeStarted = $false
    $completedNormally = $false
    $streams = @()
    $formatNative = $null -ne (Get-Command Write-InkTagNativeMessage -ErrorAction SilentlyContinue)
    $progressState = @{ Started = $null }
    $activityState = @{label=$Label;action='Connecting / waiting';seconds=0}
    $lastActivity = -10
    $nativeWatch = [Diagnostics.Stopwatch]::StartNew()
    $progressState.OperationStarted = $nativeWatch
    try {
        $null = $process.Start()
        $nativeStarted = $true
        foreach ($entry in @(@($process.StandardOutput, $outFile, 'stdout'), @($process.StandardError, $errFile, 'stderr'))) {
            $buffer = [char[]]::new(4096)
            $streams += [pscustomobject]@{ Reader=$entry[0]; Writer=$entry[1]; Name=$entry[2]; Buffer=$buffer; Pending=''; Presentation=@{raw='';display='';afterCr=$false}; Task=$entry[0].ReadAsync($buffer, 0, $buffer.Length) }
        }
        while (@($streams | Where-Object { $null -ne $_.Task }).Count) {
            $advanced = $false
            foreach ($stream in $streams) {
                if ($null -eq $stream.Task -or -not $stream.Task.IsCompleted) { continue }
                $count = $stream.Task.GetAwaiter().GetResult()
                $stream.Task = $null
                if ($count -gt 0) {
                    $chunk = [string]::new($stream.Buffer, 0, $count)
                    $stream.Writer.Write($chunk) # Preserve CR/LF, partial output and flushed raw bytes.
                    if ($formatNative) { Receive-InkTagNativeChunk $chunk $stream.Presentation $stream.Name $Label $progressState }
                    $stream.Pending += $chunk
                    $stream.Task = $stream.Reader.ReadAsync($stream.Buffer, 0, $stream.Buffer.Length)
                }
                if ($count -eq 0 -and $formatNative) { Receive-InkTagNativeChunk '' $stream.Presentation $stream.Name $Label $progressState -Final }
                do {
                    $separator = [regex]::Match($stream.Pending, '[\r\n]')
                    if ($separator.Success) {
                        $line = $stream.Pending.Substring(0, $separator.Index)
                        $stream.Pending = $stream.Pending.Substring($separator.Index + 1)
                    } elseif ($count -eq 0) { $line = $stream.Pending; $stream.Pending = '' }
                    else { break }
                    if ($line) {
                        if (!$formatNative -and ($VerbosePreference -eq 'Continue' -or $stream.Name -eq 'stderr' -or
                            $line -match '(?i)\b(error|failed|failure|cannot|could not|warning|fault|abort)\b')) {
                            Write-Host ("[{0}] [{1}] {2}" -f (Get-Date -Format 'HH:mm:ss'), $Label, $line)
                        }
                    }
                } while ($separator.Success)
                $advanced = $true
            }
            if ($nativeWatch.Elapsed.TotalSeconds -ge $OperationTimeoutSeconds) { throw "Native tool timeout; write outcome may be unknown. No replay. Raw logs: $StdoutPath" }
            if ($ShowProgress -and $formatNative -and [int]$nativeWatch.Elapsed.TotalSeconds -ge $lastActivity+10) {
                $lastActivity=[int]$nativeWatch.Elapsed.TotalSeconds
                $visible=if ($progressState.ContainsKey('LastProgress')) {$progressState.LastProgress} else {$activityState}
                $visible.seconds=$lastActivity
                Write-InkTagTerminalProgress $visible
            }
            if (-not $advanced) { Start-Sleep -Milliseconds 10 }
        }
        $remaining = [Math]::Max(1, [int](1000 * ($OperationTimeoutSeconds - $nativeWatch.Elapsed.TotalSeconds)))
        if (-not $process.WaitForExit($remaining)) { throw "Native tool timeout after stream closure; no replay. Raw logs: $StdoutPath" }
        $exitCode = $process.ExitCode
        $completedNormally = $true
    } finally {
        # Cancellation leaves write authority unknown. Stop the native helper
        # and keep flushed raw logs; the caller must never auto-replay a write.
      try { if ($formatNative) { Complete-InkTagTerminalProgress } }
      finally {
        if ($nativeStarted -and -not $process.HasExited) {
            $process.Kill()
            $process.WaitForExit()
        }
        if ($nativeStarted -and -not $completedNormally) {
            # Finish outstanding reads after stopping the helper. Bytes already
            # emitted before cancellation must stay in the original raw logs.
            foreach ($stream in $streams) {
                if ($null -ne $stream.Task) {
                    $tailCount = $stream.Task.GetAwaiter().GetResult()
                    if ($tailCount -gt 0) { $stream.Writer.Write($stream.Buffer, 0, $tailCount) }
                }
                $stream.Writer.Write($stream.Reader.ReadToEnd())
            }
        }
        $outFile.Dispose()
        $errFile.Dispose()
        $process.Dispose()
      }
    }
    return [pscustomobject]@{
        ExitCode = $exitCode
        Text = (([System.IO.File]::ReadAllText($StdoutPath)) + "`n" + [System.IO.File]::ReadAllText($StderrPath))
        Log = $StdoutPath
    }
}

function Assert-InkTagJLink788j {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$JLinkExe, [string]$LogDir = '')

    $verified = Get-Variable -Name InkTagVerifiedJLink788j -Scope Script -ErrorAction SilentlyContinue
    if ($null -ne $verified -and $verified.Value -eq $JLinkExe) { return }
    if (-not $LogDir) { $LogDir = [System.IO.Path]::GetTempPath() }
    $temporary = Join-Path $LogDir ("inktag-jlink-version-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $temporary | Out-Null
    try {
        $commandFile = Join-Path $temporary "version.jlink"
        $stdoutFile = Join-Path $temporary "stdout.log"
        $stderrFile = Join-Path $temporary "stderr.log"
        Set-Content -LiteralPath $commandFile -Encoding ASCII -Value "exit"
        $result = Invoke-InkTagJLinkProcess -FilePath $JLinkExe -StdoutPath $stdoutFile -StderrPath $stderrFile `
            -Label "version" -Arguments @("-NoGui", "1", "-AutoConnect", "0", "-CommanderScript", $commandFile)
        if ($result.ExitCode -ne 0 -or $result.Text -notmatch "(?i)J-Link Commander V7\.88j") {
            throw "InkTag requires J-Link Commander 7.88j. Actual executable: $JLinkExe"
        }
        $script:InkTagVerifiedJLink788j = $JLinkExe
    }
    finally {
        # Keep original version stdout/stderr even on a version refusal.
        Write-Verbose "J-Link version evidence: $temporary"
    }
}

function Resolve-InkTagJLinkSerial {
    [CmdletBinding()]
    param([string]$Snr = '', [Parameter(Mandatory = $true)][string]$JLinkExe,
          [Parameter(Mandatory = $true)][string]$LogDir)

    if ($Snr) {
        if ($Snr -notmatch '^\d+$') { throw 'J-Link serial must contain digits only.' }
        return $Snr
    }
    $id = [Guid]::NewGuid().ToString('N')
    $scriptPath = Join-Path $LogDir "probe-list-$id.jlink"
    Set-Content -LiteralPath $scriptPath -Encoding ASCII -Value @('ShowEmuList USB', 'exit')
    # Enumeration only: no Device, SWD connect, halt, reset or memory command.
    $result = Invoke-InkTagJLinkProcess -FilePath $JLinkExe `
        -Arguments @('-NoGui', '1', '-AutoConnect', '0', '-CommanderScript', $scriptPath) `
        -StdoutPath (Join-Path $LogDir "probe-list-$id.stdout.log") `
        -StderrPath (Join-Path $LogDir "probe-list-$id.stderr.log") -Label 'USB-probe-list' `
        -Verbose:($VerbosePreference -eq 'Continue')
    if ($result.ExitCode -ne 0) { throw "Could not list J-Link USB probes. See $($result.Log)" }
    $probeMatches = [regex]::Matches($result.Text, '(?im)^J-Link\[\d+\]:\s*Connection:\s*USB,\s*Serial number:\s*(\d+),')
    if ($probeMatches.Count -eq 0) { throw 'No J-Link USB probe found. Connect one J-Link and try again.' }
    if ($probeMatches.Count -ne 1) {
        $serials = @($probeMatches | ForEach-Object { $_.Groups[1].Value }) -join ', '
        throw "Multiple J-Link probes found ($serials). Keep one connected or specify -Snr."
    }
    $serial = $probeMatches[0].Groups[1].Value
    if (Get-Command Write-InkTagTerminalMessage -ErrorAction SilentlyContinue) { Write-InkTagTerminalMessage "Using J-Link $serial" }
    else { Write-Host "Using J-Link $serial" }
    return $serial
}

function Get-InkTagJLinkDeviceRoot {
    $isLinuxValue = Get-Variable -Name IsLinux -ErrorAction SilentlyContinue
    $isMacValue = Get-Variable -Name IsMacOS -ErrorAction SilentlyContinue
    if ($null -ne $isLinuxValue -and $isLinuxValue.Value) { return (Join-Path $HOME ".config/SEGGER/JLinkDevices") }
    if ($null -ne $isMacValue -and $isMacValue.Value) { return (Join-Path $HOME "Library/Application Support/SEGGER/JLinkDevices") }
    if (!$env:APPDATA) { throw "APPDATA is unavailable; cannot resolve the SEGGER user device-pack directory." }
    return (Join-Path $env:APPDATA "SEGGER\JLinkDevices")
}
