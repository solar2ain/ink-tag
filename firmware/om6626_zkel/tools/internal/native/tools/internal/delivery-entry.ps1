param([ValidateSet('probe','backup','flash')][string]$Action,[object[]]$ForwardArguments)
try {
    . (Join-Path $PSScriptRoot 'delivery-common.ps1')
    $root=Get-OmDeliveryRoot
    $bundle=Assert-OmBundleFiles $root
    $native=Join-Path $root ('tools/internal/runtime/firmware/ports/onmicro/om6626/tools/'+$Action+'.ps1')
    # Parse exact names without executing the backend. Abbreviations, duplicate
    # and inline switches cannot disguise a real operation as Help or DryRun.
    $parameters=(Get-Command -Name $native -CommandType ExternalScript).Parameters
    $forward=[Collections.Generic.List[string]]::new()
    $seen=@{}
    for($n=0;$n -lt $ForwardArguments.Count;$n++) {
        $token=[string]$ForwardArguments[$n]
        if($token -in @('-h','--help')) {$token='-Help'}
        if($token -notmatch '^-[A-Za-z][A-Za-z0-9]*$') {throw "Use exact named parameters without inline values: $token"}
        $key=$token.Substring(1)
        if(!$parameters.ContainsKey($key) -or $key -eq 'RemainingArguments') {throw "Unknown parameter: $token"}
        if($seen.ContainsKey($key)) {throw "Duplicate parameter: $token"}
        $seen[$key]=$true
        $forward.Add($token)
        if($parameters[$key].ParameterType -ne [Management.Automation.SwitchParameter]) {
            $n++
            if($n -ge $ForwardArguments.Count) {throw "Missing value: $token"}
            $forward.Add([string]$ForwardArguments[$n])
        }
    }
    function Find-OmArgument([string]$Name) {
        for($i=0;$i -lt $forward.Count;$i++) {if($forward[$i] -ieq ('-'+$Name)) {return $i}}
        return -1
    }
    function Read-OmArgument([string]$Name) {
        $i=Find-OmArgument $Name
        if($i -lt 0) {return ''}
        if($i+1 -ge $forward.Count) {throw "Missing value for -$Name"}
        return $forward[$i+1]
    }
    $help=(Find-OmArgument 'Help') -ge 0 -or (Find-OmArgument 'h') -ge 0
    $dry=(Find-OmArgument 'DryRun') -ge 0
    if($help) {
        Write-Output ('Exported '+$Action+' entry: '+$bundle.firmwareFamily+'/'+($bundle.supportedLayouts -join ',')+'; tools '+$bundle.toolsVersion)
        Write-Output 'Tools-only: no firmware included, selected or downloaded. Flash requires explicit -Firmware <external-signed-Full.zip> and -Config <matching PCB.toml>. Probe/backup need no firmware.'
        Write-Output 'Use complete parameter names. RetryDetect defaults OFF. Below: unchanged native backend help.'
    }
    if(!$help -and $Action -eq 'backup' -and (Find-OmArgument 'OutputDir') -lt 0) {
        $board=Read-OmArgument 'Board'
        if($board -notmatch '^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$') {throw 'A valid exact -Board marking is required'}
        $forward.Add('-OutputDir');$forward.Add((Join-Path $root ('backups/'+$board)))
    }
    if(!$help -and $Action -eq 'flash') {
        if([string]::IsNullOrWhiteSpace((Read-OmArgument 'Firmware'))) {throw 'Explicit external -Firmware <signed-Full.zip> is required; no programmer opened'}
        $config=Read-OmArgument 'Config'
        if(!$config) {throw '-Config <PCB-marking.toml> is required; see tools/configs'}
        if(!(Test-Path -LiteralPath $config -PathType Leaf)) {
            $i=Find-OmArgument 'Config'
            $config=Join-Path $root ('tools/configs/'+$config)
            if(!(Test-Path -LiteralPath $config -PathType Leaf)) {throw 'Selected config not found'}
            $forward[$i+1]=$config
        }
        $pythonExe=Get-OmEnvironmentPython $root (Read-OmArgument 'Python')
        & $pythonExe -I (Join-Path $PSScriptRoot 'delivery-runtime.py') --root $root --check --firmware (Read-OmArgument 'Firmware') --config $config
        if($LASTEXITCODE -ne 0) {throw 'Offline firmware/config/dependency admission failed; no programmer opened'}
        if((Find-OmArgument 'Python') -lt 0) {$forward.Add('-Python');$forward.Add($pythonExe)}
    }
    $manualImport=$Action -eq 'backup' -and (Find-OmArgument 'Source') -ge 0
    if(!$help -and !$dry -and !$manualImport) {
        $explicit=Read-OmArgument 'JLinkExe'
        $nativeDirectory=if($explicit) {Split-Path -Parent ([IO.Path]::GetFullPath($explicit))} else {Get-OmNativeHome}
        if($explicit -and [IO.Path]::GetFullPath($explicit) -ine (Join-Path $nativeDirectory 'JLink.exe')) {
            throw 'JLinkExe must be the pinned JLink.exe, not another executable in the same directory'
        }
        Assert-OmNative $nativeDirectory
        if($Action -ne 'probe') {Assert-OmDevicePack (Get-OmPackRoot)}
        $jflash=Read-OmArgument 'JFlashExe'
        if($jflash -and [IO.Path]::GetFullPath($jflash) -ine (Join-Path $nativeDirectory 'JFlash.exe')) {throw 'JFlash.exe must come from the pinned J-Link installation'}
        if(!$explicit) {$forward.Add('-JLinkExe');$forward.Add((Join-Path $nativeDirectory 'JLink.exe'))}
    }
    # Run unchanged backend using the same PowerShell host. No private
    # hardware implementation, implicit retry or implicit confirmation.
    $shell=[Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
    & $shell -NoProfile -ExecutionPolicy Bypass -File $native @forward
    exit $LASTEXITCODE
} catch {Write-Error $_.Exception.Message;exit 1}
