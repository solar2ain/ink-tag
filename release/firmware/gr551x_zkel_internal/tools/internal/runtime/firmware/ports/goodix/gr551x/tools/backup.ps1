[CmdletBinding()]
param(
    [ValidateSet('gr5331','gr551x','gr5513','gr5515')][string]$Chip='',
    [string]$Board='', [string]$BoardNote='',
    [string]$OutputDir='', [string]$JLinkExe='',
    [Alias('Snr')][string]$JLinkSerial='',
    [int]$FrequencyHz=500000,
    [Parameter(HelpMessage='Retry detection only; never replay a read or write.')][switch]$RetryDetect,
    [int]$TimeoutSeconds=120,
    [int]$OperationTimeoutSeconds=360,
    [switch]$DryRun,
    [Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments=$true)][object[]]$RemainingArguments
)
. (Join-Path $PSScriptRoot '../../common/tools/internal/terminal-log.ps1')
$grTerminalColour=Get-InkTagTerminalColour
try {
Set-InkTagTerminalEnglish
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../..'))
. (Join-Path $repoRoot 'tools/powershell-cli.ps1')
$guard=Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help -RemainingArguments $RemainingArguments `
    -Summary 'GR backup: Chip-first; GR551x accepts gr5513/gr5515/gr551x and reads actual chip/capacity. RetryDetect waits for contact only. Two independent reads, never replayed. GR551x vendor RAM reader requires one ordinary reset afterwards. No Flash/OTP/status writes.'
if($null -ne $guard){exit $guard}
    if(!$Board){throw 'Backup requires -Board <PCB-label>; no hardware accessed. Example: -Chip gr551x -Board ZK_EL_GR42V_V1.2'}
    if(!$DryRun){Write-InkTagTerminalStage "GR backup: $Chip"}
    $arguments=@('backup','--family','gr551x',
        '--frequency-hz',"$FrequencyHz",'--interval-seconds','1',
        '--timeout-seconds',"$TimeoutSeconds",'--operation-timeout-seconds',"$OperationTimeoutSeconds",
        '--powershell',(Get-Process -Id $PID).Path)
    foreach($pair in @(@('chip',$Chip),@('board',$Board),@('board-note',$BoardNote),
        @('output-dir',$OutputDir),@('jlink-exe',$JLinkExe),@('snr',$JLinkSerial))){
        if($pair[1]){$arguments+=@(('--'+$pair[0]),[string]$pair[1])}
    }
    foreach($pair in @(@('retry-detect',$RetryDetect),@('dry-run',$DryRun))){
        if($pair[1]){$arguments+='--'+$pair[0]}
    }
    if($PSBoundParameters.ContainsKey('Verbose') -and $PSBoundParameters['Verbose']){$arguments+='--verbose'}
    Invoke-InkTagGoodixHostProcess -Python 'python' -Script (Join-Path $repoRoot 'tools/hardware/goodix-stock-workflow.py') -Options $arguments -MachineReadable:$DryRun
    exit $LASTEXITCODE
} catch {Write-InkTagTerminalMessage $_.Exception.Message -Level ERROR;exit 1}
finally {
    try {Complete-InkTagTerminalProgress}
    finally {Restore-InkTagTerminalColour $grTerminalColour}
}
