# DA14585 工具

中文 | [English](README.en.md)

工具版本 **0.2.0.12**。用于 Windows x64 上的 DA14585 探测、固件与主板配置检查。工具独立于 InkTag CLI，不包含固件、SDK、编译器或厂商调试器。

> [!NOTE]
> 本目录可以探测设备、离线检查固件及配置，**暂不支持实际完整烧录**。所需 RAM 辅助程序未随包提供，目前没有公开下载入口；即使自行提供该文件，便携工具的写入入口仍未开放。`-Yes` 不会解除限制。此版本不提供独立备份命令。

## 准备环境

保留完整目录结构，在工具目录打开 64 位 PowerShell 5.1 或 7，运行：

```powershell
& .\tools\setup.ps1
& .\tools\check.ps1
```

随目录提供 Python 3.13.16 和固定版本依赖。安装只创建本目录的 `tools/.runtime`，不联网、不修改系统 Python、驱动、服务或执行策略。检查不会连接设备。

探测还需要自行安装 [SEGGER J-Link 7.88j](https://www.segger.com/downloads/jlink/)。默认目录为 `C:/Program Files/SEGGER/JLink_V788j`；其他位置可在当前 PowerShell 设置 `INKTAG_JLINK_HOME`。所需文件及校验值见 `tools/vendor-dependencies.json`，检查失败时请核对版本，不要替换成未经检查的程序。

## 探测设备

接好稳定电源、共地和 SWD 后执行：

```powershell
& .\tools\probe.ps1 -Chip DA14585
& .\tools\probe.ps1 -Chip DA14585 -JLinkSerial 20090928 -FrequencyHz 1000000
```

将示例序列号换成你的探针序列号。连接多个探针时必须明确选择。探测默认 1 MHz；`-FrequencyHz` 的单位是 Hz，须为整千赫兹。也接受 `-SpeedKHz`，两者不能同时使用。`-JLink`、`-Snr` 是 `-JLinkSerial` 的别名。

默认只探测一次。需要等待接触稳定时可加 `-RetryDetect -TimeoutSeconds 60`；它只重试探测，不重放写入或结果未知的操作。探测会影响设备的调试电源状态，请不要在功耗或走时测试期间使用。

## 检查完整固件与配置

完成环境准备后，可执行以下离线检查；不连接设备，也不需要 J-Link 或 RAM 辅助程序：

```powershell
& .\tools\validate.ps1 -Chip DA14585 -Firmware 'D:\Firmware\InkTag-da14585_aesl-full.bin' -Config .\tools\configs\asel29.toml
```

根据实际硬件选择配置，三个配置使用同一份完整固件：

| 配置 | 屏幕 |
| --- | --- |
| `asel29.toml` | 已适配的 AESL 2.9 寸原屏 |
| `asel42.toml` | 已适配的 AESL 4.2 寸 UC8176 原屏 |
| `asel42-ssd1683.toml` | 同主板换装 SSD1683 400×300 屏 |

不要仅凭屏幕尺寸选择配置。配置提供屏幕引脚和板级 EN 有效电平；其他屏幕信号的有效电平由驱动决定。

检查会核对 App 签名、原生镜像格式、CRC、布局、容量及配置。Recovery、加载器和 GUI 的格式或 CRC 检查不等于签名验证。检查成功只表示文件通过离线检查，不表示当前设备可以安全烧录。

`flash.ps1 -DryRun` 还可查看完整烧录预检，须提供同样的 `-Firmware` 和 `-Config` 参数。完整烧录预检默认 SWD 为 500000 Hz、SRAM 块为 256 字节；本目录的实际写入仍不可用。固件必须单独传入，工具不会自动选择固件。

## 帮助、日志与许可

各入口支持 `-Help`。探测支持 `-DryRun`，不会打开探针。一般日志显示阶段和已用时间，`-Verbose` 展开细节；失败记录会保留，不自动重新开始操作。

请保留 `tools/`、`dependencies/`、`licenses/` 及清单文件。`tool-manifest.json` 记录文件大小和 SHA256。许可全文见本目录 `LICENSE` 和 `licenses/`；第三方文件保留原许可。工具不含密钥、设备备份或用户数据。
