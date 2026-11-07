param([switch]$Worker, [string]$OptionsPath)

# Only Probe and the read-only J-Flash recipe use this process lifetime.
# The frozen programmer and its native commands are not imported or changed.
function New-OmReadJob {
    if (!('InkTag.OmReadJob' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace InkTag {
 public static class OmReadJob {
  [StructLayout(LayoutKind.Sequential)] public struct Basic {
   public long ProcessTime, JobTime; public uint Flags;
   public UIntPtr MinSet, MaxSet; public uint Active; public UIntPtr Affinity;
   public uint Priority, Scheduling;
  }
  [StructLayout(LayoutKind.Sequential)] public struct Io {
   public ulong ReadOps, WriteOps, OtherOps, ReadBytes, WriteBytes, OtherBytes;
  }
  [StructLayout(LayoutKind.Sequential)] public struct Limits {
   public Basic Basic; public Io Io;
   public UIntPtr ProcessMemory, JobMemory, PeakProcess, PeakJob;
  }
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr CreateJobObject(IntPtr attrs, string name);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool SetInformationJobObject(IntPtr job, int kind, ref Limits limits, uint size);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
 }
}
'@
    }
    $handle=[InkTag.OmReadJob]::CreateJobObject([IntPtr]::Zero,$null)
    if ($handle -eq [IntPtr]::Zero) { throw 'Cannot create owned read-process job.' }
    $job=[Microsoft.Win32.SafeHandles.SafeFileHandle]::new($handle,$true)
    $limits=New-Object InkTag.OmReadJob+Limits
    $basic=New-Object InkTag.OmReadJob+Basic
    $basic.Flags=0x2000 # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    $limits.Basic=$basic
    if (![InkTag.OmReadJob]::SetInformationJobObject($handle,9,[ref]$limits,[Runtime.InteropServices.Marshal]::SizeOf($limits))) {
        $job.Dispose(); throw 'Cannot configure owned read-process cleanup.'
    }
    return $job
}

function Start-OmReadWorker([hashtable]$Options,[string]$Directory) {
    $job=New-OmReadJob
    $process=$null
    try {
        $work=Join-Path $Directory ('worker-'+[Guid]::NewGuid().ToString('N'))
        $null=New-Item -ItemType Directory -Path $work
        $gate=Join-Path $work 'start.gate'
        $Options.Gate=$gate
        $inputFile=Join-Path $work 'options.clixml'
        $Options | Export-Clixml -LiteralPath $inputFile
        $stdout=Join-Path $work 'stdout.log'; $stderr=Join-Path $work 'stderr.log'
        $shell=[Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
        $workerScript=Join-Path $PSScriptRoot 'readonly-process.ps1'
        foreach($path in @($workerScript,$inputFile)) { if($path -match '["\r\n]'){throw 'Unsafe worker path'} }
        $line='-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$workerScript+'" -Worker -OptionsPath "'+$inputFile+'"'
        $process=Start-Process -FilePath $shell -ArgumentList $line -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $null=$process.Handle
        if (![InkTag.OmReadJob]::AssignProcessToJobObject($job.DangerousGetHandle(),$process.Handle)) {
            throw 'Cannot own read-worker process; no device operation was released.'
        }
        # The worker cannot start ANY native tool before job assignment.
        return [pscustomobject]@{Process=$process;Job=$job;Stdout=$stdout;Stderr=$stderr;Directory=$work;Gate=$gate}
    } catch {
        $job.Dispose()
        if($null -ne $process -and !$process.HasExited){$process.Kill();$null=$process.WaitForExit(2000)}
        throw
    }
}

function Enable-OmReadWorker($Session) { [IO.File]::WriteAllText($Session.Gate,'go') }

function Stop-OmReadWorker($Session) {
    if($null -eq $Session){return}
    $Session.Job.Dispose() # only this worker and its own descendants
    if(!$Session.Process.WaitForExit(2000)){throw 'Owned read worker did not exit; refusing another attempt.'}
}

function Get-OmReadText([string[]]$Paths) {
    $parts=@(foreach($path in $Paths){if(Test-Path -LiteralPath $path -PathType Leaf){
        @(Get-Content -LiteralPath $path -Raw -ErrorAction SilentlyContinue) -join ''
    }})
    return ($parts -join "`n")
}

function Show-OmReadWorkerOutput($Session,[hashtable]$Offsets,[switch]$Final) {
    foreach($path in @($Session.Stdout,$Session.Stderr)) {
        $content=Get-OmReadText @($path)
        $end=if($Final){$content.Length}else{$content.LastIndexOf("`n")+1}
        $offset=if($Offsets.ContainsKey($path)){$Offsets[$path]}else{0}
        if($end -gt $offset){
            foreach($line in ($content.Substring($offset,$end-$offset) -split '\r?\n')){if($line){Write-Host $line}}
            $Offsets[$path]=$end
        }
    }
}

if($Worker) {
    $ErrorActionPreference='Stop'
    try {
        $options=Import-Clixml -LiteralPath $OptionsPath
        $gateClock=[Diagnostics.Stopwatch]::StartNew()
        while(!(Test-Path -LiteralPath $options.Gate)) {
            if($gateClock.Elapsed.TotalSeconds -ge 10){throw 'Read worker was not released by its owner.'}
            Start-Sleep -Milliseconds 25
        }
        if($options.Kind -eq 'Probe') {
            $probeArguments=$options.ProbeArguments
            & (Join-Path $PSScriptRoot 'probe-native.ps1') @probeArguments
            exit $LASTEXITCODE
        }
        if($options.Kind -ne 'Backup'){throw 'Unknown read-worker kind'}
        $arguments=@($options.Arguments)
        foreach($argument in $arguments) {
            if($argument -match '["\r\n]' -or $argument -notmatch '^-(?:hide$|exit$|usb[^\s]+$|jlinkdevicesxmlpath.+|jflashlog.+|jlinklog.+|openprj.+|readrange0x00400000,0x004FFFFF$|saveas.+,0x00400000,0x004FFFFF$)') {
                throw 'Worker refuses a command outside the read-only J-Flash recipe.'
            }
        }
        if(@($arguments | Where-Object {$_ -eq '-readrange0x00400000,0x004FFFFF'}).Count -ne 1){throw 'Missing fixed read range'}
        $line=($arguments | ForEach-Object {'"'+$_+'"'}) -join ' '
        $native=Start-Process -FilePath $options.FilePath -ArgumentList $line -WindowStyle Hidden -PassThru -RedirectStandardOutput $options.Stdout -RedirectStandardError $options.Stderr
        $null=$native.Handle
        $native.WaitForExit()
        exit $native.ExitCode
    } catch { Write-Error $_; exit 1 }
}
