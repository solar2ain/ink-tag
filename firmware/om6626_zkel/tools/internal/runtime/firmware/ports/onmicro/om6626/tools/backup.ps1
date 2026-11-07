[CmdletBinding()]
param(
    [Parameter(HelpMessage='Required MCU selector: om6626. Uses the maintained 1 MiB main-Flash read backend; external Flash is not backed up.')][ValidateSet('om6626')][string]$Chip,
    [Parameter(HelpMessage='Required exact PCB marking for backup naming and metadata; 1 to 64 letters/digits/underscore/dot/hyphen, starting with a letter or digit.')][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$')][string]$Board,
    [Parameter(HelpMessage='Optional board note, at most 80 ASCII letters/digits/spaces/underscore/dot/hyphen; written to backup metadata.')][ValidatePattern('^[A-Za-z0-9_. -]{0,80}$')][string]$BoardNote='',
    [Parameter(HelpMessage='Backup parent directory; default factory_backups/om6626/<Board> under this checkout. Creates a new timestamped directory; never reuses an existing backup directory.')][string]$OutputDir='',
    [Parameter(HelpMessage='Import an existing local backup file instead of reading hardware. Not a live two-pass backup; cannot combine with JFlashExe/JLinkExe/JLinkSerial/FrequencyHz.')][string]$Source='',
    [Parameter(HelpMessage='Optional path to SEGGER JFlash.exe 7.88j. Must come from the same installation as JLink.exe; otherwise resolve the maintained installation.')][string]$JFlashExe='',
    [Parameter(HelpMessage='Optional path to SEGGER JLink.exe 7.88j, used to validate the installation; live reads use JFlash.exe.')][string]$JLinkExe='',
    [Parameter(HelpMessage='Optional exact USB probe serial. If omitted, SEGGER selects the probe; specify it when multiple probes are connected.')][string]$JLinkSerial='',
    [Parameter(HelpMessage='SWD transfer frequency in Hz, default 100000; positive whole kHz, at least 1000. Initial connection is capped at 100000 Hz.')][ValidateScript({$_ -ge 1000 -and $_ % 1000 -eq 0})][int]$FrequencyHz=100000,
    [Parameter(HelpMessage='Opt in to retry explicit connection absence only, separately for each read pass. Default is no retry; never replay an in-read failure or unknown timeout.')][switch]$RetryDetect,
    [Parameter(HelpMessage='Validate arguments and show the plan only; no programmer resolution, file copying or device access, including when Source is supplied.')][switch]$DryRun,
    [Parameter(HelpMessage='Readiness retry window per pass in seconds, default 120 (previously 90); range 10 to 180. Checked after a failed native attempt; not the read-process timeout.')][ValidateRange(10,180)][int]$TimeoutSeconds=120,
    [Parameter(HelpMessage='Maximum read time AFTER SWD contact, default 360 seconds; range 60 to 900. Before contact use AttemptTimeoutSeconds. Read timeout keeps partial evidence and stops without replay.')][ValidateRange(60,900)][int]$OperationTimeoutSeconds=360,
    [Parameter(HelpMessage='Per-attempt probe-open/initial-contact limit, default 12 seconds; range 3 to 120. With RetryDetect only a proven pre-target open stall may retry; attached/read failures never replay.')][ValidateRange(3,120)][int]$AttemptTimeoutSeconds=12,
    [Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments=$true)][object[]]$RemainingArguments
)
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../'))
. (Join-Path $PSScriptRoot 'terminal-log.ps1')
$terminalState=Get-InkTagTerminalColour
try {
Set-InkTagTerminalEnglish
. (Join-Path $repoRoot 'tools/powershell-cli.ps1')
$guard=Invoke-InkTagCliGuard -ScriptPath $PSCommandPath -HelpRequested $Help -RemainingArguments $RemainingArguments -Summary 'OM backup: -Chip om6626 -Board <exact PCB marking>; two independent read-only passes, no default retry'
if($null -ne $guard){exit $guard}
if(!$Chip -or !$Board){exit (Stop-InkTagCliUsage -ScriptPath $PSCommandPath -Message '-Chip and -Board are required')}
if($Source -and (@('JFlashExe','JLinkExe','JLinkSerial','FrequencyHz') | Where-Object {$PSBoundParameters.ContainsKey($_)})){
    throw '-Source is a manual import; hardware transport parameters are not applicable'
}
if(!$OutputDir){$OutputDir=Join-Path $repoRoot ('factory_backups/om6626/'+$Board)}
if($DryRun){
    Write-InkTagTerminalMessage "DryRun: OM6626 backup for PCB $Board -> $OutputDir; SWD $FrequencyHz Hz (attach at most 100000 Hz); no tool resolved, files copied or device accessed."
    if($Source){Write-InkTagTerminalMessage 'Source is an explicit manual file import, not a live backup.' -Level WARNING}
    exit 0
}
if($Source) {
    # Import is a local-file operation, never a fallback from a failed probe.
    Write-InkTagTerminalStage 'MANUAL FILE IMPORT (NO LIVE READ)'
    & (Join-Path $PSScriptRoot 'internal/backup-import.ps1') -Board $Board -Source $Source -OutputDir $OutputDir -BoardNote $BoardNote -Verbose:([bool]$PSBoundParameters['Verbose'])
    exit $LASTEXITCODE
}
$options=@{Board=$Board;BoardNote=$BoardNote;OutputDir=$OutputDir;
    RetryDetect=$RetryDetect.IsPresent;AttachWindowSeconds=$TimeoutSeconds;
    OperationTimeoutSeconds=$OperationTimeoutSeconds;AttemptTimeoutSeconds=$AttemptTimeoutSeconds;FrequencyHz=$FrequencyHz}
foreach($name in @('JFlashExe','JLinkExe','JLinkSerial')) {
    if($PSBoundParameters.ContainsKey($name)){$options[$name]=$PSBoundParameters[$name]}
}
if($PSBoundParameters.ContainsKey('Verbose')){$options.Verbose=[bool]$PSBoundParameters['Verbose']}
& (Join-Path $PSScriptRoot 'internal/backup-jlink.ps1') @options
exit $LASTEXITCODE
} catch {Write-InkTagTerminalMessage $_.Exception.Message -Level ERROR;exit 1}
finally {try {Complete-InkTagTerminalProgress} finally {Restore-InkTagTerminalColour $terminalState}}
