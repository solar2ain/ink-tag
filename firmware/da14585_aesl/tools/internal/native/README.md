# InkTag da14585_aesl 完整烧录说明

中文 | [English](README.en.md)

工具版本 **0.2.0.12**。适用于 Windows x64、Windows PowerShell **5.1**（也支持 PowerShell 7）。工具与固件独立提供，不需要安装 InkTag CLI。

## 开始前

1. 保留完整工具目录，在**本 README 所在目录**打开终端。下方每个代码块都是可单独复制的一行命令，不使用 `&`。
2. 下载对应主控的 **完整固件**，保留文件原名，放在本目录的**上一级目录**。不要将 OTA 包交给完整烧录工具。
3. 核对设备型号、主板丝印与实际屏幕，再选择下方配置。同一型号可能存在不同主板；同一主板也可能换装不同屏幕。
4. 接好稳定电源、共地和烧录线。保持默认确认和完整回读；首次烧录不要跳过回读。

本版示例使用的完整固件文件：`InkTag-da14585_aesl-full-v0.2.0.34462-e3c0352c6f9c.bin`。如果移动或重命名文件，只需替换 `-Firmware` 路径；工具不会绑定、自动搜索或下载这一版固件。

除必需的 `-Firmware`、`-Config` 外，烧录命令默认只加 `-RetryDetect`。它只重试最初的设备检测，不会重放擦写或结果未知的操作。速度、超时和确认方式均使用工具默认值。

> [!WARNING]
> **当前便携工具尚未开放实际完整烧录。** 下方列出每个配置的完整调用方式，但直接执行会在写入前拒绝。可在命令末尾加 `-DryRun` 做离线预检；预检成功不代表可以写入。不要用 `-Yes` 或修改脚本绕过限制。
>
> DA 所需 RAM 辅助程序未随包提供，目前也没有公开下载入口。本版不提供独立备份命令。

## 准备工具

安装官方 J-Link **7.88j** 与 USB 驱动；便携 Python 和所需依赖已随目录提供，无需 SDK、编译器或系统 Python。只连接一只探针时使用默认选择；连接多只时需额外指定 `-JLinkSerial`，避免选错。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\setup.ps1"
```


```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\check.ps1"
```

## 按设备和主板选择配置

### AESL／盒马 AESL-2.9 — asel29.toml

- 主板丝印：**未见丝印**。
- 屏幕：128×296，黑白红。
- 配置：`tools/configs/asel29.toml`。2.9 寸原屏；当前便携工具未开放实际完整烧录。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Firmware "..\InkTag-da14585_aesl-full-v0.2.0.34462-e3c0352c6f9c.bin" -Config ".\tools\configs\asel29.toml" -RetryDetect
```

### AESL／盒马 AESL-4.2 — asel42.toml

- 主板丝印：**未见丝印**。
- 屏幕：400×300，黑白红。
- 配置：`tools/configs/asel42.toml`。4.2 寸 UC8176 原屏；当前便携工具未开放实际完整烧录。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Firmware "..\InkTag-da14585_aesl-full-v0.2.0.34462-e3c0352c6f9c.bin" -Config ".\tools\configs\asel42.toml" -RetryDetect
```

### AESL／盒马 AESL-4.2（换装 SSD1683 屏） — asel42-ssd1683.toml

- 主板丝印：**未见丝印**。
- 屏幕：400×300，黑白红。
- 配置：`tools/configs/asel42-ssd1683.toml`。同一 4.2 寸主板，换装 HINK-E042A13-FPC-A0／SSD1683 400×300 屏；当前便携工具未开放实际完整烧录。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Firmware "..\InkTag-da14585_aesl-full-v0.2.0.34462-e3c0352c6f9c.bin" -Config ".\tools\configs\asel42-ssd1683.toml" -RetryDetect
```

## 烧录后的数据与检查

完整烧录会重置设备设置、密码、设备端 Find 信息、用户字库、旧图片状态与崩溃记录。旧图片可能只是失效，并非安全擦除；无法清理的独立外置 Flash 保留。主机上的 Apple 会话、Find 身份资料和轨迹不由本工具删除。

主板必需的出厂身份、蓝牙地址及校准按工具规则保留。本主控仅提供 Free 功能，不支持激活 Pro。

看到确认提示后再输入 `y`。最终成功以全部要求的回读与验证通过为准；单个阶段达到 100% 不代表完成。遇到错误先检查接线、目标、固件和配置，不盲目重复擦写。

## 探测、备份和帮助

`probe.ps1` 用于探测，不需要固件；已有 `backup.ps1` 时可单独保存原厂内容。它们位于上面的同一工具子目录。先查看入口帮助和输出位置，不在走时或功耗测试期间探测。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Help
```

工具不包含密钥、厂商 SDK、固件源码或用户备份。保留整个目录及随附 LICENSE、NOTICE、字体／图标及依赖许可文件；不要只复制单个脚本。
