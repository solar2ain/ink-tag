# Pure host helpers; importing does not resolve/open a debugger or mutate data.
$script:DaProgressActive=$false
function ConvertTo-InkTagDaNativeArgument {
    param([AllowEmptyString()][string]$Value)
    # Start-Process joins ArgumentList without quoting. Follow Windows CRT
    # escaping, including trailing backslashes and embedded double quotes.
    $result = '"'; $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') { $slashes++; continue }
        if ($character -eq '"') { $result += ('\' * ($slashes * 2 + 1)) + '"' }
        else { $result += ('\' * $slashes) + $character }
        $slashes = 0
    }
    return $result + ('\' * ($slashes * 2)) + '"'
}

function Get-InkTagDaTerminalState {
    $state=@{UICulture=[Threading.Thread]::CurrentThread.CurrentUICulture}
    try {$state.Foreground=[Console]::ForegroundColor;$state.Background=[Console]::BackgroundColor} catch {}
    try {$state.HostForeground=$Host.UI.RawUI.ForegroundColor;$state.HostBackground=$Host.UI.RawUI.BackgroundColor} catch {}
    return $state
}
function Start-InkTagDaTerminal {
    $state=Get-InkTagDaTerminalState
    [Threading.Thread]::CurrentThread.CurrentUICulture=[Globalization.CultureInfo]::GetCultureInfo('en-US')
    return $state
}
function Restore-InkTagDaTerminal {
    param($State)
    if($null -eq $State){return}
    try {Complete-InkTagDaProgress} finally {
        try {
            if($State.ContainsKey('HostBackground')){$Host.UI.RawUI.BackgroundColor=$State.HostBackground}
            if($State.ContainsKey('HostForeground')){$Host.UI.RawUI.ForegroundColor=$State.HostForeground}
        } catch {}
        try {
            if($State.ContainsKey('Background')){[Console]::BackgroundColor=$State.Background}
            if($State.ContainsKey('Foreground')){[Console]::ForegroundColor=$State.Foreground}
        } catch {}
        if($State.ContainsKey('UICulture')){[Threading.Thread]::CurrentThread.CurrentUICulture=$State.UICulture}
    }
}
function Test-InkTagDaInteractiveTerminal {
    try {return $Host.Name -eq 'ConsoleHost' -and ![Console]::IsOutputRedirected -and ![Console]::IsErrorRedirected} catch {return $false}
}
function Complete-InkTagDaProgress {
    if($script:DaProgressActive){try {Write-Host ''} finally {$script:DaProgressActive=$false}}
}
function Write-InkTagDaLog {
    param([ValidateSet('INFO','DEBUG','WARNING','ERROR','SUCCESS')][string]$Level,
          [string]$Message, [switch]$NoColor, [switch]$ShowDebug)
    if($Level -eq 'DEBUG' -and !$ShowDebug -and $VerbosePreference -ne 'Continue'){return}
    $plain=($Message -replace '\x1b\[[0-?]*[ -/]*[@-~]','') -replace '[\x00-\x1f\x7f]',' '
    $line='[{0}] [{1}] {2}' -f [DateTimeOffset]::Now.ToString('yyyy-MM-dd HH:mm:sszzz'),$Level,$plain
    $state=Get-InkTagDaTerminalState
    try {
        Complete-InkTagDaProgress
        if($NoColor -or (Test-Path Env:NO_COLOR) -or !(Test-InkTagDaInteractiveTerminal)){Write-Host $line}
        else {Write-Host $line -ForegroundColor (@{INFO='Cyan';DEBUG='DarkGray';WARNING='Yellow';ERROR='Red';SUCCESS='Green'}[$Level])}
    } finally {Restore-InkTagDaTerminal $state}
}
function Write-InkTagDaStage {
    param([string]$Name)
    Write-InkTagDaLog INFO ("-------------------- {0} --------------------" -f $Name)
}
function Test-InkTagDaConfirmationInput {
    try {return $Host.Name -eq 'ConsoleHost' -and ![Console]::IsInputRedirected} catch {return $false}
}
function Confirm-InkTagDaFull {
    # Confirmation belongs in the visible PowerShell parent, never inside a
    # child whose stdout is captured/filtered. No debugger has been opened.
    if(!(Test-InkTagDaConfirmationInput)){
        throw 'Interactive confirmation input unavailable; no target accessed. Explicit -Yes is required for unattended use.'
    }
    Write-InkTagDaLog WARNING 'Continue? [y/N]'
    $answer=[string](Read-Host)
    return $answer.Trim() -match '^(?i:y|yes)$'
}
function Format-InkTagDaProgressLine {
    param([string]$Message,[string]$Prefix,[int]$Columns)
    if(("$Prefix $Message").Length -le $Columns){return "$Prefix $Message"}
    if($Message.Length -le $Columns){return $Message}
    return $null # Preserve complete facts on a new line instead of truncation.
}
function Write-InkTagDaProgress {
    param([hashtable]$State)
    $seconds=[int]$State.seconds
    if($State.ContainsKey('percent')){
        $percent=$State.percent;$filled=[int][Math]::Floor($percent/5)
        $bar=('#'*$filled)+('.'*(20-$filled));$detail="$($State.action) $percent%"
    } else {
        $position=$seconds%34;if($position -gt 17){$position=34-$position}
        $bar=('.'*$position)+'###'+('.'*(17-$position));$detail='Waiting; percentage unavailable'
    }
    $message="$($State.label) : [$bar] $detail | elapsed ${seconds}s"
    $width=0;if(Test-InkTagDaInteractiveTerminal){try {$width=[Console]::WindowWidth-1} catch {}}
    $prefix='[{0}] [INFO]' -f [DateTimeOffset]::Now.ToString('yyyy-MM-dd HH:mm:sszzz')
    $line=Format-InkTagDaProgressLine $message $prefix $width
    if($width -lt 40 -or $null -eq $line){Write-InkTagDaLog INFO $message;return}
    $state=Get-InkTagDaTerminalState
    try {
        $script:DaProgressActive=$true
        if(Test-Path Env:NO_COLOR){Write-Host ("`r"+$line.PadRight($width)) -NoNewline}
        else {Write-Host ("`r"+$line.PadRight($width)) -NoNewline -ForegroundColor Cyan}
    } finally {
        # Restore colours without terminating the live progress line.
        try {if($state.ContainsKey('Foreground')){[Console]::ForegroundColor=$state.Foreground}} catch {}
        try {if($state.ContainsKey('HostForeground')){$Host.UI.RawUI.ForegroundColor=$state.HostForeground}} catch {}
    }
}
function Show-InkTagDaToolLine {
    param([string]$Line,[hashtable]$Progress,[bool]$Detailed)
    # Display filtering does not classify operation success or edit raw logs.
    if($Line -match '^FULL_STAGE: ([A-Z /]+)$'){
        Write-InkTagDaStage $Matches[1]
    } elseif($Line -match '^Readiness attempt [0-9]+ refused: .+; retrying detection only$'){
        Write-InkTagDaLog INFO $Line
    } elseif($Line -match '^WARNING: (.+)$'){
        Write-InkTagDaLog WARNING $Matches[1]
    } elseif($Line -match '^\s*pass ([12])/2: 0x00000000-0x0003FFFF\s*$'){
        $confirmed=[int]$Matches[1]*262144
        Write-InkTagDaLog INFO ("Read confirmed: $confirmed/524288 bytes; elapsed $($Progress.seconds)s (capture, not provenance)")
    } elseif($Line -match '^(?i)(Reading|Programming|Erasing|Comparing|Verifying)[^\r\n]*?(?<![-\d.])(\d{1,3}(?:\.\d+)?)%'){
        $value=[double]::Parse($Matches[2],[Globalization.CultureInfo]::InvariantCulture)
        if($value -le 100){$Progress.percent=$value;$Progress.action=$Matches[1];Write-InkTagDaProgress $Progress}
    } elseif($Line -match '(?i)^\s*(?:Error:|ERROR:|Traceback|Failed\b|INKTAG_OPERATION_TIMEOUT)'){
        Write-InkTagDaLog ERROR $Line
    } elseif($Line -match '^(?:Backup OK:|SHA256:|Classification:)'){
        Write-InkTagDaLog INFO $Line
    } else {Write-InkTagDaLog DEBUG $Line -ShowDebug:$Detailed}
}
function Receive-InkTagDaToolChunk {
    param([string]$Chunk,[hashtable]$State,$Lines,[hashtable]$Progress,[bool]$Detailed,[switch]$Final)
    foreach($character in $Chunk.ToCharArray()){
        if($character -eq "`r" -or $character -eq "`n"){
            if(!($character -eq "`n" -and $State.afterCr)){
                $Lines.Add($State.raw.ToString());Show-InkTagDaToolLine $State.display.ToString() $Progress $Detailed
            }
            $null=$State.raw.Clear();$null=$State.display.Clear();$State.afterCr=($character -eq "`r")
        } else {
            $State.afterCr=$false;$null=$State.raw.Append($character)
            if($character -eq "`b"){
                if($State.display.Length){$null=$State.display.Remove($State.display.Length-1,1)}
            } else {
                $null=$State.display.Append($character)
                if($character -eq '%'){Show-InkTagDaToolLine $State.display.ToString() $Progress $Detailed}
            }
        }
    }
    if($Final -and $State.raw.Length){
        $Lines.Add($State.raw.ToString());Show-InkTagDaToolLine $State.display.ToString() $Progress $Detailed
        $null=$State.raw.Clear();$null=$State.display.Clear()
    }
}
function Stop-InkTagDaOwnedProcess {
    param([Diagnostics.Process]$Process)
    if($Process.HasExited){return}
    if($Process.PSObject.Methods['Kill'].OverloadDefinitions -match 'bool'){$Process.Kill($true)}
    else {
        # PS5.1 lacks Kill(entireProcessTree). Only this owned child's PID is
        # selected, never an executable name or another attached programmer.
        $kill=[Diagnostics.ProcessStartInfo]::new()
        $kill.FileName=Join-Path ([Environment]::SystemDirectory) 'taskkill.exe'
        $kill.Arguments='/PID '+$Process.Id+' /T /F';$kill.UseShellExecute=$false;$kill.CreateNoWindow=$true
        $kill.RedirectStandardOutput=$true;$kill.RedirectStandardError=$true
        $worker=[Diagnostics.Process]::Start($kill)
        try {
            if(!$worker.WaitForExit(5000)){
                # Bound the termination helper itself and keep both PIDs owned.
                if(!$Process.HasExited){$Process.Kill()}
                if(!$worker.HasExited){$worker.Kill()}
                throw 'Owned child-tree termination did not finish; cleanup outcome unknown'
            }
        } finally {$worker.Dispose()}
        if(!$Process.HasExited){$Process.Kill()}
    }
}
function Invoke-InkTagDaLoggedProcess {
    param([Parameter(Mandatory)][string]$Exe,[string[]]$Arguments=@(),[string]$Log='',
          [ValidateScript({$_ -gt 0 -and $_ -le 86400})][double]$OperationTimeoutSeconds=60,
          [string]$Label='Operation',[switch]$Detailed)
    $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$Exe
    $start.Arguments=(($Arguments|ForEach-Object {ConvertTo-InkTagDaNativeArgument $_}) -join ' ')
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $start.RedirectStandardInput=$true
    $process=[Diagnostics.Process]::new();$process.StartInfo=$start
    $lines=[Collections.Generic.List[string]]::new();$watch=[Diagnostics.Stopwatch]::StartNew()
    $writer=$null;$resultWriter=$null;$started=$false;$timedOut=$false;$nativeExit=$null;$exitCode=1;$failure='';$drainDeadline=$null
    $childPid=$null;$childExitObserved=$false;$terminationAttempted=$false;$terminationError='';$outputDrainCompleted=$false
    try {
        if($Log){
            if(Test-Path -LiteralPath ($Log+'.result.json')){throw 'Result evidence exists; no overwrite or replay'}
            $stream=[IO.File]::Open($Log,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
            $writer=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false));$writer.AutoFlush=$true
            $resultStream=[IO.File]::Open($Log+'.result.json',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
            $resultWriter=[IO.StreamWriter]::new($resultStream,[Text.UTF8Encoding]::new($false))
        }
        Write-InkTagDaLog DEBUG ('Tool: '+$Exe+' '+$start.Arguments) -ShowDebug:$Detailed
        # The native-operation deadline starts at launch, after local evidence
        # files and terminal formatting are ready. Host setup must not consume
        # a short child budget before the child has even been launched.
        $watch.Restart()
        if(!$process.Start()){throw 'Cannot start host tool'};$started=$true;$childPid=$process.Id
        # Native children are noninteractive. The visible PowerShell entry
        # owns confirmations; unexpected input() must receive EOF and fail,
        # rather than wait invisibly until the overall Full deadline.
        $process.StandardInput.Close()
        $progress=@{label=$Label;seconds=0;action='Waiting'};$streams=@();$lastHeartbeat=-10
        foreach($reader in @($process.StandardOutput,$process.StandardError)){
            $buffer=[char[]]::new(4096)
            $streams+=@{reader=$reader;buffer=$buffer;task=$reader.ReadAsync($buffer,0,$buffer.Length);
                state=@{raw=[Text.StringBuilder]::new();display=[Text.StringBuilder]::new();afterCr=$false}}
        }
        while(@($streams|Where-Object {$null -ne $_.task}).Count -or !$process.HasExited){
            $progress.seconds=[int][Math]::Floor($watch.Elapsed.TotalSeconds)
            foreach($item in $streams){
                if($null -ne $item.task -and $item.task.IsCompleted){
                    $count=$item.task.GetAwaiter().GetResult()
                    if($count -gt 0){
                        $chunk=[string]::new($item.buffer,0,$count)
                        if($writer){$writer.Write($chunk)}
                        Receive-InkTagDaToolChunk $chunk $item.state $lines $progress ([bool]$Detailed)
                        $item.task=$item.reader.ReadAsync($item.buffer,0,$item.buffer.Length)
                    } else {Receive-InkTagDaToolChunk '' $item.state $lines $progress ([bool]$Detailed) -Final;$item.task=$null}
                }
            }
            if(!$timedOut -and $watch.Elapsed.TotalSeconds -ge $OperationTimeoutSeconds){
                $timedOut=$true;$exitCode=124
                $message="INKTAG_OPERATION_TIMEOUT: ${OperationTimeoutSeconds}s; result unknown, no automatic replay."
                $lines.Add($message);Write-InkTagDaLog ERROR $message
                # A native USB call may not acknowledge termination. Attempt
                # owned-child cleanup once, retain its outcome, then return a
                # terminal timeout. Never restart Waiting or repeat cleanup in
                # finally; neither a kill request nor host exit restores DAP.
                $terminationAttempted=$true
                try {Stop-InkTagDaOwnedProcess $process} catch {$terminationError=$_.Exception.Message}
                $drainDeadline=$watch.Elapsed.TotalSeconds+3
            }
            $interval=if(Test-InkTagDaInteractiveTerminal){1}else{10}
            if(!$timedOut -and $progress.seconds -ge $lastHeartbeat+$interval){$lastHeartbeat=$progress.seconds;Write-InkTagDaProgress $progress}
            if($timedOut -and $null -ne $drainDeadline -and $watch.Elapsed.TotalSeconds -ge $drainDeadline){break}
            Start-Sleep -Milliseconds 20
        }
        $outputDrainCompleted=@($streams|Where-Object {$null -ne $_.task}).Count -eq 0
        $childExitObserved=$process.WaitForExit(1000)
        if(!$childExitObserved){
            $failure='Host tool exit unconfirmed; result and DAP cleanup unknown, no replay'
            if(!$timedOut){throw $failure}
        } else {$nativeExit=$process.ExitCode}
        $exitCode=if($timedOut){124}else{$nativeExit}
        if($timedOut){
            $outcome=if($childExitObserved){'observed'}else{'UNCONFIRMED'}
            Write-InkTagDaLog WARNING ("Stopped on timeout; child PID=$childPid exit=$outcome; result/DAP state unknown; no replay.")
        }
    } catch {
        $failure=$_.Exception.Message
        throw
    } finally {
        try {
            try {Complete-InkTagDaProgress} finally {
                try {
                    if($started -and !$process.HasExited -and !$terminationAttempted){
                        $terminationAttempted=$true
                        try {Stop-InkTagDaOwnedProcess $process} catch {$terminationError=$_.Exception.Message}
                        $childExitObserved=$process.WaitForExit(1000)
                    }
                } finally {
                    if($writer){$writer.Dispose()}
                    # .NET Framework StreamReader.Dispose can wait on an
                    # outstanding ReadAsync held by a wedged native child.
                    # Unknown cleanup is terminal: leave those pipe handles to
                    # host exit instead of blocking here after the deadline.
                    if(!$started -or $outputDrainCompleted){$process.Dispose()}
                }
            }
        } finally {
            if($resultWriter){
                try {
                    $metadata=@{exitCode=$exitCode;nativeExitCode=$nativeExit;timedOut=$timedOut;error=$failure;elapsedSeconds=$watch.Elapsed.TotalSeconds;operationTimeoutSeconds=$OperationTimeoutSeconds;
                        childPid=$childPid;childExitObserved=$childExitObserved;terminationAttempted=$terminationAttempted;terminationError=$terminationError;outputDrainCompleted=$outputDrainCompleted;
                        pipeDisposalDeferred=($started -and !$outputDrainCompleted)}
                    $resultWriter.Write(($metadata|ConvertTo-Json))
                } finally {$resultWriter.Dispose()}
            }
        }
    }
    return [pscustomobject]@{ExitCode=$exitCode;NativeExitCode=$nativeExit;TimedOut=$timedOut;Lines=$lines.ToArray();Text=($lines -join "`n");Log=$Log}
}

function New-InkTagDaToolLogDirectory {
    param([Parameter(Mandatory)]$Context,[string]$OutputDir='')
    $base=if($OutputDir){[IO.Path]::GetFullPath($OutputDir)}elseif($Context.Portable){Join-Path $Context.Root 'logs'}else{Join-Path $Context.Root '.local/da-tool-logs'}
    $path=[IO.Path]::GetFullPath((Join-Path $base ([Guid]::NewGuid().ToString('N'))))
    $cursor=$path
    while($cursor){
        if(Test-Path -LiteralPath $cursor){
            if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Log path contains a link/reparse point'}
        }
        $parent=Split-Path -Parent $cursor;if($parent -eq $cursor){break};$cursor=$parent
    }
    $null=New-Item -ItemType Directory -Path $path
    return $path
}

function Invoke-InkTagDaReadiness {
    param([Parameter(Mandatory)][scriptblock]$Attempt, [switch]$RetryDetect,
          [int]$IntervalSeconds = 1, [int]$TimeoutSeconds = 0)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $count = 0
    do {
        if($count -gt 0 -and $TimeoutSeconds -gt 0 -and $watch.Elapsed.TotalSeconds -ge $TimeoutSeconds){
            throw 'Readiness deadline expired; no further attempt or target operation.'
        }
        $count++
        $remaining=if($TimeoutSeconds -gt 0){[Math]::Max(0.001,$TimeoutSeconds-$watch.Elapsed.TotalSeconds)}else{0}
        $reply = & $Attempt $remaining
        $result = $reply.result
        $safe=$null -ne $result
        foreach($field in @('resetCalled','cpuHaltCalled','nativeConnectCalled','cpuOrPeripheralWrites')){
            if($null -eq $result -or $result.$field -isnot [bool] -or $result.$field){$safe=$false}
        }
        if ($reply.exitCode -eq 0 -and $null -ne $result -and $result.ok -is [bool] -and $result.ok -and !$result.fatal -and
            $result.chipId -eq '585' -and $result.cpuid -eq '0x410CC200' -and
            [string]$result.serial -match '^[0-9]{4,16}$' -and
            $safe -and $null -ne $result.cleanupErrors -and $result.cleanupErrors.Count -eq 0) { return $result }
        # Only structured, nonfatal readiness refusal is retryable. Neither
        # capture nor programming is invoked by this loop, even after success.
        if ($null -eq $result -or $reply.exitCode -ne 1 -or
            !$safe -or $result.ok -isnot [bool] -or $result.ok -or $null -eq $result.cleanupErrors -or
            $result.fatal -or $result.cleanupErrors.Count -gt 0 -or
            $result.error -notin @('DAP power ACK unavailable; target may be asleep',
                                   'SW-DP ID unavailable; check target power and SWD',
                                   'Selected probe unavailable; no fallback')) {
            $detail=if($null -ne $result -and $result.error){[string]$result.error}else{'exit='+[string]$reply.exitCode}
            $step=if($null -ne $result -and $result.failedStep){[string]$result.failedStep}else{'unknown-step'}
            throw "Terminal/unknown probe result at ${step}: $detail; no retry or reset fallback."
        }
        if (!$RetryDetect -or ($TimeoutSeconds -gt 0 -and $watch.Elapsed.TotalSeconds -ge $TimeoutSeconds)) {
            throw 'Target not readable; no capture/write attempted. Check SWD/power or ordinary shared-CLI debug grace.'
        }
        Write-InkTagDaLog INFO ('Readiness attempt {0} refused; elapsed {1:N1}s; retrying detection only' -f $count,$watch.Elapsed.TotalSeconds)
        $delay=$IntervalSeconds*1000
        if($TimeoutSeconds -gt 0){$delay=[Math]::Min($delay,[Math]::Max(0,$TimeoutSeconds*1000-$watch.Elapsed.TotalMilliseconds))}
        Start-Sleep -Milliseconds ([int]$delay)
    } while ($true)
}

function Invoke-InkTagDaInternal {
    param([Parameter(Mandatory)][string]$Script, [string[]]$Arguments,
          [Parameter(Mandatory)][string]$Log, [switch]$Detailed,
          [ValidateRange(1,86400)][int]$OperationTimeoutSeconds=420)
    $shell = (Get-Process -Id $PID).Path
    $reply=Invoke-InkTagDaLoggedProcess -Exe $shell -Arguments (@('-NoProfile','-ExecutionPolicy','Bypass','-File',$Script)+$Arguments) `
        -Log $Log -OperationTimeoutSeconds $OperationTimeoutSeconds -Label 'Capture' -Detailed:$Detailed
    $nativeExit=$reply.ExitCode
    $global:LASTEXITCODE=$nativeExit
    Set-Variable -Name LASTEXITCODE -Scope 1 -Value $nativeExit
    if ($nativeExit -ne 0) { throw "Operation failed (native exit $nativeExit); partial evidence retained; no replay. Log: $Log" }
    return $nativeExit
}
