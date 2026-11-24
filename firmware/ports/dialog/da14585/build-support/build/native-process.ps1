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


function Invoke-InkTagDaLoggedProcess {
    param([string]$Exe,[string[]]$Arguments,[string]$Log,[int]$OperationTimeoutSeconds=300,[string]$Label)
    if ((Test-Path -LiteralPath $Log) -or (Test-Path -LiteralPath ($Log+'.result.json'))) { throw 'Immutable process evidence exists.' }
    $info=[Diagnostics.ProcessStartInfo]::new()
    $info.FileName=$Exe
    $info.Arguments=(($Arguments | ForEach-Object {ConvertTo-InkTagDaNativeArgument $_}) -join ' ')
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    $child=[Diagnostics.Process]::new();$child.StartInfo=$info
    $started=$false;$timedOut=$false;$nativeExit=$null;$combined=''
    try {
        $started=$child.Start();if(!$started){throw 'Process failed to start.'}
        $stdout=$child.StandardOutput.ReadToEndAsync();$stderr=$child.StandardError.ReadToEndAsync()
        $timedOut=!$child.WaitForExit($OperationTimeoutSeconds*1000)
        if($timedOut){$child.Kill();if(!$child.WaitForExit(5000)){throw 'Timed-out child did not exit.'}}
        $nativeExit=[int]$child.ExitCode
        $combined=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
    } finally {
        [IO.File]::WriteAllText($Log,$Exe+' '+$info.Arguments+"`n"+$combined,(New-Object Text.UTF8Encoding($false)))
        [IO.File]::WriteAllText(($Log+'.result.json'),(@{childStarted=$started;nativeExitCode=$nativeExit;timedOut=$timedOut}|ConvertTo-Json))
        $child.Dispose()
    }
    return @{ExitCode=$nativeExit;NativeExitCode=$nativeExit;TimedOut=$timedOut;Text=$combined}
}
