# Shared body for the two MCU probe entry points. Wrapper parameters remain
# visible to PowerShell help; no board/profile or firmware selection occurs.
. (Join-Path $PSScriptRoot 'terminal-log.ps1')
$grTerminalColour=Get-InkTagTerminalColour
try {
    Set-InkTagTerminalEnglish
    $ErrorActionPreference='Stop'
    $repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
    . (Join-Path $repoRoot 'tools/powershell-cli.ps1')
    $guard=Invoke-InkTagCliGuard -ScriptPath $grProbePublicEntry -HelpRequested $Help -RemainingArguments $RemainingArguments `
        -Summary 'GR MCU probe: no price-tag/board selection. Reports valid SDK package information or explicitly unconfirmed Cortex-M4 information. RetryDetect repeats detection only. No Flash/OTP/status writes.'
    if($null -ne $guard){exit $guard}
    if(!$DryRun){Write-InkTagTerminalStage "GR MCU probe: $Chip"}
    $family=if($Chip -eq 'gr5331'){'gr5331'}else{'gr551x'}
    $arguments=@('probe','--family',$family,
        '--frequency-hz',"$FrequencyHz",'--interval-seconds',"$IntervalSeconds",
        '--timeout-seconds',"$TimeoutSeconds",'--operation-timeout-seconds',"$OperationTimeoutSeconds",
        '--powershell',(Get-Process -Id $PID).Path)
    foreach($pair in @(@('chip',$Chip),@('output-dir',$OutputDir),@('jlink-exe',$JLinkExe),@('snr',$JLinkSerial))){
        if($pair[1]){$arguments+=@(('--'+$pair[0]),[string]$pair[1])}
    }
    foreach($pair in @(@('retry-detect',$RetryDetect),@('dry-run',$DryRun))){
        if($pair[1]){$arguments+='--'+$pair[0]}
    }
    if($grProbePublicParameters.ContainsKey('Verbose') -and $grProbePublicParameters['Verbose']){$arguments+='--verbose'}
    Invoke-InkTagGoodixHostProcess -Python 'python' -Script (Join-Path $repoRoot 'tools/hardware/goodix-stock-workflow.py') -Options $arguments -MachineReadable:$DryRun
    exit $LASTEXITCODE
} catch {Write-InkTagTerminalMessage $_.Exception.Message -Level ERROR;exit 1}
finally {
    try {Complete-InkTagTerminalProgress}
    finally {Restore-InkTagTerminalColour $grTerminalColour}
}
