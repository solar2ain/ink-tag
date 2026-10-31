# Internal hardware-only adapter. Public validation/retry/logging lives above it.
[CmdletBinding()]
param(
    [ValidateSet('probe','read','resume','publish')][string]$Action,
    [ValidateSet('gr5331','gr551x','gr5513','gr5515')][string]$Chip,
    [string]$EvidenceDir, [string]$ResultFile,
    [string]$JLinkExe='', [string]$Snr='', [int]$FrequencyHz=500000,
    [switch]$LeaveHalted, [string]$RequestFile='',
    [switch]$McuOnly, [switch]$CaptureWindow, [switch]$AfterVendorRead,
    [switch]$ReadProductionCache
)
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
try {
    if($McuOnly -and $Action -ne 'probe') { throw 'MCU mode is probe-only; never a read/write/cleanup mode' }
    if($CaptureWindow -and $Action -ne 'probe') { throw 'CaptureWindow is detection-only' }
    if($ReadProductionCache -and ($Action -ne 'probe' -or $McuOnly -or !$LeaveHalted)) { throw 'Production-cache inspection is internal halted Full preflight only' }
    if($Chip -eq 'gr551x' -and $Action -ne 'probe') { throw 'Family alias is detection-only; resolve actual MCU before Flash access' }
    if($AfterVendorRead -and ($Action -ne 'resume' -or $Chip -eq 'gr5331')) { throw 'Vendor cleanup is GR551x read-only maintenance only' }
    if($AfterVendorRead) {
        . (Join-Path $PSScriptRoot 'goodix-vendor-read.ps1')
        Invoke-InkTagGoodixVendorRead -RepoRoot $repoRoot -Chip $Chip -Snr $Snr -EvidenceDir $EvidenceDir -IdentityFile $RequestFile -Cleanup
        @{resumed=$true;method='one chip-aware vendor reset with named options after RAM read helper'}|ConvertTo-Json|Set-Content -LiteralPath $ResultFile -Encoding UTF8
        exit 0
    }
    if($Action -eq 'publish') {
        . (Join-Path $repoRoot 'tools/stock-backup.ps1')
        $request=Get-Content -LiteralPath $RequestFile -Raw | ConvertFrom-Json
        $session=$request.session
        $session.ExplicitOutput=$false
        $pair=[pscustomobject]@{Role='flash';First=$request.first;Second=$request.second;Bytes=$request.bytes}
        $classification=if($request.originalQualified){'stock'}else{'unknown'}
        $locks=@()
        try {
            foreach($file in @($pair.First,$pair.Second)) {
                Assert-InkTagBackupPath $file
                $locks+=@([IO.File]::Open($file,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read))
                if((Get-InkTagBackupSHA $file) -ne $request.sha256){throw 'Capture changed after provenance review; no publication'}
            }
            $destination=Complete-InkTagStockBackupSession -Session $session -Classification $classification `
                -ReadPairs @($pair) -Identity $request.identity -Ranges @($request.range) `
                -OriginalQualified ([bool]$request.originalQualified)
        } finally {foreach($stream in $locks){$stream.Dispose()}}
        @{directory=$destination}|ConvertTo-Json|Set-Content -LiteralPath $ResultFile -Encoding UTF8
        exit 0
    }
    if($Action -eq 'read') {
        if($Chip -ne 'gr5331') {
            . (Join-Path $PSScriptRoot 'goodix-vendor-read.ps1')
            Invoke-InkTagGoodixVendorRead -RepoRoot $repoRoot -Chip $Chip -Snr $Snr -EvidenceDir $EvidenceDir -IdentityFile $RequestFile
            exit 0
        }
        . (Join-Path $PSScriptRoot 'goodix-xip-read.ps1')
        Invoke-InkTagGoodixXipRead -RepoRoot $repoRoot -EvidenceDir $EvidenceDir `
            -JLinkExe $JLinkExe -Snr $Snr -FrequencyHz $FrequencyHz -IdentityFile $RequestFile
        exit 0
    }
    if($Chip -ne 'gr5331') {
        . (Join-Path $repoRoot 'firmware/ports/goodix/gr551x/tools/internal/gr551x-package.ps1')
        $identity=Get-InkTagGr551xPackage -RepoRoot $repoRoot -JLinkExe $JLinkExe -Snr $Snr `
            -FrequencyHz $FrequencyHz -RequireNoResetAttach:($Action -eq 'resume') -LeaveHalted:$LeaveHalted `
            -McuOnly:$McuOnly -CaptureWindow:$CaptureWindow -ReadProductionCache:$ReadProductionCache -EvidenceDir $EvidenceDir
        if(!$McuOnly -and $Chip -ne 'gr551x' -and $identity.ChipConfirmed -and $identity.Chip -ne $Chip){throw "Exact MCU mismatch: requested $Chip, detected $($identity.Chip)"}
        @{chip=$identity.Chip;chipConfirmed=[bool]$identity.ChipConfirmed;packageId=$identity.PackageId;packageName=$identity.PackageName;
            flashBytes=$(if($null -ne $identity.FlashSizeKB){$identity.FlashSizeKB*1024}else{$null});baseAddress=0x01000000;
            cpuid=$identity.CpuId;packageRaw=$identity.PackageRaw;identityReason=$identity.IdentityReason;identitySource=$identity.IdentitySource;
            probeSerial=$identity.ProbeSerial;
            resetFallbackObserved=$identity.ResetFallbackObserved;
            directAttachObserved=(!$identity.ResetFallbackObserved)}|ConvertTo-Json -Depth 5|
            Set-Content -LiteralPath $ResultFile -Encoding UTF8
        if($ReadProductionCache) {
            $fullIdentity=Get-Content -LiteralPath $ResultFile -Raw|ConvertFrom-Json
            $fullIdentity|Add-Member -NotePropertyName factorySnapshotFile -NotePropertyValue $identity.FactorySnapshotFile
            $fullIdentity|Add-Member -NotePropertyName factorySnapshotSha256 -NotePropertyValue $identity.FactorySnapshotSha256
            $fullIdentity|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $ResultFile -Encoding UTF8
        }
        exit 0
    }
    # Read the documented SRAM window; decode a uniquely valid SDK trim cache.
    # Its address is linker-dependent, unlike GR551x's eFuse cache.
    . (Join-Path $repoRoot 'tools/jlink-tools.ps1')
    $tool=Resolve-InkTagJLinkTool -ToolName 'JLink.exe' -ExplicitPath $JLinkExe -RepoRoot $repoRoot
    Assert-InkTagJLink788j -JLinkExe $tool
    if(Test-Path -LiteralPath $EvidenceDir){throw 'Probe evidence collision'}
    New-Item -ItemType Directory -Path $EvidenceDir -ErrorAction Stop|Out-Null
    $speed=[Math]::Max(1,[int][Math]::Ceiling($FrequencyHz/1000.0))
    $commandFile=Join-Path $EvidenceDir 'probe.jlink'
    # Commander otherwise resumes the CPU on close even without a GO command.
    # Hold the captured boot window across the probe -> read handoff. Explicit
    # GO below remains the sole successful ordinary-probe/cleanup resume.
    $commands=@('si SWD',"speed $speed",'SWDSelect','connect','halt','exec SetRestartOnClose = 0','mem32 0xE000ED00 1')
    $sramFile=Join-Path $EvidenceDir 'mcu-sram.bin'
    if($ReadProductionCache){$commands+=('savebin "'+$sramFile+'" 0x20000000 0x18000')}
    if(!$LeaveHalted){$commands+='go'}
    $commands+='qc'
    $commands|Set-Content -LiteralPath $commandFile -Encoding ASCII
    $arguments=@('-NoGui','1','-Device','Cortex-M4','-If','SWD','-Speed',"$speed",'-AutoConnect','1','-ExitOnError','1','-CommanderScript',('"'+$commandFile+'"'))
    if($Snr){$arguments=@('-SelectEmuBySN',$Snr)+$arguments}
    $stdout=Join-Path $EvidenceDir 'stdout.log';$stderr=Join-Path $EvidenceDir 'stderr.log'
    if($CaptureWindow) {
        . (Join-Path $PSScriptRoot 'goodix-capture-window.ps1')
        $nativeExit=Invoke-InkTagGoodixCaptureWindow -Tool $tool -Arguments $arguments -Stdout $stdout -Stderr $stderr -EvidenceDir $EvidenceDir
    } else {
        $process=Start-Process -FilePath $tool -WindowStyle Hidden -PassThru -Wait -ArgumentList $arguments `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $nativeExit=$process.ExitCode
    }
    $output=(Get-Content -LiteralPath $stdout -Raw)+(Get-Content -LiteralPath $stderr -Raw)
    if($CaptureWindow) { Assert-InkTagGoodixCaptureResult -Output $output }
    $resetFallbackObserved=$output -match '(?i)Connect fallback:.*Reset|Reset via Reset pin.*Connect'
    if($Action -eq 'resume' -and $resetFallbackObserved){throw 'Reset fallback refused during cleanup; not a direct attach'}
    if($output -match '(?i)Could not read memory'){throw 'Could not read memory; raw evidence retained'}
    if($nativeExit -ne 0){throw "J-Link probe failed (exit $nativeExit); raw evidence retained"}
    if($output -notmatch '(?im)E000ED00\s*=\s*([0-9a-f]{8})'){throw 'Missing CPUID; no confirmed target'}
    $cpuid=[Convert]::ToUInt32($Matches[1],16)
    if((($cpuid -shr 4)-band 0xFFF) -ne 0xC24 -or (($cpuid -shr 24)-band 0xFF) -ne 0x41){throw 'Non-Cortex-M4 identity refused'}
    $probeSerial=if($output -match '(?im)^\s*S/N:\s*(\d+)\s*$'){$Matches[1]}else{$Snr}
    if($Snr -and $probeSerial -and $probeSerial -ne $Snr){throw 'Actual J-Link serial mismatch; no Flash access'}
    . (Join-Path $PSScriptRoot 'gr533x-package.ps1')
    $package=if(Test-Path -LiteralPath $sramFile){Get-InkTagGr533xCachedPackage -Sram ([IO.File]::ReadAllBytes($sramFile))}else{$null}
    $identity=@{chip='UNIDENTIFIED-MCU';requestedChip=$Chip;chipConfirmed=$false;cpuid=('0x{0:X8}'-f $cpuid);
        flashBytes=$(if($McuOnly){$null}else{524288});baseAddress=0x00200000;
        capacitySource=$(if($McuOnly){'not detected'}else{'requested registered GR5331 diagnostic range only'});
        probeSerial=$probeSerial;resetFallbackObserved=$resetFallbackObserved;directAttachObserved=(!$resetFallbackObserved)}
    if($null -ne $package){
        foreach($key in $package.Keys){$identity[$key]=$package[$key]}
        $identity.capacitySource='SDK production trim: physical Flash size in KiB'
        Write-Host ('Detected MCU '+$package.packageName+'; 512 KiB Flash, 96 KiB RAM.')
    } else {Write-Host 'SWD/core responds; actual GR5331 package identity remains unconfirmed.'}
    if($ReadProductionCache) {
        $identity.factorySnapshotFile=$sramFile
        $hasher=[Security.Cryptography.SHA256]::Create()
        try {$identity.factorySnapshotSha256=[BitConverter]::ToString($hasher.ComputeHash([IO.File]::ReadAllBytes($sramFile))).Replace('-','').ToLowerInvariant()}
        finally {$hasher.Dispose()}
    }
    $identity|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $ResultFile -Encoding UTF8
    exit 0
} catch {
    # A failed ordinary RESET-assisted connection is still target absence.
    # Never normalize a memory/identity/tool error into a retryable failure.
    $connectionFailure=$_.Exception.Message -match '^(?:J-Link probe failed \(exit |Missing CPUID;|Unable to read the Goodix package ID over SWD\.|J-Link connected, but the Goodix package byte)'
    if($Action -eq 'probe' -and $connectionFailure -and (Test-Path -LiteralPath $EvidenceDir)) {
        $native=@(Get-ChildItem -LiteralPath $EvidenceDir -File -Filter '*.log' | ForEach-Object {Get-Content -LiteralPath $_.FullName -Raw}) -join "`n"
        if($native -match '(?i)Cannot connect to target|Could not connect to target' -and
           $native -notmatch '(?i)Could not read memory|(?:E000ED00|00803214)\s*=\s*[0-9a-f]+') {
            Write-Host 'Cannot connect to target; probe-only no-contact evidence retained.'
            exit 1
        }
    }
    Write-Host ("ERROR: {0}" -f $_.Exception.Message) -ForegroundColor Red
    exit 1
}
