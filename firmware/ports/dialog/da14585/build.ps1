[CmdletBinding()]
param(
    [string]$RepoRoot = "",
    [string]$KeilRoot = "$env:LOCALAPPDATA\Keil_v5",
    [Parameter(Mandatory=$true)][string]$SdkRoot,
    [string]$Output,
    [ValidateSet("da14585_aesl")]
    [string]$Board = "da14585_aesl",
    [ValidateRange(10, 1800)]
    [int]$BuildTimeoutSeconds = 300
)

$ErrorActionPreference = "Stop"
if (!$RepoRoot) { $RepoRoot = $PSScriptRoot }
$RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
$BuildScope = 'UnsignedComparison'
if ($Output) {
    $Output = [IO.Path]::GetFullPath($Output)
    if (Test-Path -LiteralPath $Output) { throw 'Output must name a new directory.' }
}
. (Join-Path $RepoRoot 'build-support/build/native-process.ps1')
$pythonExe = (Get-Command python -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
Write-Host 'source build: native proof plus OTA/Full packaging; no signing or device access.'

$uv4 = Join-Path $KeilRoot "UV4\UV4.exe"
if (-not (Test-Path -LiteralPath $uv4)) {
    throw "Keil UV4.exe not found: $uv4"
}

$persistentSdk = Join-Path $SdkRoot 'dialog/DA145xx_SDK/6.0.24.1464'
$dialogSdk = Join-Path $RepoRoot '.local/da-sdk6-crc-only'
$dialogMarker = Join-Path $persistentSdk "sdk\platform\core_modules\rwip\api\rwip.h"
if (-not (Test-Path -LiteralPath $dialogMarker -PathType Leaf)) {
    throw 'Acquire the licensed SDK6 externally and prepare its verified DA SDK inventory first.'
}

$projectRoot = Join-Path $dialogSdk "projects\target_apps\ble_examples\ble_app_inktag"
$uvprojx = Join-Path $projectRoot "Keil_5\inktag_da14585.uvprojx"
$buildLog = Join-Path $projectRoot "Keil_5\uv4_build.log"
$recoveryRoot = Join-Path $dialogSdk "projects\target_apps\ble_examples\ble_app_inktag_recovery"
$recoveryProject = Join-Path $recoveryRoot "Keil_5\ble_app_ota.uvprojx"
$recoveryBuildLog = Join-Path $recoveryRoot "Keil_5\uv4_build.log"
$bootloaderRoot = Join-Path $dialogSdk "utilities\secondary_bootloader"
$bootloaderProject = Join-Path $bootloaderRoot "secondary_bootloader.uvprojx"
$bootloaderBuildLog = Join-Path $bootloaderRoot "uv4_inktag_da14585_build.log"

function Assert-DaSecondaryHandoffSafe([string]$MapPath) {
    $mapText = Get-Content -LiteralPath $MapPath -Raw
    $overwriteEnd = 0x07FC0A00L
    $requiredSymbols = @(
        "inktag_da_restart_via_secondary_bootloader",
        "secondary_bootloader_jump",
        "spi_flash_read_data",
        "spi_flash_is_busy",
        "spi_set_bitmode",
        "spi_access",
        "spi_transaction"
    )
    foreach ($symbol in $requiredSymbols) {
        $pattern = "(?m)^\s+" + [Regex]::Escape($symbol) +
            "(?:\.[0-9]+)?\s+0x([0-9a-f]+)\s+Thumb Code"
        $match = [Regex]::Match($mapText, $pattern)
        if (-not $match.Success) {
            throw "DA14585 secondary-loader handoff symbol $symbol is missing from $MapPath"
        }
        $address = [Convert]::ToInt64($match.Groups[1].Value, 16)
        if ($address -lt $overwriteEnd) {
            throw ("DA14585 secondary-loader handoff symbol {0} at 0x{1:X8} overlaps the " +
                   "0x07FC0000-0x07FC09FF loader destination. See {2}" -f
                   $symbol, $address, $MapPath)
        }
    }
}

$materializerArgs = @("--board", $Board)
$workspaceText = & python (Join-Path $RepoRoot 'build-support/sdk/da_sdk_workspace.py') materialize --repo $RepoRoot --sdk-root $SdkRoot --build-profile crc-only -- @materializerArgs
if ($LASTEXITCODE -ne 0) {
    throw "DA14585 project materialization failed with exit code $LASTEXITCODE"
}
$workspace = $workspaceText | ConvertFrom-Json
& python -B (Join-Path $RepoRoot 'build-support/build/ota_policy.py') --project $recoveryProject --expected integrity-only
if ($LASTEXITCODE -ne 0) { throw 'Recovery compiler policy differs; no build attempted.' }
if ([IO.Path]::GetFullPath($workspace.sdk) -ne [IO.Path]::GetFullPath($dialogSdk)) { throw 'Unexpected mutable SDK workspace; no build attempted.' }

$buildId = 'da-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$candidateJob = if ($Output) { $Output } else { Join-Path $RepoRoot ('.local/builds/crc-only/' + $buildId) }
$candidateTool = Join-Path $RepoRoot 'build-support/build/build_inputs.py'
$armclang = Join-Path $KeilRoot 'ARM/ARMCLANG/bin/armclang.exe'
& python -B $candidateTool capture --repo $RepoRoot --sdk $dialogSdk --scope $BuildScope --job $candidateJob --compiler $uv4 --compiler $armclang
if ($LASTEXITCODE -ne 0) { throw 'Build input capture/hook audit failed; no native build or signing attempted.' }
$buildLog = Join-Path $candidateJob 'application-build.log'
$recoveryBuildLog = Join-Path $candidateJob 'recovery-build.log'
$bootloaderBuildLog = Join-Path $candidateJob 'bootloader-build.log'
$candidateLogs = Join-Path $candidateJob 'logs'
$null = New-Item -ItemType Directory -Path $candidateLogs

function Invoke-DaBuildProcess([string]$Project, [string]$NativeLog, [string]$Role) {
    # uVision parses its GUI command line itself. Quoted "-b"/"-t" switches
    # can open a blank IDE without building; quote only argument data needing
    # it. Keep this vendor syntax out of the maintained Python/tool launcher.
    $arguments = @($uv4Action,$Project,'-t','DA14585','-j0','-o',$NativeLog)
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $uv4
    $start.Arguments = (($arguments | ForEach-Object {
        if ($_ -match '[\s"]') { ConvertTo-InkTagDaNativeArgument $_ } else { $_ }
    }) -join ' ')
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $log = Join-Path $candidateLogs ($Role + '-launcher.log')
    if ((Test-Path -LiteralPath $log) -or (Test-Path -LiteralPath ($log+'.result.json'))) {
        throw 'Native launcher evidence exists; no overwrite or replay.'
    }
    [IO.File]::WriteAllText($log,$start.FileName+' '+$start.Arguments)
    $process = [Diagnostics.Process]::new(); $process.StartInfo = $start
    $nativeExit = $null; $timedOut = $false; $started = $false
    try {
        $started = $process.Start()
        if (!$started) { throw 'Native compiler process did not start.' }
        $timedOut = !$process.WaitForExit([int]($BuildTimeoutSeconds*1000))
        if ($timedOut) {
            $process.Kill()
            if (!$process.WaitForExit(5000)) { throw 'Timed-out native compiler did not exit; no replay.' }
        }
        $nativeExit = [int]$process.ExitCode
    } finally {
        [IO.File]::WriteAllText(($log+'.result.json'),(@{nativeExitCode=$nativeExit;timedOut=$timedOut;
            childStarted=$started;operationTimeoutSeconds=$BuildTimeoutSeconds}|ConvertTo-Json))
        $process.Dispose()
    }
    if ($timedOut -or $null -eq $nativeExit -or $nativeExit -ne 0) {
        throw "$Role native build failed or timed out; no signing, retry or candidate selection. See $NativeLog"
    }
    return $nativeExit
}

function Invoke-DaPackage([string]$Name, [string[]]$PythonArguments) {
    $result = Invoke-InkTagDaLoggedProcess -Exe $pythonExe -Arguments $PythonArguments `
        -Log (Join-Path $candidateLogs ($Name + '.log')) `
        -OperationTimeoutSeconds $BuildTimeoutSeconds -Label $Name
    if ($result.TimedOut -or $result.ExitCode -ne 0 -or $result.NativeExitCode -ne 0) {
        throw "$Name failed or timed out; immutable logs preserved; no retry or candidate selection."
    }
    return $result.Text
}

function Save-DaBuildEvidence([string]$Source, [string]$Relative) {
    $destination = Join-Path $candidateJob $Relative
    if (Test-Path -LiteralPath $destination) { throw 'Immutable build evidence already exists.' }
    $null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination)
    $hash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash.ToLowerInvariant()
    Copy-Item -LiteralPath $Source -Destination $destination -ErrorAction Stop
    if ((Get-FileHash -LiteralPath $Source).Hash.ToLowerInvariant() -ne $hash -or
        (Get-FileHash -LiteralPath $destination).Hash.ToLowerInvariant() -ne $hash) {
        throw 'Native evidence changed during capture; no signing.'
    }
    return @{path=$Relative;bytes=(Get-Item -LiteralPath $destination).Length;sha256=$hash}
}

function Get-DaBuildReference([string]$Path, [string]$Relative) {
    return @{path=$Relative;bytes=(Get-Item -LiteralPath $Path).Length;
        sha256=(Get-FileHash -LiteralPath $Path).Hash.ToLowerInvariant()}
}

if (Test-Path -LiteralPath $buildLog) {
    Remove-Item -LiteralPath $buildLog -Force
}
$uv4Action = "-b"
$uv4ExitCode = Invoke-DaBuildProcess $uvprojx $buildLog 'application'
$deadline = [DateTime]::UtcNow.AddSeconds($BuildTimeoutSeconds)
$buildLogText = ""
$buildFinished = $false
do {
    if (Test-Path -LiteralPath $buildLog) {
        $buildLogText = Get-Content -LiteralPath $buildLog -Raw
        $buildFinished =
            $buildLogText -match '(?im)^Build Time Elapsed:' -or
            $buildLogText -match '(?im)Target not created|Build aborted'
    }
    if (-not $buildFinished) { Start-Sleep -Milliseconds 200 }
} while (-not $buildFinished -and [DateTime]::UtcNow -lt $deadline)

if (-not $buildFinished) {
    throw "Keil build did not finish within $BuildTimeoutSeconds seconds. See $buildLog"
}
$keilReportedFailure =
    $buildLogText -match '- [1-9][0-9]* Error\(s\)' -or
    $buildLogText -match '(?im)fatal error' -or
    $buildLogText -match '(?im)Target not created|Build aborted'
if ($uv4ExitCode -ne 0 -or $keilReportedFailure) {
    $failureReason = if ($uv4ExitCode -ne 0) { "exit code $uv4ExitCode" } else { "the build log reports an error" }
    throw "Keil build failed: $failureReason. See $buildLog"
}

$objects = Join-Path $projectRoot "Keil_5\out_DA14585\Objects"
$applicationMap = Join-Path $projectRoot "Keil_5\out_DA14585\Listings\ble_app_inktag_585.map"
if (-not (Test-Path -LiteralPath $applicationMap -PathType Leaf)) {
    throw "DA14585 linker map is missing: $applicationMap"
}
$applicationMapText = Get-Content -LiteralPath $applicationMap -Raw
Assert-DaSecondaryHandoffSafe $applicationMap
if ($applicationMapText -notmatch 'Load Region LR_FREE_AREA \(Base: 0x[0-9a-f]+, Size: 0x([0-9a-f]+)') {
    throw "DA14585 high scratch load size could not be read from $applicationMap"
}
$highScratchLoadBytes = [Convert]::ToInt32($Matches[1], 16)
if ($highScratchLoadBytes -ne 0) {
    throw "DA14585 high scratch contributes $highScratchLoadBytes initialized bytes; it must remain pure ZI so it does not extend the OTA payload. See $applicationMap"
}
if ($applicationMapText -notmatch 'Execution Region ER_RESET_TRACE \(Exec base: 0x([0-9a-f]+), Load base: 0x[0-9a-f]+, Size: 0x00000020, Max: 0x00000020') {
    throw "DA14585 reset trace must occupy its exact 32-byte UNINIT high-RAM region. See $applicationMap"
}
$resetTraceBase = [Convert]::ToInt64($Matches[1], 16)
if ($resetTraceBase -lt 0x07FD7000 -or ($resetTraceBase + 32) -gt 0x07FD7600) {
    throw "DA14585 reset trace overlaps SDK low RAM or the secondary loader body. See $applicationMap"
}
if ($applicationMapText -notmatch '(?m)^\s+0x[0-9a-f]+\s+-\s+0x[0-9a-f]+\s+Zero\s+RW\s+\d+\s+\.bss\.inktag_free_area\s+') {
    throw "DA14585 application scratch is not linked as zero-initialized .bss.inktag_free_area. See $applicationMap"
}
$minimumHighScratchHeadroomBytes = 32
if ($applicationMapText -notmatch 'Execution Region ER_FREE_AREA \(Exec base: 0x[0-9a-f]+, Load base: 0x[0-9a-f]+, Size: 0x([0-9a-f]+), Max: 0x([0-9a-f]+)') {
    throw "DA14585 high scratch size/capacity could not be read from $applicationMap"
}
$highScratchBytes = [Convert]::ToInt32($Matches[1], 16)
$highScratchCapacityBytes = [Convert]::ToInt32($Matches[2], 16)
$highScratchHeadroomBytes = $highScratchCapacityBytes - $highScratchBytes
if ($highScratchHeadroomBytes -lt $minimumHighScratchHeadroomBytes) {
    throw "DA14585 high scratch headroom is only $highScratchHeadroomBytes bytes ($highScratchBytes / $highScratchCapacityBytes used); at least $minimumHighScratchHeadroomBytes bytes are required. See $applicationMap"
}
Write-Host "DA14585 high scratch: pure ZI; $highScratchBytes / $highScratchCapacityBytes bytes; $highScratchHeadroomBytes bytes placement headroom"

# Do not consume the last sliver of executable SRAM while adopting source
# paths. This reserve is separate from the existing static stack/ZI gates.
$minimumPlacementHeadroomBytes = 512

if ($applicationMapText -notmatch 'Execution Region ARM_LIB_STACK \(Exec base: 0x([0-9a-f]+), Load base: 0x[0-9a-f]+, Size: 0x([0-9a-f]+)') {
    throw "DA14585 application stack placement could not be read from $applicationMap"
}
$applicationStackBase = [Convert]::ToInt64($Matches[1], 16)
$applicationStackMapBytes = [Convert]::ToInt32($Matches[2], 16)
if ($applicationMapText -notmatch 'Execution Region RET_DATA \(Exec base: 0x([0-9a-f]+)') {
    throw "DA14585 retained-data boundary could not be read from $applicationMap"
}
$retainedDataBase = [Convert]::ToInt64($Matches[1], 16)
$placementHeadroomBytes = $retainedDataBase -
    ($applicationStackBase + $applicationStackMapBytes)
if ($placementHeadroomBytes -lt $minimumPlacementHeadroomBytes) {
    throw "DA14585 code/RW/ZI/stack placement headroom is only $placementHeadroomBytes bytes; at least $minimumPlacementHeadroomBytes bytes are required before retained RAM. See $applicationMap"
}
Write-Host "DA14585 SRAM placement: stack ends at 0x$('{0:X8}' -f ($applicationStackBase + $applicationStackMapBytes)); retained RAM starts at 0x$('{0:X8}' -f $retainedDataBase); $placementHeadroomBytes bytes placement headroom"

$applicationStackBytes = 0x860
$minimumStackHeadroomBytes = 384
$stackReport = Join-Path $objects "ble_app_inktag_585.htm"
if (-not (Test-Path -LiteralPath $stackReport -PathType Leaf)) {
    throw "DA14585 linker stack report is missing: $stackReport"
}
$stackReportText = Get-Content -LiteralPath $stackReport -Raw
if ($stackReportText -notmatch 'Maximum Stack Usage\s*=\s*([0-9]+) bytes') {
    throw "DA14585 maximum stack usage could not be read from $stackReport"
}
$reportedStackBytes = [int]$Matches[1]
$applicationStackMapBytes = [int]$applicationStackMapBytes
if ($applicationStackMapBytes -ne $applicationStackBytes) {
    throw "DA14585 linked stack is $applicationStackMapBytes bytes; expected the enforced $applicationStackBytes-byte region. See $applicationMap"
}
$stackHeadroomBytes = $applicationStackBytes - $reportedStackBytes
if ($stackHeadroomBytes -lt $minimumStackHeadroomBytes) {
    throw "DA14585 static stack headroom is only $stackHeadroomBytes bytes ($reportedStackBytes / $applicationStackBytes used); at least $minimumStackHeadroomBytes bytes are required for indirect calls and interrupts. See $stackReport"
}
Write-Host "DA14585 stack: $reportedStackBytes / $applicationStackBytes bytes; $stackHeadroomBytes bytes static headroom"

$artifacts = @((Get-Item -LiteralPath (Join-Path $objects 'ble_app_inktag_585.bin')))
if (Test-Path -LiteralPath (Join-Path $objects 'ble_app_inktag_585.hex')) {
    $artifacts += Get-Item -LiteralPath (Join-Path $objects 'ble_app_inktag_585.hex')
}
$buildOut = $candidateJob
$artifactBase = "InkTag-$Board"
$assetImage = $null
foreach ($artifact in $artifacts) {
    $destination = Join-Path $buildOut ($artifactBase + $artifact.Extension.ToLowerInvariant())
    Copy-Item -LiteralPath $artifact.FullName -Destination $destination -Force
}
if ($Board -eq "da14585_aesl") {
    $armclang = Join-Path $KeilRoot "ARM\ARMCLANG\bin\armclang.exe"
    if (-not (Test-Path -LiteralPath $armclang -PathType Leaf)) {
        throw "ArmClang compiler not found: $armclang"
    }
    $assetBuild = Join-Path $RepoRoot ".cache\build\dialog\$Board\external-assets"
    New-Item -ItemType Directory -Force -Path $assetBuild | Out-Null
    $fontObject = Join-Path $assetBuild "fonts.o"
    $qrObject = Join-Path $assetBuild "device-info-qr.o"
    $lunarObject = Join-Path $assetBuild "lunar.o"
    $guiSource = Join-Path $RepoRoot "compat\common\gui"
    $assetCompileArgs = @(
        "--target=arm-arm-none-eabi", "-mcpu=cortex-m0", "-mthumb",
        "-std=c99", "-Oz", "-fdata-sections", "-ffunction-sections",
        "-I$guiSource", "-c"
    )
    foreach ($assetCompile in @(
        @{ Source = (Join-Path $guiSource "inktag_gui_fonts.c"); Output = $fontObject },
        @{ Source = (Join-Path $guiSource "inktag_gui_device_qr_asset.c"); Output = $qrObject },
        @{ Source = (Join-Path $guiSource "inktag_calendar.c"); Output = $lunarObject }
    )) {
        $assetResult = Invoke-InkTagDaLoggedProcess -Exe $armclang `
            -Arguments ($assetCompileArgs + @($assetCompile.Source,'-o',$assetCompile.Output)) `
            -Log (Join-Path $candidateLogs ((Split-Path $assetCompile.Output -Leaf)+'.compile.log')) `
            -OperationTimeoutSeconds $BuildTimeoutSeconds -Label 'GUI object build'
        if ($assetResult.TimedOut -or $assetResult.ExitCode -ne 0 -or $assetResult.NativeExitCode -ne 0) {
            throw "DA14585 external GUI asset compilation failed for $($assetCompile.Source)"
        }
    }
    $assetImage = Join-Path $buildOut "$artifactBase-font-assets.bin"
    $null = Invoke-DaPackage 'gui-package' @((Join-Path $RepoRoot 'build-support/sdk/package-gui-font-assets.py'),
        '--font-object',$fontObject,'--qr-object',$qrObject,'--lunar-object',$lunarObject,'--output',$assetImage)

    $rawApp = Join-Path $buildOut "$artifactBase.bin"
    $appRawBytes = (Get-Item -LiteralPath $rawApp).Length
    $appSigningBudgetBytes = 96
    $appNativePayloadLimit = 0x17600
    if ($appRawBytes + $appSigningBudgetBytes -gt $appNativePayloadLimit) {
        throw 'App raw plus the unchanged official-signature budget exceeds the native boot-copy limit.'
    }
    Write-Host "DA14585 App raw: $appRawBytes bytes; native payload limit $appNativePayloadLimit; 96-byte native trailer budget retained."

    $otaImage = Join-Path $buildOut "$artifactBase-ota.img"

    if (Test-Path -LiteralPath $recoveryBuildLog) {
        Remove-Item -LiteralPath $recoveryBuildLog -Force
    }
    $recoveryExitCode = Invoke-DaBuildProcess $recoveryProject $recoveryBuildLog 'recovery'
    $deadline = [DateTime]::UtcNow.AddSeconds($BuildTimeoutSeconds)
    $recoveryLogText = ""
    $recoveryFinished = $false
    do {
        if (Test-Path -LiteralPath $recoveryBuildLog) {
            $recoveryLogText = Get-Content -LiteralPath $recoveryBuildLog -Raw
            $recoveryFinished =
                $recoveryLogText -match '(?im)^Build Time Elapsed:' -or
                $recoveryLogText -match '(?im)Target not created|Build aborted'
        }
        if (-not $recoveryFinished) { Start-Sleep -Milliseconds 200 }
    } while (-not $recoveryFinished -and [DateTime]::UtcNow -lt $deadline)
    if (-not $recoveryFinished -or $recoveryExitCode -ne 0 -or
        $recoveryLogText -match '- [1-9][0-9]* Error\(s\)|(?im)fatal error|Target not created|Build aborted') {
        throw "DA14585 fixed Recovery build failed. See $recoveryBuildLog"
    }
    $recoveryMap = Join-Path $recoveryRoot "Keil_5\out_DA14585\Listings\InkTag-DA14585-Recovery.map"
    Assert-DaSecondaryHandoffSafe $recoveryMap
    $recoveryRawSource = Join-Path $recoveryRoot "Keil_5\out_DA14585\Objects\InkTag-DA14585-Recovery.bin"
    $recoveryRaw = Join-Path $buildOut "$artifactBase-recovery.bin"
    $recoveryImage = Join-Path $buildOut "$artifactBase-recovery.img"
    Copy-Item -LiteralPath $recoveryRawSource -Destination $recoveryRaw -Force
    # Check the unchanged internal-header budget before packaging.
    $recoveryImageBytes = (Get-Item -LiteralPath $recoveryRaw).Length + 64
    if ($recoveryImageBytes -gt 0x8000) { throw 'Recovery raw plus internal header exceeds its region.' }
    $recoveryRegionBytes = 0x8000
    $recoveryHeadroomBytes = $recoveryRegionBytes - $recoveryImageBytes
    $recoveryStackBytes = 0x600
    $recoveryMinimumStackHeadroomBytes = 256
    $recoveryStackReport = Join-Path $recoveryRoot "Keil_5\out_DA14585\Objects\InkTag-DA14585-Recovery.htm"
    $recoveryStackReportText = Get-Content -LiteralPath $recoveryStackReport -Raw
    if ($recoveryStackReportText -notmatch 'Maximum Stack Usage\s*=\s*([0-9]+) bytes') {
        throw "DA14585 Recovery maximum stack usage could not be read from $recoveryStackReport"
    }
    $recoveryReportedStackBytes = [int]$Matches[1]
    $recoveryStackHeadroomBytes = $recoveryStackBytes - $recoveryReportedStackBytes
    if ($recoveryStackHeadroomBytes -lt $recoveryMinimumStackHeadroomBytes) {
        throw "DA14585 Recovery static stack headroom is only $recoveryStackHeadroomBytes bytes ($recoveryReportedStackBytes / $recoveryStackBytes used); at least $recoveryMinimumStackHeadroomBytes bytes are required for BLE indirect calls and interrupts. See $recoveryStackReport"
    }
    Write-Host "DA14585 fixed Recovery: $recoveryImageBytes / $recoveryRegionBytes bytes; $recoveryHeadroomBytes bytes Flash headroom; stack $recoveryReportedStackBytes / $recoveryStackBytes bytes ($recoveryStackHeadroomBytes bytes static headroom)"

    if (Test-Path -LiteralPath $bootloaderBuildLog) {
        Remove-Item -LiteralPath $bootloaderBuildLog -Force
    }
    $bootloaderExitCode = Invoke-DaBuildProcess $bootloaderProject $bootloaderBuildLog 'bootloader'
    $deadline = [DateTime]::UtcNow.AddSeconds($BuildTimeoutSeconds)
    $bootloaderLogText = ""
    $bootloaderFinished = $false
    do {
        if (Test-Path -LiteralPath $bootloaderBuildLog) {
            $bootloaderLogText = Get-Content -LiteralPath $bootloaderBuildLog -Raw
            $bootloaderFinished =
                $bootloaderLogText -match '(?im)^Build Time Elapsed:' -or
                $bootloaderLogText -match '(?im)Target not created|Build aborted'
        }
        if (-not $bootloaderFinished) { Start-Sleep -Milliseconds 200 }
    } while (-not $bootloaderFinished -and [DateTime]::UtcNow -lt $deadline)
    if (-not $bootloaderFinished -or $bootloaderExitCode -ne 0 -or
        $bootloaderLogText -match '- [1-9][0-9]* Error\(s\)|(?im)fatal error|Target not created|Build aborted') {
        throw "DA14585 InkTag secondary-bootloader build failed. See $bootloaderBuildLog"
    }
    $bootloaderMap = Join-Path $bootloaderRoot "out_DA14585\Listings\secondary_bootloader_585.map"
    $bootloaderMapText = Get-Content -LiteralPath $bootloaderMap -Raw
    if ($bootloaderMapText -notmatch 'Execution Region ER_2 \(Exec base: 0x07fd7600.*?Size: 0x([0-9a-f]+)') {
        throw "DA14585 secondary bootloader is not linked at its boot-safe high-RAM address. See $bootloaderMap"
    }
    $bootloaderEr2Bytes = [Convert]::ToInt32($Matches[1], 16)
    if ($bootloaderEr2Bytes -gt 0x800) {
        throw "DA14585 secondary bootloader ER_2 uses $bootloaderEr2Bytes bytes; at most 0x800 bytes are reserved"
    }
    $bootloaderStackBytes = 0x200
    $bootloaderMinimumStackHeadroomBytes = 128
    $bootloaderStackReport = Join-Path $bootloaderRoot "out_DA14585\Objects\secondary_bootloader_585.htm"
    $bootloaderStackReportText = Get-Content -LiteralPath $bootloaderStackReport -Raw
    if ($bootloaderStackReportText -notmatch 'Maximum Stack Usage\s*=\s*([0-9]+) bytes') {
        throw "DA14585 secondary bootloader maximum stack usage could not be read from $bootloaderStackReport"
    }
    $bootloaderReportedStackBytes = [int]$Matches[1]
    $bootloaderStackHeadroomBytes = $bootloaderStackBytes - $bootloaderReportedStackBytes
    if ($bootloaderStackHeadroomBytes -lt $bootloaderMinimumStackHeadroomBytes) {
        throw "DA14585 secondary bootloader static stack headroom is only $bootloaderStackHeadroomBytes bytes ($bootloaderReportedStackBytes / $bootloaderStackBytes used); at least $bootloaderMinimumStackHeadroomBytes bytes are required. See $bootloaderStackReport"
    }
    $bootloaderRawSource = Join-Path $bootloaderRoot "out_DA14585\Objects\secondary_bootloader_585.bin"
    $bootloaderRaw = Join-Path $buildOut "$artifactBase-secondary-bootloader.bin"
    Copy-Item -LiteralPath $bootloaderRawSource -Destination $bootloaderRaw -Force
    # The DA14585 target used to omit __DA14585__, silently compiling out the
    # Recovery-request selector even though the patched C source looked right.
    # Keep a byte-level artifact gate for the little-endian 0x00035008 marker
    # literal so an unselectable fixed Recovery can never be packaged again.
    $bootloaderBytes = [IO.File]::ReadAllBytes($bootloaderRaw)
    if ($bootloaderBytes.Length -gt 0xA00) {
        throw "DA14585 secondary bootloader exceeds the 0xA00-byte deterministic handoff window"
    }
    if ($bootloaderBytes.Length -lt 0x180 -or
        [BitConverter]::ToUInt32($bootloaderBytes, 0) -ne 0x07FD8000 -or
        [BitConverter]::ToUInt32($bootloaderBytes, 4) -ne 0x07FC012D) {
        throw "DA14585 secondary bootloader has an invalid handoff vector table"
    }
    $requestMarkerLiteral = [byte[]](0x08, 0x50, 0x03, 0x00)
    $requestSelectorPresent = $false
    for ($offset = 0; $offset -le $bootloaderBytes.Length - $requestMarkerLiteral.Length; $offset++) {
        if ($bootloaderBytes[$offset] -eq $requestMarkerLiteral[0] -and
            $bootloaderBytes[$offset + 1] -eq $requestMarkerLiteral[1] -and
            $bootloaderBytes[$offset + 2] -eq $requestMarkerLiteral[2] -and
            $bootloaderBytes[$offset + 3] -eq $requestMarkerLiteral[3]) {
            $requestSelectorPresent = $true
            break
        }
    }
    if (-not $requestSelectorPresent) {
        throw "DA14585 secondary bootloader omitted the 0x00035008 fixed-Recovery request selector"
    }
    Write-Host "DA14585 secondary bootloader: ER_2 $bootloaderEr2Bytes / 2048 bytes; stack $bootloaderReportedStackBytes / $bootloaderStackBytes bytes ($bootloaderStackHeadroomBytes bytes static headroom)"

    $nativeEvidence = @{}
    foreach ($role in @(
        @{name='application';raw=$rawApp;objects=$objects;stem='ble_app_inktag_585';map=$applicationMap;project=$uvprojx},
        @{name='recovery';raw=$recoveryRaw;objects=(Split-Path $recoveryRawSource -Parent);stem='InkTag-DA14585-Recovery';map=$recoveryMap;project=$recoveryProject},
        @{name='bootloader';raw=$bootloaderRaw;objects=(Split-Path $bootloaderRawSource -Parent);stem='secondary_bootloader_585';map=$bootloaderMap;project=$bootloaderProject}
    )) {
        $directory = 'evidence/' + $role.name + '/'
        $nativeEvidence[$role.name] = @{
            raw=(Get-DaBuildReference $role.raw (Split-Path $role.raw -Leaf));
            elf=(Save-DaBuildEvidence (Join-Path $role.objects ($role.stem+'.axf')) ($directory+'firmware.axf'));
            map=(Save-DaBuildEvidence $role.map ($directory+'firmware.map'));
            stack=(Save-DaBuildEvidence (Join-Path $role.objects ($role.stem+'.htm')) ($directory+'stack.htm'));
            linkerCommand=(Save-DaBuildEvidence (Join-Path $role.objects ($role.stem+'.lnp')) ($directory+'link.lnp'));
            project=(Save-DaBuildEvidence $role.project ($directory+'project.uvprojx'))}
    }
    $nativeEvidence.gui = @{
        raw=(Get-DaBuildReference $assetImage (Split-Path $assetImage -Leaf));
        fonts=(Save-DaBuildEvidence $fontObject 'evidence/gui/fonts.o');
        qr=(Save-DaBuildEvidence $qrObject 'evidence/gui/qr.o');
        lunar=(Save-DaBuildEvidence $lunarObject 'evidence/gui/lunar.o')}

    # No private-key operation precedes successful App/Recovery/loader gates.
    & python -B $candidateTool check --repo $RepoRoot --job $candidateJob
    if ($LASTEXITCODE -ne 0) { throw 'Native inputs changed; no signing or candidate collection permitted.' }
    $nativeProof = [ordered]@{status='passed';scope=$BuildScope;otaSignaturePolicy="integrity-only";recoverySoftwareVersion=(Get-Content -LiteralPath (Join-Path $RepoRoot 'version.json') -Raw | ConvertFrom-Json).recoveryVersion;
        nativeExits=@{application=$uv4ExitCode;recovery=$recoveryExitCode;bootloader=$bootloaderExitCode};
        rawSha256=@{application=(Get-FileHash -LiteralPath $rawApp).Hash.ToLowerInvariant();
            recovery=(Get-FileHash -LiteralPath $recoveryRaw).Hash.ToLowerInvariant();
            bootloader=(Get-FileHash -LiteralPath $bootloaderRaw).Hash.ToLowerInvariant();
            assets=(Get-FileHash -LiteralPath $assetImage).Hash.ToLowerInvariant()};
        buildLogs=@{application=(Get-DaBuildReference $buildLog 'application-build.log');
            recovery=(Get-DaBuildReference $recoveryBuildLog 'recovery-build.log');
            bootloader=(Get-DaBuildReference $bootloaderBuildLog 'bootloader-build.log')};
        evidence=$nativeEvidence;
        appRawBytes=$appRawBytes;appNativePayloadLimit=$appNativePayloadLimit;appTrailerBudgetBytes=96;
        appStackHeadroom=$stackHeadroomBytes;recoveryStackHeadroom=$recoveryStackHeadroomBytes;
        bootloaderStackHeadroom=$bootloaderStackHeadroomBytes;
        configurationPresets=@('asel29','asel42','asel42-ssd1683');
        toolchain=@{uv4=(Get-FileHash -LiteralPath $uv4).Hash;armclang=(Get-FileHash -LiteralPath $armclang).Hash}}
    $proofPath=Join-Path $candidateJob 'native-proof.json'
    if(Test-Path -LiteralPath $proofPath){throw 'Immutable native proof already exists.'}
    [IO.File]::WriteAllText($proofPath,($nativeProof|ConvertTo-Json -Depth 10),(New-Object Text.UTF8Encoding($false)))
    Write-Host ('[NATIVE_PROOF] ' + $proofPath)
    $packageArgs = @((Join-Path $RepoRoot 'build-support/build/package_full.py'),
        '--proof',$proofPath,'--version-json',(Join-Path $RepoRoot 'version.json'))
    $null = Invoke-DaPackage 'full-package' $packageArgs
    $packageOut = Join-Path $candidateJob 'packages'
    foreach ($file in @("$artifactBase-ota.img","$artifactBase-recovery.img",
                        "$artifactBase-full.bin",'package-manifest.json')) {
        Write-Host ('[PACKAGE] ' + (Join-Path $packageOut $file))
    }
}
$buildKind = "incremental build"
Write-Host "DA14585 build OK ($Board, $buildKind)" -ForegroundColor Green
foreach ($artifact in $artifacts) {
    $destination = Join-Path $buildOut ($artifactBase + $artifact.Extension.ToLowerInvariant())
    Write-Host ("[ARTIFACT] {0} ({1} bytes)" -f $destination, (Get-Item -LiteralPath $destination).Length)
}
if ($Board -eq "da14585_aesl") {
    Write-Host ("[ARTIFACT] {0} ({1} bytes)" -f $assetImage, (Get-Item -LiteralPath $assetImage).Length)
    Write-Host ("[ARTIFACT] {0} ({1} bytes)" -f $recoveryRaw, (Get-Item -LiteralPath $recoveryRaw).Length)
    Write-Host ("[ARTIFACT] {0} ({1} bytes)" -f $bootloaderRaw, (Get-Item -LiteralPath $bootloaderRaw).Length)
}
