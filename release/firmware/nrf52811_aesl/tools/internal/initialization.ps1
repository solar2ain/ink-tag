[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('Generate','Flash','Preserve')][string]$Action,
    [string]$Firmware='', [string]$Config='', [Alias('Input')][string]$Materials='',
    [Alias('OutputDir')][string]$Output='', [string]$JLinkExe='', [Alias('Snr')][string]$JLinkSerial='',
    [ValidateRange(1000,50000000)][int]$FrequencyHz=500000,
    [ValidateRange(1,86400)][int]$OperationTimeoutSeconds=180,
    [ValidateSet('en','zh')][string]$Language='en', [switch]$Interactive,
    [switch]$Yes, [switch]$DryRun, [switch]$RetryDetect=$true, [switch]$NoWait, [Alias('h')][switch]$Help,
    [Parameter(ValueFromRemainingArguments=$true)][object[]]$RemainingArguments
)
$ToolRoot=Split-Path -Parent $PSScriptRoot
$ErrorActionPreference='Stop'
try {
    $Interactive=$Interactive -and -not $Yes -and -not $DryRun
    [Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
    $script:InitMessages=(Get-Content -LiteralPath (Join-Path $ToolRoot 'internal/initialization-messages.json') -Raw -Encoding UTF8 | ConvertFrom-Json).$Language
    if($Interactive -and -not $DryRun -and -not $Help){foreach($line in $script:InitMessages.tutorial){Write-Host $line}}
    if($Help) {
        Write-Output 'Recommended: flash.bat -Firmware FULL.zip -Config BOARD.toml [-JLinkExe PATH] [-Snr SERIAL] -Yes'
        Write-Output 'generate-hex.bat -Firmware FULL.zip -Config BOARD.toml -Output NEW_DIRECTORY'
        Write-Output 'flash-hex.bat -Materials INITIALIZATION_DIRECTORY [-JLinkExe PATH] [-Snr SERIAL] -Yes'
        Write-Output 'Windows PowerShell5.1/.NET and J-Link7.88j only. No Python/pip/CLI/BLE.'
        Write-Output 'Missing paths are prompted individually. -Yes requires complete input paths.'
        Write-Output 'Single J-Link is selected automatically; multiple probes require -Snr as reported by the backend.'
        Write-Output 'Initial detection waits for the selected programmer and board. Esc cancels. Pass -NoWait for one detection attempt.'
        Write-Output 'Use -DryRun to validate without opening a probe. Full resets ordinary user data.'
        exit 0
    }
    if($RemainingArguments){throw 'Unknown/extra argument; use -Help'}
    # Native entries coexist with older supported tools in the same directory.
    # Their local inventory never replaces or interprets legacy parent metadata.
    $manifestPath=Join-Path $ToolRoot 'native-tools.json'
    if(Test-Path -LiteralPath $manifestPath) {
        $bundle=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8|ConvertFrom-Json
        if($bundle.chip -cne 'nrf52811' -or $bundle.buildTarget -cne 'nrf52811_aesl' -or $bundle.artifactKind -cne 'tools-only' -or $bundle.runtimeKind -cne 'windows-powershell-dotnet' -or $bundle.firmwareIncluded -ne $false){throw 'Use the complete native Windows tools directory'}
        foreach($required in @('internal/initialization.ps1','internal/initialization-bytes.cs','internal/initialization-format.ps1','internal/initialization-session.ps1','internal/initialization-jlink.ps1','internal/initialization-dependencies.json','internal/initialization-screen-rules.json','assets/full-writer.bin','configs/EL022HWRL.toml','configs/EL022HWRL-A0.toml')) {
            if($bundle.files.PSObject.Properties.Name -cnotcontains $required){throw "Incomplete native tools manifest: $required"}
        }
        foreach($entry in $bundle.files.PSObject.Properties) {
            if($entry.Name -cnotmatch '^([A-Za-z0-9_.-]+/)*[A-Za-z0-9_.-]+$' -or $entry.Name.Split('/') -contains '..'){throw 'Unsafe native tools manifest path'}
            $path=Join-Path $ToolRoot $entry.Name;$file=Get-Item -LiteralPath $path -ErrorAction Stop
            if($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $file.Length -ne $entry.Value.bytes){throw "Native tools file missing/changed: $($entry.Name)"}
            $hasher=[Security.Cryptography.SHA256]::Create()
            try{$digest=[BitConverter]::ToString($hasher.ComputeHash([IO.File]::ReadAllBytes($path))).Replace('-','').ToLowerInvariant()}finally{$hasher.Dispose()}
            if($digest -cne $entry.Value.sha256){throw "Native tools file missing/changed: $($entry.Name)"}
        }
    }
    . (Join-Path $ToolRoot 'internal/initialization-format.ps1')
    if($Interactive -and -not $DryRun -and $Action -eq 'Preserve' -and -not $Firmware) {
        $choices=@()
        foreach($directory in @($ToolRoot,(Join-Path (Split-Path -Parent $ToolRoot) 'firmware'),(Join-Path (Split-Path -Parent $ToolRoot) 'full'))) {
            if(-not (Test-Path -LiteralPath $directory -PathType Container)){continue}
            foreach($file in Get-ChildItem -LiteralPath $directory -Filter '*.zip' -File | Sort-Object Name) {
                try {$null=Get-InitFull $file.FullName;$choices+=@($file)}
                catch {Write-Host ($script:InitMessages.skippedFirmware+' '+$file.Name+': '+$_.Exception.Message)}
            }
        }
        if($choices.Count -eq 1) {
            $Firmware=$choices[0].FullName
            Write-Host ($script:InitMessages.firmware+$Firmware)
        } elseif($choices.Count -gt 1) {
            Write-Host $script:InitMessages.selectFirmware
            for($i=0;$i -lt $choices.Count;$i++){Write-Host ('{0}. {1}' -f ($i+1),$choices[$i].Name)}
            $selection=([string](Read-Host $script:InitMessages.firmwareNumber)).Trim()
            if(-not $selection){Write-Host $script:InitMessages.cancelled;exit 0}
            $number=0
            if(-not [int]::TryParse($selection,[ref]$number) -or $number -lt 1 -or $number -gt $choices.Count){throw $script:InitMessages.invalidFirmware}
            $Firmware=$choices[$number-1].FullName
        } else {
            Write-Host $script:InitMessages.browseFirmware
            $Firmware=Select-InitFirmwareFile $script:InitMessages.dialogTitle
            if(-not $Firmware){Write-Host $script:InitMessages.cancelled;exit 0}
        }
        # Reject OTA/App-only input before presenting any board or opening a probe.
        $null=Get-InitFull $Firmware
    }
    if(($Action -eq 'Flash' -and ($Firmware -or $Config)) -or ($Action -ne 'Flash' -and $Materials)){throw 'Use -Materials for flash-hex, or -Firmware/-Config for the other entries'}
    $missing=if($Action -eq 'Flash'){-not $Materials}else{-not $Firmware -or -not $Config -or ($Action -eq 'Generate' -and -not $Output -and -not $DryRun)}
    if($Yes -and $missing){throw 'Automated -Yes requires all input paths; use -Help'}
    if($Action -eq 'Flash') {
        if(-not $Materials){$Materials=([string](Read-Host 'Initialization directory (empty cancels)')).Trim().Trim('"');if(-not $Materials){exit 0}}
    } else {
        if(-not $Config) {
            $choices=@(Get-ChildItem -LiteralPath (Join-Path $ToolRoot 'configs') -Filter '*.toml' -File | Sort-Object @{Expression={$_.BaseName.Length}},Name | ForEach-Object {
                if(-not (Test-Path -LiteralPath $manifestPath) -or $bundle.files.PSObject.Properties.Name -ccontains ('configs/'+$_.Name)) {
                    $c=Get-InitConfig $_.FullName
                    [pscustomobject]@{Path=$_.FullName;Label=($c.BoardMarking+' / '+$c.Note+' ['+$_.BaseName+']')}
                }
            })
            Assert-InitCondition ($choices.Count -gt 0) $script:InitMessages.noBoards
            Write-Host $script:InitMessages.selectBoard
            for($i=0;$i -lt $choices.Count;$i++){Write-Host ('{0}. {1}' -f ($i+1),$choices[$i].Label)}
            while(-not $Config) {
                $selection=([string](Read-Host $script:InitMessages.boardNumber)).Trim()
                if(-not $selection){exit 0}
                $number=0
                if([int]::TryParse($selection,[ref]$number) -and $number -ge 1 -and $number -le $choices.Count){$Config=$choices[$number-1].Path}
                elseif($Interactive){throw $script:InitMessages.invalidBoard}
                else {Write-Host 'Invalid selection; enter a listed number, or empty to cancel.'}
            }
        }
        if(-not $Firmware){$Firmware=([string](Read-Host 'Full ZIP path with App/Recovery/S112/GUI (empty cancels)')).Trim().Trim('"');if(-not $Firmware){exit 0}}
        if($Action -eq 'Generate' -and -not $Output -and -not $DryRun){$Output=([string](Read-Host 'New output directory (empty cancels)')).Trim().Trim('"');if(-not $Output){exit 0}}
    }
    if($Action -eq 'Flash') {
        Assert-InitCondition ([bool]$Materials -and -not $Firmware -and -not $Config) 'Specify -Materials initialization-directory'
        $prepared=Read-InitMaterials $Materials
    } else {
        Assert-InitCondition ([bool]$Firmware -and [bool]$Config -and -not $Materials) 'Specify -Firmware Full.zip and -Config marked-board.toml'
        $full=Get-InitFull $Firmware;$board=Get-InitConfig $Config
        $prepared=[pscustomobject]@{Full=$full;Config=$board;Data=(New-InitMaterialData $full $board)}
    }
    if($DryRun) {
        [pscustomobject]@{status='validated-no-probe';action=$Action;firmwareSha=$prepared.Full.PackageSha;configSha=$prepared.Config.Sha;appBytes=$prepared.Full.Image.AppBytes;recoveryBytes=$prepared.Full.Image.RecoveryBytes;pythonRequired=$false}|ConvertTo-Json
        exit 0
    }
    if($Action -eq 'Generate') {
        Assert-InitCondition ([bool]$Output) 'Specify -Output a new directory'
        $result=New-InitMaterials $full $board $Output
        [pscustomobject]@{status='initialization-generated';directory=$result.Directory;addressTable='addresses.json';programmingPerformed=$false;pythonRequired=$false}|ConvertTo-Json
        exit 0
    }
    Write-Host ($script:InitMessages.board+$prepared.Config.BoardMarking+' / '+$prepared.Config.Note)
    $firmwareLabel=if($Action -eq 'Flash'){Join-Path ([IO.Path]::GetFullPath($Materials)) 'source-full.zip'}else{[IO.Path]::GetFullPath($Firmware)}
    Write-Host ($script:InitMessages.firmware+$firmwareLabel)
    if($Action -eq 'Preserve'){Write-Host $script:InitMessages.preservation}
    Write-Host $script:InitMessages.scope
    Write-Host $script:InitMessages.warning
    Write-Host ($script:InitMessages.firmwareHash+$prepared.Full.PackageSha)
    if(-not $Yes) {
        Write-Host $script:InitMessages.confirm
        if((Read-Host) -notmatch '^(?i:y|yes)$'){Write-Host $script:InitMessages.cancelled;exit 0}
    }
    if(-not $Output){$Output=Join-Path ([IO.Path]::GetTempPath()) ('InkTag-native/nrf11-'+[guid]::NewGuid().ToString('N').Substring(0,12))}
    . (Join-Path $ToolRoot 'internal/initialization-session.ps1')
    $mode=if($Action -eq 'Preserve'){'Preserve'}else{'Clean'}
    $result=Invoke-InitFlash $prepared $mode $JLinkExe $JLinkSerial $Output $FrequencyHz $OperationTimeoutSeconds -RetryDetect:($RetryDetect -and !$NoWait)
    $result|ConvertTo-Json -Depth 8
    exit 0
} catch {
    if($_.Exception -is [OperationCanceledException]){Write-Host $script:InitMessages.cancelled;exit 130}
    if($script:InitMessages){Write-Host $script:InitMessages.failed}
    Write-Error -Message $_.Exception.Message -ErrorAction Continue
    exit 1
}
