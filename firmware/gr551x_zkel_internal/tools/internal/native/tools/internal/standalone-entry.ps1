# Used by generated public proxies. Setup/check never execute a native tool.
function Invoke-InkTagGoodixEnvironment {
    param([string]$EntryRoot,[string]$Action,[System.Collections.IDictionary]$Parameters)
    $ErrorActionPreference='Stop'
    $bundle=Split-Path $EntryRoot -Parent
    $isBundle=Test-Path -LiteralPath (Join-Path $bundle 'inktag-goodix-tools.json') -PathType Leaf
    $guardPath=if($isBundle){Join-Path $EntryRoot 'internal/runtime/tools/powershell-cli.ps1'}else{Join-Path $EntryRoot '../../../../../tools/powershell-cli.ps1'}
    . $guardPath
    $guard=Invoke-InkTagCliGuard -ScriptPath (Join-Path $EntryRoot ($Action+'.ps1')) -HelpRequested ([bool]$Parameters['Help']) -RemainingArguments $Parameters['RemainingArguments'] -Summary 'GR tools-only setup/check: Windows x64 CPython3.12; offline file/import/hash checks only, no SDK/CLI/BLE/probe/erase. Import licensed locked native dependencies explicitly; no native execution.'
    if($null -ne $guard){$global:LASTEXITCODE=$guard;return}
    if(!$isBundle) {
        throw 'Use setup/check from a generated standalone tools-only ZIP. Developer SDK setup remains tools/sdk/setup-sdk.ps1.'
    }
    $runner=Join-Path $EntryRoot 'internal/standalone-runtime.py'
    $python=Join-Path $bundle '.env/Scripts/python.exe'
    if($Action -eq 'setup') { $python=if($Parameters['Python']){[string]$Parameters['Python']}else{'python'} }
    elseif(!(Test-Path -LiteralPath $python -PathType Leaf)) { $python='python' }
    $options=@('-I',$runner,$Action,'--bundle-root',$bundle)
    foreach($pair in @(@('JLinkHome','--jlink-home'),@('GProgrammerHome','--gprogrammer-home'))) {
        if($Parameters[$pair[0]]){$options+=@($pair[1],[string]$Parameters[$pair[0]])}
    }
    if($Parameters['Offline']){$options+='--offline'}
    if($Parameters['PythonOnly']){$options+='--python-only'}
    & $python @options
}

function Invoke-InkTagGoodixBundledEntry {
    param([string]$EntryRoot,[string]$Mcu,[string]$Action,[System.Collections.IDictionary]$Parameters)
    $ErrorActionPreference='Stop'
    $bundle=Split-Path $EntryRoot -Parent
    $runtime=Join-Path $EntryRoot 'internal/runtime'
    $entry=Join-Path $runtime ('firmware/ports/goodix/'+$Mcu+'/tools/'+$Action+'.ps1')
    # Help/usage remain available before dependency installation. The maintained
    # inner guard handles unknown switches and exits before its device path.
    $usage=$Parameters['Help'] -or (($Parameters.Keys -contains 'RemainingArguments') -and $null -ne $Parameters['RemainingArguments'] -and @($Parameters['RemainingArguments']).Count -gt 0)
    if($usage -or $Action -eq 'restore'){& $entry @Parameters;return}
    if($Action -eq 'flash' -and
       (!$Parameters['Config'] -or !$Parameters['Firmware'])) {
        throw '-Config and -Firmware are required; supply an external compatible signed app-full.zip. No hardware accessed.'
    }
    $python=Join-Path $bundle '.env/Scripts/python.exe'
    if(!(Test-Path -LiteralPath $python -PathType Leaf)){throw 'Run tools/setup.ps1 first; no global Python/CLI fallback'}
    $options=@('-I',(Join-Path $EntryRoot 'internal/standalone-runtime.py'),'check','--bundle-root',$bundle)
    if($Parameters['DryRun']){$options+='--python-only'}
    else {
        $options+=@('--native-group','jlink')
        if($Action -eq 'flash' -or ($Action -eq 'backup' -and $Mcu -eq 'gr551x')){
            $options+=@('--native-group','gprogrammer')
        }
    }
    $check=& $python @options
    if($LASTEXITCODE -ne 0){$check|Write-Output;throw 'Dependency/integrity check failed; no device accessed'}
    $forward=@{};foreach($key in $Parameters.Keys){$forward[$key]=$Parameters[$key]}
    if($forward['Python']){
        if($forward['Python'] -ne 'python' -and [IO.Path]::GetFullPath([string]$forward['Python']) -ne [IO.Path]::GetFullPath($python)){
            throw 'Use setup.ps1 -Python to select the isolated interpreter; per-operation global Python is refused'
        }
        $forward['Python']=$python
    }
    $jlink=Join-Path $runtime '.tools/jlink/7.88j/JLink.exe'
    if($forward['JLinkExe'] -and [IO.Path]::GetFullPath([string]$forward['JLinkExe']) -ne [IO.Path]::GetFullPath($jlink)){
        throw 'Use setup.ps1 -JLinkHome for the locked local dependency; arbitrary JLinkExe is refused'
    }
    $priorPath=$env:PATH;$priorPythonPath=$env:PYTHONPATH;$priorNoUser=$env:PYTHONNOUSERSITE
    $priorJlink=$env:INKTAG_JLINK_HOME
    try {
        $env:PATH=(Split-Path $python -Parent)+[IO.Path]::PathSeparator+$priorPath
        $env:PYTHONPATH='';$env:PYTHONNOUSERSITE='1';$env:INKTAG_JLINK_HOME=Split-Path $jlink -Parent
        & $entry @forward
    } finally {
        $env:PATH=$priorPath;$env:PYTHONPATH=$priorPythonPath;$env:PYTHONNOUSERSITE=$priorNoUser
        $env:INKTAG_JLINK_HOME=$priorJlink
    }
}
