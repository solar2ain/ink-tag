param([string]$JLinkExe='', [string]$Snr='', [int]$FrequencyHz=100000,
    [ValidateSet('halt','attach','pre-reset','under-reset')][string]$ConnectMode='halt',
    [int]$AttemptTimeoutSeconds=12,[int]$TimeoutSeconds=120,[int]$IntervalSeconds=1,[switch]$RetryDetect)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../../'))
. (Join-Path $repoRoot 'tools/jlink-tools.ps1')
. (Join-Path $PSScriptRoot 'readonly-process.ps1')

function Test-OmDetectionRetryAllowed([string]$LogText) {
    if($LogText -match '(?i)(E000ED00\s*=|Connected successfully|Wrong SWD target)'){return $false}
    if($LogText -match '(?is)\A\s*(?:=== J-Link attempt [0-9a-f]{32} ===\s*)?INKTAG_ATTEMPT_TIMEOUT after [0-9]+ second\(s\)\s*\z'){return $true}
    return ($LogText -match '(?i)(failed to connect|could not connect|cannot connect|SW-DP.*(failed|not found))' -and
        $LogText -notmatch '(?i)(INKTAG_ATTEMPT_TIMEOUT|E000ED00\s*=|Connected successfully|Wrong SWD target)')
}

function Get-OmProbeCommands {
    $commands=@('device CORTEX-M4','si SWD',('speed '+[int]($FrequencyHz/1000)))
    if($ConnectMode -in @('pre-reset','under-reset')){$commands+=@('r0','sleep 200')}
    if($ConnectMode -eq 'pre-reset'){$commands+=@('r1','sleep 50')}
    $commands+='connect'
    if($ConnectMode -eq 'under-reset'){$commands+=@('r1','sleep 50')}
    if($ConnectMode -ne 'attach'){$commands+='halt'}
    return $commands
}

function Invoke-OmProbeNative([string[]]$Commands,[string]$Stem,[double]$ContactLimit) {
    $commandFile=Join-Path $directory ($Stem+'.jlink')
    Set-Content -LiteralPath $commandFile -Value ($Commands+@('qc')) -Encoding ASCII
    $stdout=Join-Path $directory ($Stem+'.stdout.log');$stderr=Join-Path $directory ($Stem+'.stderr.log')
    $arguments=@('-NoGui','1','-Device','CORTEX-M4','-If','SWD','-Speed',([string][int]($FrequencyHz/1000)),'-AutoConnect','1','-CommanderScript',$commandFile)
    if($Snr){$arguments=@('-SelectEmuBySN',$Snr)+$arguments}
    foreach($argument in $arguments){if($argument -match '["\r\n]'){throw 'Unsafe Probe argument'}}
    $line=($arguments | ForEach-Object {'"'+$_+'"'}) -join ' '
    $native=$null;$timedOut=$false;$contact=$false;$active=$false
    $startedUtc=[DateTime]::UtcNow.ToString('o')
    try {
        $native=Start-Process -FilePath $JLinkExe -WorkingDirectory (Split-Path -Parent $JLinkExe) -ArgumentList $line -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $null=$native.Handle
        $clock=[Diagnostics.Stopwatch]::StartNew();$notice=-1
        while(!$native.HasExited){
            $text=Get-OmReadText @($stdout,$stderr)
            # Preserve the old per-process budget: reaching the CPUID command
            # permits Commander to finish its remaining fallback/read sequence.
            # This is NOT readable-target evidence for identity or retry policy.
            if(!$active -and $text -match '(?i)E000ED00'){$active=$true;Write-Host 'OM_PROBE_NATIVE_ACTIVE'}
            if($text -match '(?im)^E000ED00\s*=\s*[0-9A-F]{8}'){
                if(!$contact){Write-Host 'OM_PROBE_CONTACT'};$contact=$true
            }
            $tick=[int][Math]::Floor($clock.Elapsed.TotalSeconds/5)
            if(!$contact -and $tick -gt 0 -and $tick -ne $notice){$notice=$tick;Write-Host "OM_PROBE_WAIT Still detecting OM6626 ($([int]$clock.Elapsed.TotalSeconds)s); waiting for probe/target response."}
            $limit=if($active){300}else{$ContactLimit}
            if($clock.Elapsed.TotalSeconds -ge $limit){$timedOut=$true;break}
            Start-Sleep -Milliseconds 100;$native.Refresh()
        }
        if($timedOut){$native.Kill();if(!$native.WaitForExit(2000)){throw 'Probe process did not stop; refusing retry'}}else{$native.WaitForExit()}
        $text=Get-OmReadText @($stdout,$stderr)
        if($text -match '(?im)^E000ED00\s*=\s*[0-9A-F]{8}'){$contact=$true}
        $log=Join-Path $directory ($Stem+'.log')
        $merged=$text
        if($timedOut){$merged+="`nINKTAG_ATTEMPT_TIMEOUT after $limit second(s)"}
        $merged | Set-Content -LiteralPath $log -Encoding UTF8
        @{naturalExit=(!$timedOut);exitCode=$(if($timedOut){$null}else{$native.ExitCode});contactSeen=$contact;nativeActive=$active;elapsedSeconds=$clock.Elapsed.TotalSeconds;
            executable=$JLinkExe;arguments=$arguments;startedUtc=$startedUtc;endedUtc=[DateTime]::UtcNow.ToString('o');
            stdoutSHA256=(Get-FileHash -LiteralPath $stdout -Algorithm SHA256).Hash;stderrSHA256=(Get-FileHash -LiteralPath $stderr -Algorithm SHA256).Hash} |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $directory ($Stem+'.native.json')) -Encoding UTF8
        Write-Host "  Log: $log"
        return [pscustomobject]@{Text=$text;TimedOut=$timedOut;Contact=$contact;ExitCode=$(if($timedOut){$null}else{$native.ExitCode})}
    } finally {
        if($null -ne $native -and !$native.HasExited){$native.Kill();$null=$native.WaitForExit(2000)}
    }
}

try {
    if($FrequencyHz -lt 1000 -or $FrequencyHz%1000 -ne 0 -or $AttemptTimeoutSeconds -lt 3 -or $TimeoutSeconds -lt 1 -or $IntervalSeconds -lt 1){throw 'Invalid Probe transport limits'}
    # Initialize exactly once, not once per retry. The owned worker job covers
    # the version check too, so even initialization cancellation leaves no child.
    $JLinkExe=Resolve-InkTagJLinkTool -ToolName JLink.exe -ExplicitPath $JLinkExe -RepoRoot $repoRoot
    Assert-InkTagJLink788j -JLinkExe $JLinkExe
    $directory=Join-Path ([IO.Path]::GetTempPath()) ('inktag-om6626-probe-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff'))
    $null=New-Item -ItemType Directory -Path $directory
    Write-Host 'OM_PROBE_READY'
    $window=[Diagnostics.Stopwatch]::StartNew();$attempt=0
    do {
        if($attempt -gt 0 -and $window.Elapsed.TotalSeconds -ge $TimeoutSeconds){throw 'Probe detection window expired'}
        $attempt++;Write-Host "OM_PROBE_ATTEMPT $attempt"
        $limit=$AttemptTimeoutSeconds
        if($RetryDetect){$limit=[Math]::Min($limit,[Math]::Max(0,$TimeoutSeconds-$window.Elapsed.TotalSeconds))}
        $commands=(Get-OmProbeCommands)+@('mem32 0xE000ED00 1','mem32 0x40000000 2','mem32 0x00400000 4','mem32 0x00404000 4','mem32 0x0040A000 4')
        $result=Invoke-OmProbeNative $commands ('attempt-{0:D3}' -f $attempt) $limit
        $identity=[regex]::Match($result.Text,'(?im)^40000000\s*=\s*([0-9A-F]{8})\s+([0-9A-F]{8})')
        $errors=$result.Text -match '(?im)(cannot connect|could not connect|failed|unknown command|syntax error|^\s*ERROR|no emulator|no J-Link)'
        if(!$result.TimedOut -and $result.ExitCode -eq 0 -and $result.Contact -and !$errors){
            if(!$identity.Success -or ([Convert]::ToUInt32($identity.Groups[1].Value,16) -band 0xF00) -ne 0x200){throw 'Wrong SWD target: OM6626 SYS identity not verified'}
            # Same normal release command sequence as the former Probe backend.
            $release=Invoke-OmProbeNative ((Get-OmProbeCommands)+@('g')) 'release' $AttemptTimeoutSeconds
            if($release.TimedOut -or $release.ExitCode -ne 0 -or $release.Text -match '(?i)cannot connect|could not connect|failed|error|unknown command'){throw 'OM6626 identified, but release failed; no retry'}
            Write-Host 'Probe OK: OM6626 SWD target is readable.'
            Write-Host 'Target released after probe.'
            exit 0
        }
        $absence=$result.Text -match '(?i)failed to connect|could not connect|cannot connect|SW-DP.*(failed|not found)'
        $fatal=$result.Text -match '(?i)unknown command|syntax error|no emulator|no J-Link|access denied|license.*(expired|missing|invalid|error)'
        $emptyTimeout=$result.TimedOut -and !$result.Text.Trim()
        $retryText=if($emptyTimeout){"INKTAG_ATTEMPT_TIMEOUT after $AttemptTimeoutSeconds second(s)"}else{$result.Text}
        if(!$RetryDetect -or $result.Contact -or $identity.Success -or $fatal -or !(Test-OmDetectionRetryAllowed $retryText) -or $window.Elapsed.TotalSeconds -ge $TimeoutSeconds){throw 'Probe failed; original logs retained, no retry'}
        Write-Host ('OM_PROBE_RETRY '+$(if($emptyTimeout){'Probe contact timed out without native output; bounded detection retry. Original timeout/log retained.'}else{'Target not detected; bounded detection retry. Original failure/log retained.'}))
        $remaining=[Math]::Max(0,1000*$TimeoutSeconds-$window.Elapsed.TotalMilliseconds)
        Start-Sleep -Milliseconds ([int][Math]::Min(1000*$IntervalSeconds,$remaining))
    } while($true)
} catch { Write-Host ('ERROR: '+$_.Exception.Message);exit 1 }
