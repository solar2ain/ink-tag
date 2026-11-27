# Windows dialogs are only used to fill omitted interactive inputs.
function New-InkTagToolFileDialog {
    param([switch]$Save)
    if ([Threading.Thread]::CurrentThread.GetApartmentState() -ne 'STA') {
        throw 'File selection requires STA. Run the BAT entry or powershell.exe -STA; no device accessed.'
    }
    Add-Type -AssemblyName System.Windows.Forms
    if ($Save) { return New-Object System.Windows.Forms.SaveFileDialog }
    return New-Object System.Windows.Forms.OpenFileDialog
}

function Select-InkTagToolFile {
    param([string]$Title, [string]$Filter, [string]$Directory, [switch]$Save)
    $picker = New-InkTagToolFileDialog -Save:$Save
    try {
        $picker.Title = $Title
        $picker.Filter = $Filter
        $picker.InitialDirectory = $Directory
        $picker.CheckPathExists = $true
        if ($Save) {
            $picker.AddExtension = $true
            $picker.DefaultExt = 'bin'
            $picker.OverwritePrompt = $false
        } else {
            $picker.Multiselect = $false
            $picker.CheckFileExists = $true
        }
        if ($picker.ShowDialog().ToString() -ne 'OK' -or !$picker.FileName) {
            throw (Get-InkTagFlashText 'Cancelled; no device accessed.' '已取消，未访问设备。')
        }
        if ($Save -and (Test-Path -LiteralPath $picker.FileName)) {
            throw 'Output exists; choose a new BIN path. No file overwritten; no device accessed.'
        }
        return $picker.FileName
    } finally { $picker.Dispose() }
}

function Select-InkTagBoardConfig {
    param([string]$Directory)
    if ($env:INKTAG_TLSR_RELEASE_TOOLS) { $Directory = Join-Path $env:INKTAG_TLSR_RELEASE_TOOLS 'configs' }
    $presets = @(Get-ChildItem -LiteralPath $Directory -File | Where-Object {
        $_.Extension.ToLowerInvariant() -in @('.toml', '.yaml', '.yml', '.json')
    } | Sort-Object Name)
    if (!$presets.Count) {
        throw (Get-InkTagFlashText 'No board config found. Check the board configuration directory; no device accessed.' '找不到主板配置，请检查板型配置目录。')
    }
    return Select-InkTagNumberedFile -Files $presets -Stage (Get-InkTagFlashText 'SELECT PCB AND SCREEN' '选择主板和屏幕')
}

function Select-InkTagFlashFirmware {
    param([string]$Directory, [string]$FullDirectory)
    if ($env:INKTAG_TLSR_RELEASE_TOOLS) {
        $Directory = $env:INKTAG_TLSR_RELEASE_TOOLS
        $FullDirectory = Join-Path $Directory '../full'
    }
    $folders = @($Directory)
    if ($FullDirectory -and (Test-Path -LiteralPath $FullDirectory -PathType Container)) { $folders += $FullDirectory }
    $packages = @(Get-ChildItem -LiteralPath $folders -File -Filter '*.zip' | Where-Object {
        $_.Name -match '(?:-complete-firmware|-full)\.zip$'
    } | Sort-Object Name)
    if ($packages.Count -eq 1) {
        $selected = Get-InkTagFlashText 'Full firmware selected for validation: {0}' '将校验同目录的唯一完整固件：{0}'
        Write-InkTagTerminalMessage ($selected -f $packages[0].Name)
        return $packages[0].FullName
    }
    if ($packages.Count) {
        return Select-InkTagNumberedFile -Files $packages -Stage (Get-InkTagFlashText 'SELECT COMPLETE FIRMWARE' '选择完整固件')
    }
    $title = Get-InkTagFlashText 'Select InkTag complete firmware ZIP' '选择 InkTag 完整固件 ZIP'
    return Select-InkTagToolFile -Title $title -Filter 'InkTag complete firmware (*.zip)|*.zip' -Directory $Directory
}

function Select-InkTagNumberedFile {
    param([object[]]$Files, [string]$Stage)
    Write-InkTagTerminalStage $Stage
    for ($i = 0; $i -lt $Files.Count; $i++) {
        Write-Host ('  {0}. {1}' -f ($i + 1), $Files[$i].BaseName)
    }
    $choice = (Read-Host (Get-InkTagFlashText 'Number (empty cancels)' '输入编号（回车取消）')).Trim()
    [int]$selected = 0
    if (!$choice) { throw (Get-InkTagFlashText 'Cancelled; no device accessed.' '已取消，未访问设备。') }
    if (![int]::TryParse($choice, [ref]$selected) -or $selected -lt 1 -or $selected -gt $Files.Count) {
        throw (Get-InkTagFlashText 'Invalid selection number; no device accessed.' '编号无效，未访问设备。')
    }
    return $Files[$selected - 1].FullName
}
