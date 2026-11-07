[CmdletBinding()]
param(
    [ValidateSet('DA14585')][string]$Chip = 'DA14585',
    [string]$JLinkExe = '', [Alias('JLink','Snr')][string]$JLinkSerial = '', [string]$PythonExe = 'python',
    [Parameter(HelpMessage='SWD frequency in Hz; positive whole kHz. Default 1000000 Hz.')]
    [ValidateScript({$_ -ge 1000 -and $_ -le 4000000 -and $_ % 1000 -eq 0})][int]$FrequencyHz = 1000000,
    [Parameter(HelpMessage='Legacy kHz spelling; cannot combine with FrequencyHz.')]
    [ValidateRange(1,4000)][int]$SpeedKHz,
    [Parameter(HelpMessage='Opt in to readiness retries only, never capture/write/unknown-result replay.')]
    [switch]$RetryDetect,
    [ValidateRange(1,300)][int]$IntervalSeconds = 1,
    [ValidateRange(0,86400)][int]$TimeoutSeconds = 0,
    [Parameter(HelpMessage='Per Python/DLL attempt limit; capped by overall deadline. Timeout is terminal/unknown.')]
    [Alias('AttemptTimeoutSeconds')][ValidateRange(1,300)][int]$OperationTimeoutSeconds = 15,
    [string]$OutputDir = '', [switch]$DryRun,
    [Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments=$true)][object[]]$RemainingArguments
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'internal/context.ps1')
$toolContext = Get-InkTagDaToolContext -ToolsRoot $PSScriptRoot
$RepoRoot = $toolContext.Root
. (Join-Path $PSScriptRoot 'internal/host-common.ps1')
$terminalState=Start-InkTagDaTerminal
try {
. (Resolve-InkTagDaToolFile $toolContext 'tools/powershell-cli.ps1')
$cli = Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help -RemainingArguments $RemainingArguments -Summary 'Read-only DA14585 DAP detection; RetryDetect is opt-in, not capture/write retry'
if ($null -ne $cli) { exit $cli }
if($PSBoundParameters.ContainsKey('SpeedKHz')){
    if($PSBoundParameters.ContainsKey('FrequencyHz')){throw 'Use FrequencyHz or SpeedKHz, not both; no device accessed.'}
    $FrequencyHz=$SpeedKHz*1000
}
$SpeedKHz=[int]($FrequencyHz/1000)
if($JLinkSerial -and $JLinkSerial -notmatch '^[0-9]{4,16}$'){throw 'JLinkSerial must be an exact numeric serial; no fallback.'}
if ($DryRun) {
    Write-InkTagDaLog INFO "DRY RUN: chip=$Chip SWD=$SpeedKHz kHz RetryDetect=$([bool]$RetryDetect); per-attempt limit=${OperationTimeoutSeconds}s; no debugger or output session opened"
    exit 0
}
Write-InkTagDaStage 'PROBE / LOCAL DEPENDENCIES'
$python = Resolve-InkTagDaPython $toolContext $PythonExe
Assert-InkTagDaToolEnvironment $toolContext $python -OperationTimeoutSeconds $OperationTimeoutSeconds
. (Resolve-InkTagDaToolFile $toolContext 'tools/jlink-tools.ps1')
. (Resolve-InkTagDaToolFile $toolContext 'tools/da-backup-archive.ps1')
if ($toolContext.Portable) { $JLinkExe = Resolve-InkTagDaVendorFile $toolContext 'JLink.exe' $JLinkExe }
else {
    $JLinkExe = Resolve-InkTagJLinkTool -ToolName JLink.exe -ExplicitPath $JLinkExe -RepoRoot $RepoRoot
    Assert-InkTagJLink788j -JLinkExe $JLinkExe
}
$abi=Invoke-InkTagDaLoggedProcess -Exe $python -Arguments @('-c','import struct; print(struct.calcsize("P"))') -OperationTimeoutSeconds $OperationTimeoutSeconds -Label 'Python ABI' -Detailed:([bool]$PSBoundParameters['Verbose'])
$pointerSize=$abi.Text.Trim()
if ($abi.ExitCode -ne 0 -or $pointerSize -notin '4','8') { throw 'Cannot determine Python architecture' }
$dllName = if ($pointerSize -eq '8') { 'JLink_x64.dll' } else { 'JLinkARM.dll' }
$dll = Join-Path (Split-Path -Parent $JLinkExe) $dllName
if ($toolContext.Portable) { $dll = Resolve-InkTagDaVendorFile $toolContext $dllName $dll }
if (!(Test-Path -LiteralPath $dll -PathType Leaf)) { throw "Missing matching J-Link DLL: $dll" }
$session = New-InkTagDaCaptureSession -RepoRoot $RepoRoot -OutputDir $OutputDir -Category debug
$probeArgs = @('-B',(Resolve-InkTagDaToolFile $toolContext 'tools/hardware/probe-da14585-dap.py'),'--jlink-dll',$dll,'--speed',[string]$SpeedKHz)
if ($JLinkSerial) { $probeArgs += @('--serial',$JLinkSerial) }
Write-InkTagDaLog WARNING 'DAP power requests perturb sleep; do not use on a clock/power observation device.'
Write-InkTagDaLog INFO "Operation evidence: $($session.path)"
Write-InkTagDaStage 'PROBE / EXACT SWD TARGET'
$attemptState=@{count=0}
$showDetail=[bool]$PSBoundParameters['Verbose']
$attempt = {
    param([double]$RemainingSeconds)
    $attemptState.count++
    $limit=[double]$OperationTimeoutSeconds
    if($RemainingSeconds -gt 0){$limit=[Math]::Min($limit,$RemainingSeconds)}
    $log=Join-Path $session.path ('probe-{0:d4}.log' -f $attemptState.count)
    $reply=Invoke-InkTagDaLoggedProcess -Exe $python -Arguments $probeArgs -Log $log -OperationTimeoutSeconds $limit -Label ('Detect '+$attemptState.count) -Detailed:$showDetail
    $result = $null
    $json = @($reply.Lines | Where-Object { ([string]$_).TrimStart().StartsWith('{') })
    if ($json.Count -eq 1) { try { $result = ([string]$json[0]) | ConvertFrom-Json } catch {} }
    if($null -ne $result -and $JLinkSerial -and [string]$result.serial -ne $JLinkSerial){$result=$null}
    @{exitCode=$reply.ExitCode;result=$result}
}
    $identity = Invoke-InkTagDaReadiness -Attempt $attempt -RetryDetect:$RetryDetect -IntervalSeconds $IntervalSeconds -TimeoutSeconds $TimeoutSeconds
    Write-InkTagDaLog SUCCESS "SWD identity read: Cortex-M0 / DA14585 chip ID 585; J-Link $($identity.serial). No Flash programmed."
    Write-InkTagDaLog INFO "Raw logs and native/timeout result receipts: $($session.path)"
} catch {
    Write-InkTagDaLog ERROR $_.Exception.Message
    Write-InkTagDaLog WARNING 'Stopped without reset or replay. A timed-out child may not have restored DAP state; preserve evidence.'
    exit 1
} finally {Restore-InkTagDaTerminal $terminalState}
