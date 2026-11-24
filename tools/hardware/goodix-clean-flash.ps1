[CmdletBinding()]
param(
    [ValidateSet('generate','flash','preserve')][string]$Action,
    [ValidateSet('gr5331','gr551x')][string]$Port,
    [string]$Firmware='', [string]$Config='', [string]$Output='',
    [string]$Materials='', [string]$Evidence='',
    [string]$ConsoleExe='', [string]$JLinkExe='', [string]$Snr='',
    [switch]$Yes, [switch]$Help, [switch]$DryRun, [switch]$RetryDetect=$true,
    [switch]$InteractiveMenu, [ValidateSet('en','zh')][string]$Language='en',
    [string]$ToolDirectory=''
)
$ErrorActionPreference='Stop'
if($Help) {
    Write-Output @'
GR clean initialization / preservation (Windows PowerShell 5.1, no Python).
Recommended normal programming: flash.bat (preserve usable original data first).
generate-bin.bat -Firmware FULL.zip -Config BOARD.toml -Output NEW_DIRECTORY
flash-bin.bat -Materials DIRECTORY -ConsoleExe GR5xxx_console.exe [-Snr SERIAL] [-Yes]
flash.bat -Firmware FULL.zip -Config BOARD.toml -ConsoleExe GR5xxx_console.exe [-Snr SERIAL] [-Yes]
Optional: -JLinkExe PATH, -Evidence NEW_DIRECTORY, -DryRun. Paths may contain spaces.
Double-click flash.bat for the English guide or flash-zh.bat for Chinese.
The guide uses a single verified local Full ZIP automatically, then asks for
the board. It lists multiple candidates and opens a file picker if none qualify.
Full flashing requires FLASH confirmation before device access, unless -Yes
was supplied. Connect the device when prompted. Esc cancels waiting.
Supplied arguments skip their prompts; Enter cancels a prompt.
Multiple probes require an explicit choice. GUI is already inside mcu.bin.
generate-bin and flash-bin prompt only for their own required inputs.
Firmware is always an explicit external Full
package, never an App-only BIN. Probe/write dependencies are the existing
Goodix GProgrammer installation (native loaders/DLLs) and J-Link 7.88j.
Preserve keeps usable address, readable native overrides and verified same-unit
Pro; absent/unreadable optional records initialize defaults/unactivated.
MCU user data is cleared. OTP/eFuse/security spaces and external SPI image
Flash are skipped. No automatic write retry; exact MCU and readback mandatory.
Generation, writing and matching readback are reported separately. This new
entry has host fake-device coverage; it does not claim new physical Full tests.
'@
    exit 0
}
try {
    . (Join-Path $PSScriptRoot 'goodix-clean-programmer.ps1')
    if($RetryDetect -or $InteractiveMenu -or ($Action -ceq 'preserve' -and !$Yes -and !$DryRun)) {
        . (Join-Path $PSScriptRoot 'goodix-clean-guide.ps1')
    }
    if($InteractiveMenu) {
        Assert-Gr ($Action -ceq 'preserve' -and !$Yes -and !$DryRun -and $ToolDirectory) 'Invalid interactive flash entry'
        [Console]::InputEncoding = [Text.Encoding]::UTF8
        [Console]::OutputEncoding = [Text.Encoding]::UTF8
        $ToolDirectory = [IO.Path]::GetFullPath($ToolDirectory)
        foreach($line in @('intro','wiring','procedure','cancel')) { Write-Host $script:GrGuideText[$line] }
    }
    function Read-GrPath {
        param([string]$Prompt)
        $suffix=if($InteractiveMenu -and $Language -ceq 'zh'){' (Enter)'}else{' (Enter cancels)'}
        $value=(Read-GrText ($Prompt+$suffix)).Trim().Trim('"')
        Assert-Gr (![string]::IsNullOrWhiteSpace($value)) 'Cancelled before probe access'
        return $value
    }
    function Get-GrPresetsDirectory {
        if($InteractiveMenu) {
            $folder=Join-Path $ToolDirectory 'configs'
            Assert-Gr (Test-Path -LiteralPath $folder -PathType Container) $script:GrGuideText.boardMissing
            return $folder
        }
        $folder=Join-Path $script:GrCleanRoot ('tools/platforms/goodix/'+$Port+'/programmer/configs')
        if (!(Test-Path -LiteralPath $folder -PathType Container)) {
            $folder=Join-Path $script:GrCleanRoot ('firmware/ports/goodix/'+$Port+'/tools/configs')
        }
        if(!(Test-Path -LiteralPath $folder -PathType Container)) {
            $bundle=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../..'))
            Assert-Gr (Test-Path -LiteralPath (Join-Path $bundle 'inktag-goodix-tools.json') -PathType Leaf) 'Board preset directory missing'
            $folder=Join-Path $bundle 'tools/configs'
        }
        return $folder
    }
    function Select-GrBoard {
        $folder=Get-GrPresetsDirectory
        $presets=@(Get-ChildItem -LiteralPath $folder -File | Where-Object {$_.Extension -cin @('.toml','.json')} | Sort-Object Name)
        $missing=if($InteractiveMenu){$script:GrGuideText.boardMissing}else{'No supported board presets supplied'}
        Assert-Gr ($presets.Count -gt 0) $missing
        for($i=0;$i -lt $presets.Count;$i++) {
            $board=Read-GrConfig $presets[$i].FullName
            Assert-Gr (($Port -ceq 'gr5331') -eq ($board.chip -ceq 'GR5331')) 'Bundled preset is for another MCU'
            Write-Host ('{0}. {1} / {2} ({3})' -f ($i+1),$presets[$i].BaseName,$board.facts.board_marking,$board.chip)
        }
        $prompt=if($InteractiveMenu){$script:GrGuideText.board}else{'Step 1: select exact board number (Enter cancels)'}
        $invalid=if($InteractiveMenu){$script:GrGuideText.boardInvalid}else{'Invalid board number'}
        $answer=Read-GrText $prompt;$number=0
        Assert-Gr (![string]::IsNullOrWhiteSpace($answer)) 'Cancelled before probe access'
        Assert-Gr ([int]::TryParse($answer,[ref]$number) -and $number -ge 1 -and $number -le $presets.Count) $invalid
        return $presets[$number-1].FullName
    }
    Assert-Gr ($Action -cin @('generate','flash','preserve') -and $Port -cin @('gr5331','gr551x')) 'Use the board generate-bin/flash-bin/flash BAT entry'
    if($Action -ceq 'flash') {
        if(!$Materials){$Materials=Read-GrPath 'Initialization materials directory'}
        $materialReview=Read-GrCleanMaterials $Materials $Port;$package=$materialReview.firmware;$selected=$materialReview.config
        $sourceFirmware=Join-Path $Materials 'components.zip'
    } else {
        if($InteractiveMenu -and !$Firmware){$Firmware=Select-GrFirmware $ToolDirectory $Port}
        if($Action -ceq 'preserve' -and !$Config){$Config=Select-GrBoard}
        if(!$Firmware){$Firmware=Read-GrPath $(if($Action -ceq 'preserve'){'Step 2: external Full ZIP path'}else{'External Full ZIP path'})}
        if(!$Config){Write-Host ('Board presets: '+(Get-GrPresetsDirectory));$Config=Read-GrPath 'Exact board TOML/JSON path'}
        $selected=Read-GrConfig $Config
        Assert-Gr (($Port -ceq 'gr5331') -eq ($selected.chip -ceq 'GR5331')) 'Wrong board tool directory'
        if($Action -ceq 'generate') {
            if(!$Output){$Output=Read-GrPath 'New output directory'}
            $result=New-GrCleanMaterials $Firmware $Config $Output $Port
            $result|ConvertTo-Json -Depth 20;exit 0
        }
        $package=Read-GrFirmware $Firmware $selected.chip
        $sourceFirmware=$Firmware
        # Fail before probe access if these components cannot boot without MAC.
        $null=New-GrCleanImage $package $selected
    }
    if($DryRun) {
        @{dryRun=$true;firmware=[IO.Path]::GetFullPath($sourceFirmware);chip=$selected.chip;family=$package.family;hardwareAccessed=$false}|ConvertTo-Json
        exit 0
    }
    if($Action -ceq 'preserve' -and !$Yes) {
        Write-Host ($script:GrGuideText.selection -f $selected.facts.board_marking,$selected.chip,[IO.Path]::GetFullPath($sourceFirmware))
        Write-Host $script:GrGuideText.fullImpact
        Assert-Gr ((Read-GrText $script:GrGuideText.confirmation) -ceq 'FLASH') $script:GrGuideText.fullCancelled
    } elseif($Action -ceq 'flash' -and !$Yes) {
        Assert-Gr ((Read-GrText 'This initializes MCU user data. Type FLASH to continue') -ceq 'FLASH') 'Cancelled before device access'
    }
    if(!$ConsoleExe){
        $ConsoleExe=Join-Path $script:GrCleanRoot '.tools/gprogrammer/_._/GR5xxx_console.exe'
        if(!(Test-Path -LiteralPath $ConsoleExe) -and !$Yes){
            $prompt=if($InteractiveMenu){$script:GrGuideText.console}else{'Installed GR5xxx_console.exe path (keep its native loader/DLL directory)'}
            $ConsoleExe=Read-GrPath $prompt
        }
    }
    Assert-Gr (Test-Path -LiteralPath $ConsoleExe -PathType Leaf) 'Install native GProgrammer and pass -ConsoleExe; no automatic download'
    . (Join-Path $script:GrCleanRoot 'tools/jlink-tools.ps1')
    $JLinkExe=Resolve-InkTagJLinkTool -ToolName 'JLink.exe' -ExplicitPath $JLinkExe -RepoRoot $script:GrCleanRoot
    Assert-Gr (!$Snr -or $Snr -cmatch '^\d+$') 'Invalid exact J-Link serial'
    if(!$Evidence){$Evidence=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) ('InkTag/programmer/gr-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'-'+[Guid]::NewGuid().ToString('N'))}
    Assert-Gr (!(Test-Path -LiteralPath $Evidence)) 'Evidence directory exists; use a fresh directory'
    $null=New-Item -ItemType Directory -Path $Evidence
    $options=@{console=[IO.Path]::GetFullPath($ConsoleExe);jlink=$JLinkExe;snr=$Snr;evidence=[IO.Path]::GetFullPath($Evidence);powershell=(Join-Path $PSHOME 'powershell.exe');counter=0;yes=[bool]$Yes;firmwarePath=[IO.Path]::GetFullPath($sourceFirmware);guide=$script:GrGuideText;retryDetect=[bool]$RetryDetect}
    if(!(Test-Path -LiteralPath $options.powershell)){$options.powershell=(Join-Path $PSHOME 'pwsh.exe')}
    $result=Invoke-GrCleanFlash $options $package $selected -Preserve:($Action -ceq 'preserve')
    $result|ConvertTo-Json -Depth 25
    exit 0
} catch {
    if($InteractiveMenu -and $script:GrGuideText){[Console]::Error.WriteLine($script:GrGuideText.errorPrefix)}
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
