# Internal bounded native polling, never used for reads, writes or cleanup.
function Get-InkTagGoodixCaptureHookText {
    param([switch]$Gr551xPackageCache)
    $text=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'manual-reset-capture.JLinkScript'))
    if(!$Gr551xPackageCache){return $text}
    $anchor='  r = JLINK_CORESIGHT_WriteAP(JLINK_CORESIGHT_AP_REG_ADDR, 0xE000EDF0);'
    if(($text.Split(@($anchor),[StringSplitOptions]::None)).Count -ne 2){throw 'Native halt anchor changed; no capture started'}
    $extension=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'gr551x-cache-ready.JLinkScript'))
    return ($extension+"`n"+$text.Replace($anchor,"  if (_Gr551xCacheReady() != 0) { return 1; }`n"+$anchor))
}

function Invoke-InkTagGoodixCaptureWindow {
    param([string]$Tool, [string[]]$Arguments, [string]$Stdout, [string]$Stderr,
          [string]$EvidenceDir, [switch]$Gr551xPackageCache)
    $sourceHook=Join-Path $PSScriptRoot 'manual-reset-capture.JLinkScript'
    $hook=Join-Path $EvidenceDir 'capture-hook.JLinkScript'
    if(Test-Path -LiteralPath $hook) { throw 'Capture hook evidence collision; refusing overwrite' }
    if($Gr551xPackageCache) {
        [IO.File]::WriteAllText($hook,(Get-InkTagGoodixCaptureHookText -Gr551xPackageCache))
    } else {Copy-Item -LiteralPath $sourceHook -Destination $hook -ErrorAction Stop}
    $hasher=[Security.Cryptography.SHA256]::Create()
    try { $hookSha=[BitConverter]::ToString($hasher.ComputeHash([IO.File]::ReadAllBytes($hook))).Replace('-','').ToLowerInvariant() }
    finally { $hasher.Dispose() }
    @{scriptSha256=$hookSha;
      activeWindowMs=30000;waitForPackageCache=[bool]$Gr551xPackageCache;startedUtc=[DateTime]::UtcNow.ToString('o')}|
        ConvertTo-Json|Set-Content -LiteralPath (Join-Path $EvidenceDir 'capture-window.json') -Encoding UTF8
    $trace=Join-Path $EvidenceDir 'capture-native.log'
    $Arguments+=@('-JLinkScriptFile',('"'+$hook+'"'),'-Log',('"'+$trace+'"'))
    $child=$null
    try {
        $child=Start-Process -FilePath $Tool -WindowStyle Hidden -PassThru -ArgumentList $Arguments `
            -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr
        # Keep the native handle alive so PS 5.1 retains the actual exit code.
        $nativeHandle=$child.Handle
        $announced=$false
        [long]$traceOffset=0
        $carry=''
        while(!$child.HasExited) {
            # The native trace is flushed while piped Commander stdout may not be.
            if(!$announced -and (Test-Path -LiteralPath $trace)) {
                # DAP polling quickly pushes READY out of a tail-only read.
                $stream=$null; $reader=$null
                try {
                    $stream=[IO.File]::Open($trace,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
                    [void]$stream.Seek($traceOffset,[IO.SeekOrigin]::Begin)
                    $reader=[IO.StreamReader]::new($stream)
                    $chunk=$carry+$reader.ReadToEnd()
                    $traceOffset=$stream.Position
                } finally {
                    if($null -ne $reader){$reader.Dispose()}elseif($null -ne $stream){$stream.Dispose()}
                }
                $carry=if($chunk.Length -gt 64){$chunk.Substring($chunk.Length-64)}else{$chunk}
                if($chunk -match 'GR_CAPTURE_READY:') {
                    Write-Host 'GR_CAPTURE_READY: continuous SWD capture active (30 s); press RESET now.'
                    $announced=$true
                }
            }
            Start-Sleep -Milliseconds 100
            $child.Refresh()
        }
        $child.WaitForExit()
        return [int]$child.ExitCode
    } finally {
        if($null -ne $child) {
            if(!$child.HasExited) { $child.Kill(); $child.WaitForExit() }
            $child.Dispose()
        }
    }
}

function Assert-InkTagGoodixCaptureResult {
    param([string]$Output)
    # J-Link 7.88j can fall back to generic attach after a hook compile error,
    # even with ExitOnError. A returned CPUID/native zero is not sufficient.
    if($Output -match '(?i)Error while evaluating J-Link script|Failed prepare script|Error while compiling') {
        throw 'Native capture script failed to compile/evaluate; no retry'
    }
    if($Output -match '(?i)GR_CAPTURE_WRONG_CPUID') {
        throw 'Stable non-Cortex-M4 CPUID detected; MCU mismatch, no Flash access or retry'
    }
    if($Output -match '(?i)Selected interface .*not supported') {
        throw 'J-Link interface is unsupported; no automatic retry'
    }
    if($Output -match '(?i)Communication timed out|Cannot connect to J-Link') {
        throw 'Cannot connect to target: J-Link/probe transport interrupted during detection; no Flash transfer started'
    }
    if($Output -match 'GR_CAPTURE_TIMEOUT: no initialized GR551x package cache' -or
       ($Output -match 'GR_PACKAGE_CACHE_WAIT:' -and $Output -match 'GR_CAPTURE_TIMEOUT:')) {
        throw 'MCU package cache did not become valid within the capture window; exact chip/capacity unconfirmed; no Flash operation'
    }
    if($Output -notmatch 'GR_CAPTURE_HALTED:') {
        if($Output -match 'GR_CAPTURE_TIMEOUT:' -and $Output -notmatch '(?im)E000ED00\s*=|00803214\s*=') {
            if($Output -match 'GR_CAPTURE_UNSTABLE:') {
                throw 'Cannot connect to target; native capture window expired with unstable SWD/core reads; no Flash access'
            }
            throw 'Cannot connect to target; native capture window expired'
        }
        throw 'Native capture did not confirm its early halt; no identity result'
    }
}
