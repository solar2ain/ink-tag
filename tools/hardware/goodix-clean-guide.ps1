$script:GrGuideText = if ($Language -eq 'zh') {
    @{
        intro = 'InkTag GR 完整刷机'
        wiring = '用 J-Link 连接 SWDIO、SWCLK、GND 和目标参考电压，给设备供电。需安装 Goodix GProgrammer 和 SEGGER J-Link。'
        procedure = '流程：选择并校验完整固件与主板 → 确认完整刷机 → 等待并核对设备 → 写入与回读校验。'
        cancel = '选择或确认时按 Enter 取消，等待设备时按 Esc 取消。确认前不会访问设备。'
        firmware = '选择完整固件编号（Enter 取消）'
        firmwareSelected = '已采用固件'
        dialog = '选择 InkTag 完整固件 ZIP'
        firmwareCancelled = '已取消固件选择。'
        firmwareInvalid = '无效的固件编号。'
        board = '选择准确主板编号（Enter 取消）'
        boardMissing = '未找到工具随附的主板配置。'
        boardInvalid = '无效的主板编号。'
        console = '已安装的 GR5xxx_console.exe 路径'
        probe = '选择 J-Link 探针编号（Enter 取消）'
        waiting = '正在等待 J-Link 和目标设备。请检查接线并给设备供电，按 Esc 取消。'
        waitCancelled = '已取消等待，未写入。'
        selection = '主板：{0}（{1}）；完整固件：{2}'
        fullImpact = '完整刷机会清除 MCU 上的设置、密码、Find、图片索引和健康历史。脚本会尝试保留同机可验证的地址、Pro 和 SDK 配置，并跳过独立 SPI 图片 Flash。'
        confirmation = '是否完整刷机？输入 FLASH 确认，或按 Enter 选择否（默认）'
        fullCancelled = '已取消完整刷机，未访问设备。'
        errorPrefix = '刷机失败'
    }
} else {
    @{
        intro = 'InkTag GR Full flashing'
        wiring = 'Connect J-Link SWDIO, SWCLK, GND and target reference voltage, and power the device. Install Goodix GProgrammer and SEGGER J-Link.'
        procedure = 'Select and validate the Full package and board, confirm Full flashing, wait for and verify the device, then write and verify readback.'
        cancel = 'Press Enter to cancel a selection or confirmation, or Esc while waiting. Device access starts after confirmation.'
        firmware = 'Select the Full firmware number (Enter cancels)'
        firmwareSelected = 'Using firmware'
        dialog = 'Select an InkTag Full firmware ZIP'
        firmwareCancelled = 'Firmware selection cancelled.'
        firmwareInvalid = 'Invalid firmware number.'
        board = 'Select the exact board number (Enter cancels)'
        boardMissing = 'No supported board configuration was bundled with these tools.'
        boardInvalid = 'Invalid board number.'
        console = 'Path to the installed GR5xxx_console.exe'
        probe = 'Select the J-Link probe number (Enter cancels)'
        waiting = 'Waiting for J-Link and the target. Check the wiring and power, or press Esc to cancel.'
        waitCancelled = 'Cancelled while waiting, before writing.'
        selection = 'Board: {0} ({1}); Full firmware: {2}'
        fullImpact = 'Full flashing clears MCU settings, passwords, Find, the image index and health history. It attempts to retain verifiable same-device address, Pro and SDK settings, and skips external SPI image Flash.'
        confirmation = 'Perform Full flashing? Type FLASH to continue, or press Enter for No (default)'
        fullCancelled = 'Full flashing cancelled before device access.'
        errorPrefix = 'Flashing failed'
    }
}

function Show-GrFirmwareDialog {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    try {
        $dialog.Title = $script:GrGuideText.dialog
        $dialog.Filter = 'Full firmware ZIP (*.zip)|*.zip'
        $dialog.CheckFileExists = $true
        $dialog.RestoreDirectory = $true
        if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
            return $dialog.FileName
        }
    } finally {
        $dialog.Dispose()
    }
}

function Select-GrFirmware {
    param([string]$ToolDirectory,[ValidateSet('gr5331','gr551x')][string]$Port)
    $parent = Split-Path -Parent $ToolDirectory
    $folders = @($ToolDirectory, (Join-Path $ToolDirectory 'full'),
                 $parent, (Join-Path $parent 'full'))
    $files = @(foreach ($folder in $folders) {
        if (Test-Path -LiteralPath $folder -PathType Container) {
            Get-ChildItem -LiteralPath $folder -Filter '*.zip' -File
        }
    })
    $chips = if ($Port -ceq 'gr5331') { @('GR5331') } else { @('GR5513','GR5515') }
    $family = $null
    $bundleManifest = Join-Path $parent 'inktag-goodix-tools.json'
    if (Test-Path -LiteralPath $bundleManifest -PathType Leaf) {
        $family = (Get-GrJson ([InkTag.GoodixBytes]::ReadFile($bundleManifest,131072))).firmwareFamily
    }
    $files = @(foreach ($file in ($files | Sort-Object -Property FullName -Unique)) {
        foreach ($chip in $chips) {
            try {
                $package = Read-GrFirmware $file.FullName $chip
            } catch {
                continue
            }
            if (!$family -or $package.family -ceq $family) { $file }
            break
        }
    })
    if (!$files.Count) {
        $selected = Show-GrFirmwareDialog
        Assert-Gr (![string]::IsNullOrWhiteSpace($selected)) $script:GrGuideText.firmwareCancelled
        return $selected
    }
    if ($files.Count -eq 1) {
        Write-Host ('{0}: {1}' -f $script:GrGuideText.firmwareSelected,$files[0].Name)
        return $files[0].FullName
    }
    for ($i = 0; $i -lt $files.Count; $i++) {
        Write-Host ('{0}. {1}' -f ($i + 1), $files[$i].FullName)
    }
    $answer = Read-GrText $script:GrGuideText.firmware
    Assert-Gr (![string]::IsNullOrWhiteSpace($answer)) $script:GrGuideText.firmwareCancelled
    $number = 0
    Assert-Gr ([int]::TryParse($answer, [ref]$number) -and $number -ge 1 -and
        $number -le $files.Count) $script:GrGuideText.firmwareInvalid
    return $files[$number - 1].FullName
}

function Test-GrWaitCancelled {
    if (![Console]::IsInputRedirected -and [Console]::KeyAvailable) {
        return [Console]::ReadKey($true).Key -eq [ConsoleKey]::Escape
    }
    return $false
}
