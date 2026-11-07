param(
    [string]$JLinkExe = "",
    [string]$Snr = "",
    [int]$FrequencyHz = 100000,
    [ValidateSet("halt", "pre-reset", "under-reset", "attach")]
    [string]$ConnectMode = "halt",
    [switch]$NoWait,
    [int]$IntervalSeconds = 1,
    [int]$TimeoutSeconds = 120,
    [switch]$RetryDetect,
    [int]$AttemptTimeoutSeconds = 8
)

trap {
    Write-Host ""
    Write-Host ("ERROR: " + $_.Exception.Message) -ForegroundColor Red
    exit 1
}

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot 'readonly-process.ps1')
$args = @{
    FrequencyHz = $FrequencyHz
    ConnectMode = $ConnectMode
    IntervalSeconds = $IntervalSeconds
    TimeoutSeconds = $TimeoutSeconds
    AttemptTimeoutSeconds = $AttemptTimeoutSeconds
    RetryDetect = $RetryDetect.IsPresent
}
if ($JLinkExe -ne "") { $args.JLinkExe = $JLinkExe }
if ($Snr -ne "") { $args.Snr = $Snr }
# One supervised session, with initialization once and native retries inside it.
# The native connection/read/release recipe remains the former Probe recipe;
# a close/cancel/crash cannot leave its native descendants holding the USB probe.
$directory=Join-Path ([IO.Path]::GetTempPath()) ('inktag-om6626-read-worker-'+[Guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $directory
$session=$null
try {
    $session=Start-OmReadWorker @{Kind='Probe';ProbeArguments=$args} $directory
    Enable-OmReadWorker $session
    $clock=[Diagnostics.Stopwatch]::StartNew();$offsets=@{};$contactSeen=$false;$ready=$false
    while(!$session.Process.HasExited) {
        Show-OmReadWorkerOutput $session $offsets
        $text=Get-OmReadText @($session.Stdout,$session.Stderr)
        if($text -match '(?m)^OM_PROBE_READY\s*$'){$ready=$true}
        if($text -match '(?m)^OM_PROBE_(?:NATIVE_ACTIVE|CONTACT)\s*$'){$contactSeen=$true}
        # Five seconds cover worker startup/finalization, not extra SWD commands.
        $limit=if(!$ready){$AttemptTimeoutSeconds+5}elseif($contactSeen){$TimeoutSeconds+305}else{$TimeoutSeconds+$AttemptTimeoutSeconds+10}
        if($clock.Elapsed.TotalSeconds -ge $limit){throw "Probe worker stalled; its owned processes are being stopped. Logs: $($session.Directory)"}
        Start-Sleep -Milliseconds 100
        $session.Process.Refresh()
    }
    $session.Process.WaitForExit()
    Show-OmReadWorkerOutput $session $offsets -Final
    $result=$session.Process.ExitCode
    if($result -ne 0 -and (Get-OmReadText @($session.Stdout,$session.Stderr)) -notmatch '(?m)^ERROR:'){
        Write-Host "ERROR: Probe worker exited with code $result. Raw logs: $($session.Directory)"
    }
} finally { Stop-OmReadWorker $session }
exit $result
