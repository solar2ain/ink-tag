[CmdletBinding()]
param(
    [Parameter(HelpMessage='Complete firmware ZIP, including signed OTA and matching GUI resources.')][string]$Firmware = '',
    [Parameter(HelpMessage='Supplied exact-board TOML configuration.')][string]$Config = '',
    [Parameter(HelpMessage='Confirm the displayed user-data clearing scope.')][switch]$Yes,
    [Parameter(HelpMessage='Validate firmware and configuration without opening a probe.')][switch]$DryRun,
    [Alias('Snr')][string]$JLinkSerial = '',
    [string]$JLinkExe = '',
    [Alias('Python')][string]$PythonExe = '',
    [Alias('Target')][ValidateSet('nrf52811')][string]$Chip = 'nrf52811',
    [int]$FrequencyHz = 500000,
    [Parameter(HelpMessage='Retry only initial read-only target detection; never retry capture, programming or verification.')][switch]$RetryDetect,
    [Parameter(HelpMessage='Initial detection window in seconds; used only with RetryDetect.')][ValidateRange(1,3600)][int]$DetectTimeoutSeconds = 120,
    [Parameter(HelpMessage='Seconds between initial no-contact detection attempts.')][ValidateRange(1,3600)][int]$IntervalSeconds = 1,
    [ValidateRange(1,86400)][int]$OperationTimeoutSeconds = 180,
    [string]$OutputDir = '',
    [Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments=$true)][object[]]$RemainingArguments
)
. (Join-Path $PSScriptRoot 'internal/terminal-log.ps1')
$terminalState = Get-InkTagTerminalColour
try {
    Set-InkTagTerminalEnglish
    $ErrorActionPreference = 'Stop'
    . (Join-Path $PSScriptRoot 'internal/runtime-context.ps1')
    $context = Get-InkTagRuntimeContext $PSScriptRoot
    . $context.CliHelper
    $guard = Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help `
        -RemainingArguments $RemainingArguments -Summary 'nRF52811 complete programming: MCU, native settings, GUI resources and image headers; default y/N'
    if ($null -ne $guard) { exit $guard }
    if (-not $Firmware -or -not $Config) { throw 'Specify -Firmware app-full.zip and -Config EL022HWRL.toml' }
    if ($FrequencyHz -lt 1000 -or $FrequencyHz % 1000 -ne 0 -or ($JLinkSerial -and $JLinkSerial -notmatch '^\d+$')) { throw 'Invalid SWD frequency or J-Link serial' }
    if ($context.IsBundle) {
        $bundle = Get-Content -LiteralPath (Join-Path $context.Root 'bundle.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($bundle.artifactKind -ne 'tools-only' -or $bundle.fullWriter -ne 'native-v1' -or
            $bundle.runtimeKind -ne 'portable' -or $bundle.firmwareIncluded -ne $false) {
            throw 'Unsupported tools-only writer identity; no target accessed'
        }
        $bundledPython=Join-Path $PSScriptRoot '.runtime/python/python.exe'
        if ($PythonExe -and [IO.Path]::GetFullPath($PythonExe) -ne [IO.Path]::GetFullPath($bundledPython)) { throw 'Portable tools use their verified portable Python only' }
        $PythonExe=$bundledPython
        . (Join-Path $PSScriptRoot 'internal/full-bundle-guard.ps1')
        Assert-InkTagCompleteBundleFiles -ToolsDirectory $PSScriptRoot
        $checked = & $PythonExe -I -B (Join-Path $PSScriptRoot 'internal/full-runtime.py') check --tools $PSScriptRoot
        if ($LASTEXITCODE -ne 0) { throw 'Run this bundle tools/setup.ps1 before flashing; no target accessed' }
    } elseif (-not $PythonExe) { $PythonExe = 'python' }
    $reviewText = & $PythonExe -I -B (Join-Path $PSScriptRoot 'internal/nrf11_full.py') inspect --firmware $Firmware --config $Config
    if ($LASTEXITCODE -ne 0) { throw 'Firmware/config validation failed; no target accessed' }
    $review = ($reviewText -join "`n") | ConvertFrom-Json
    if ($DryRun) {
        $review | Add-Member -NotePropertyName retryDetect -NotePropertyValue ([bool]$RetryDetect)
        $review | Add-Member -NotePropertyName detectTimeoutSeconds -NotePropertyValue $DetectTimeoutSeconds
        $review | Add-Member -NotePropertyName intervalSeconds -NotePropertyValue $IntervalSeconds
        $review | ConvertTo-Json -Depth 8; exit 0
    }
    Write-InkTagTerminalStage 'CONFIGURED FULL / CLEAR USER DATA'
    Write-InkTagTerminalMessage ("Config: {0}; PCB: {1}; Note: {2}; target: nRF52811 (not detected yet)" -f $Config,$review.config.board_marking,$review.config.note)
    Write-InkTagTerminalMessage 'Complete programming clears all ordinary settings and invalidates stored pictures. GUI resources are replaced. Image pixel data is not securely erased. No automatic backup.' -Level WARNING
    Write-InkTagTerminalMessage 'FICR/UICR, factory identity/calibration and reserved external sectors are preserved. No BLE or clock write. Full permits version downgrade; OTA anti-rollback remains separate.'
    Write-InkTagTerminalMessage ("App: {0}/{1} B; Recovery: {2}/{3} B" -f $review.appBytes,$review.appLimit,$review.recoveryBytes,$review.recoveryLimit)
    Write-InkTagTerminalMessage ("Package SHA-256: {0}" -f $review.packageSha256)
    Write-InkTagTerminalMessage $review.recoveryTrust -Level WARNING
    Write-InkTagTerminalMessage ("Screen: {0}, {1}x{2} {3}" -f $review.config.screen.ic,$review.config.screen.width,$review.config.screen.height,$review.config.screen.color)
    Write-InkTagTerminalMessage 'Progress is per native operation; 100% is not final verification. Detailed output is in the evidence logs; use -Verbose to display it.'
    if ($Yes) { Write-InkTagTerminalMessage 'Explicit -Yes confirms the clearing scope; signature, identity, voltage and readback checks remain required.' -Level WARNING }
    if (-not $Yes) {
        Write-InkTagTerminalMessage 'Continue? [y/N]' -Level WARNING
        if (([string](Read-Host)).Trim() -notmatch '^(?i:y|yes)$') { Write-InkTagTerminalMessage 'Cancelled; no target accessed'; exit 0 }
    }
    if (-not $OutputDir) { $OutputDir = Join-Path ([IO.Path]::GetTempPath()) 'InkTag-flashing' }
    $operation = Join-Path ([IO.Path]::GetFullPath($OutputDir)) ('nrf11-' + [Guid]::NewGuid().ToString('N').Substring(0,12))
    $detect = $operation + '-probe'
    Write-InkTagTerminalMessage "Operation evidence: $operation"
    Write-InkTagTerminalStage 'COMPLETE FLASH / EXACT PROBE AND SWD TARGET'
    New-Item -ItemType Directory -Path $detect -Force | Out-Null
    . $context.JLinkHelper
    $JLinkExe = Resolve-InkTagJLinkTool -ToolName 'JLink.exe' -ExplicitPath $JLinkExe -RepoRoot $context.Root
    if ([IO.Path]::GetFileName($JLinkExe) -ine 'JLink.exe') { throw 'The executable must be the pinned JLink.exe; no target accessed' }
    $nativeLock=Get-Content (Join-Path $PSScriptRoot 'full-dependencies.lock.json') -Raw|ConvertFrom-Json
    foreach($file in $nativeLock.native.files){
        $path=Join-Path (Split-Path -Parent $JLinkExe) $file.name
        if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -cne $file.sha256){throw 'Install the pinned J-Link 7.88j pack; native file binding mismatch. No target accessed.'}
    }
    Assert-InkTagJLink788j -JLinkExe $JLinkExe -LogDir $detect -Verbose:($VerbosePreference -eq 'Continue')
    $serial = Resolve-InkTagJLinkSerial -Snr $JLinkSerial -JLinkExe $JLinkExe -LogDir $detect -Verbose:($VerbosePreference -eq 'Continue')
    if ($RetryDetect) {
        Write-InkTagTerminalStage 'COMPLETE FLASH / INITIAL READ-ONLY DETECTION'
        Write-InkTagTerminalMessage "RetryDetect enabled: initial no-contact detection only; window=${DetectTimeoutSeconds}s; interval=${IntervalSeconds}s. Capture and writes are never retried."
        & (Join-Path $PSScriptRoot 'internal/capture-jlink.ps1') -Operation probe -JLinkExe $JLinkExe -Snr $serial `
            -FrequencyHz $FrequencyHz -Wait $true -TimeoutSeconds $DetectTimeoutSeconds -IntervalSeconds $IntervalSeconds `
            -OperationTimeoutSeconds $OperationTimeoutSeconds -OutputDir $detect -PythonExe $PythonExe `
            -Verbose:($VerbosePreference -eq 'Continue')
        if ($LASTEXITCODE -ne 0) { throw 'Initial target detection stopped; no preservation capture or Full write started. Raw logs retained.' }
    }
    $progressState = @{}
    & $PythonExe -I -B (Join-Path $PSScriptRoot 'internal/full-session.py') --firmware $Firmware --config $Config `
        --jlink $JLinkExe --serial $serial --output $operation --frequency $FrequencyHz --timeout $OperationTimeoutSeconds `
        --expected-package-sha $review.packageSha256 --expected-config-sha $review.config.sha256 | ForEach-Object { Show-InkTagFullEvent ([string]$_) $progressState }
    if ($LASTEXITCODE -ne 0) { throw "Full stopped; preserve evidence at $operation. No automatic retry or reset." }
    exit 0
} catch { Write-InkTagTerminalMessage $_.Exception.Message -Level ERROR; exit 1 }
finally { try { Complete-InkTagTerminalProgress } finally { Restore-InkTagTerminalColour $terminalState } }
