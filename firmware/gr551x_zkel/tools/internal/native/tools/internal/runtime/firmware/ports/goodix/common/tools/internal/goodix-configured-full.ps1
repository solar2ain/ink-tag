. (Join-Path $PSScriptRoot 'terminal-log.ps1')
function Invoke-InkTagGoodixConfiguredFull {
    param(
        [string]$ScriptPath, [System.Collections.IDictionary]$Parameters,
        [string]$Config, [string]$Firmware, [bool]$Yes,
        [bool]$DryRun, [string]$Python, [bool]$HelpRequested, [object[]]$RemainingArguments
    )
    $fullTerminalColour = Get-InkTagTerminalColour
    try {
    Set-InkTagTerminalEnglish
    $ErrorActionPreference = 'Stop'
    $repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
    . (Join-Path $repoRoot 'tools/powershell-cli.ps1')
    $guard = Invoke-InkTagCliGuard -ScriptPath $ScriptPath -HelpRequested $HelpRequested `
        -RemainingArguments $RemainingArguments -Summary 'GR Full: -Config PCB.toml/yaml/json -Firmware app-full.zip. Preserves verified same-device Pro and necessary identity/calibration; clears MCU user data and old health/crash history. Fitted GR5513 external Flash remains. Asks y/N, or -Yes skips confirmation. -DryRun reviews offline without hardware. Optional JLinkSerial, RetryDetect and TimeoutSeconds. Unrecognized old user formats are cleared; untyped legacy addresses use the native public default. Invalid recognized authority stops before erase; old executable layout/epoch is not an admission gate. No local stock backup or post-write App helper/CLI/BLE/time sync. One write, complete readback and bounded final-reset cause cleanup. Registered GR5513 and GR5515I0ND use the shared GR551x package.'
    if ($null -ne $guard) { $global:LASTEXITCODE = $guard; return }
        if (!$Config -or !$Firmware) { throw '-Config and -Firmware are required; legacy Board/BIN/ELF mode is retired. No hardware accessed.' }
        if (!$DryRun) {
            Write-InkTagTerminalStage 'CONFIGURED FULL / CLEAR MCU USER DATA'
            Write-InkTagTerminalMessage 'Full preserves a verified same-device Pro authorization and necessary identity/calibration. It clears MCU user settings/password/Find, fonts, internal image slots and old health/crash history. THIS CANNOT BE UNDONE. GR5513 external Flash content is retained (not erased or accessed). Immutable factory/OTP is not programmed. Readable missing/damaged/unknown old information initializes defaults; unverifiable or foreign Pro initializes Free with a warning. Actual read failure, changed target or invalid incoming firmware still stops before erase. Untyped legacy address bytes are kept and initialized as native public. Recovery state is rebuilt from the selected package.' -Level WARNING
            if($Yes) { Write-InkTagTerminalMessage '-Yes supplied: data-clear confirmation accepted. Signature, exact MCU/identity/calibration checks and complete readback remain mandatory.' -Level WARNING }
            if (!$Yes) {
                $answer = Read-Host 'Erase all MCU user data and Full flash? [y/N]'
                if ($answer -notmatch '^(?i:y|yes)$') {
                    Write-InkTagTerminalMessage 'Full cancelled; no device accessed.'
                    $global:LASTEXITCODE = 0
                    return
                }
            }
        }
        $family = if ((Split-Path (Split-Path $ScriptPath -Parent) -Parent) -match 'gr5331$') { 'gr5331' } else { 'gr551x' }
        $options = @('--family', $family, '--base', $Firmware, '--config', $Config)
        if ($Parameters['Verbose']) { $options += '--verbose' }
        if ($Parameters['JLinkSerial']) { $options += @('--snr', [string]$Parameters['JLinkSerial']) }
        if ($Parameters['RetryDetect']) { $options += '--retry-detect' }
        if ($Parameters['DetectTimeoutSeconds']) { $options += @('--detect-timeout', [string]$Parameters['DetectTimeoutSeconds']) }
        if ($Parameters['FrequencyHz']) { $options += @('--frequency-hz', [string]$Parameters['FrequencyHz']) }
        if ($Parameters['OperationTimeoutSeconds']) { $options += @('--operation-timeout', [string]$Parameters['OperationTimeoutSeconds']) }
        if (!$DryRun) {
            . (Join-Path $repoRoot 'tools/jlink-tools.ps1')
            $jlink=Resolve-InkTagJLinkTool -ToolName 'JLink.exe' -RepoRoot $repoRoot
            $options += @('--require-write','--console-progress','--jlink',$jlink)
        }
        elseif ($Parameters['Verbose']) { $options += '--console-progress' }
        $stageName='GR Full: '+$family
        if($DryRun){$stageName+=' / OFFLINE REVIEW'}
        Write-InkTagTerminalStage $stageName
        Write-InkTagTerminalMessage 'Validating Config and app-full.zip; no Flash access during offline review'
        Write-InkTagTerminalMessage ('DryRun={0}; exact live MCU/capacity/voltage checks precede a single Full write.' -f $DryRun) -Level DEBUG
        Invoke-InkTagGoodixHostProcess -Python $Python -Script (Join-Path $repoRoot 'tools/hardware/goodix_configured_full.py') -Options $options -MachineReadable:($DryRun -and !$Parameters['Verbose'])
        if ($LASTEXITCODE -ne 0) { throw 'Configured Full failed; inspect the recorded stage. No automatic write replay was attempted.' }
        if ($DryRun) { Write-InkTagTerminalMessage '[OFFLINE REVIEW] Hardware was not accessed; this is not a Full qualification pass.' }
        $global:LASTEXITCODE = 0
    } catch {
        Write-InkTagTerminalMessage $_.Exception.Message -Level ERROR
        $global:LASTEXITCODE = 1
    } finally {
        try { Complete-InkTagTerminalProgress }
        finally { Restore-InkTagTerminalColour $fullTerminalColour }
    }
}
