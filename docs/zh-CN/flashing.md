# 完整烧录

完整烧录用于首次安装和救砖，会覆盖原固件。烧录前先备份原厂固件，并确认发布文件名中的主板 ID 与设备完全一致。

## 通用流程

1. 从同一个 GitHub Release 下载完整固件和 `SHA256SUMS.txt`。
2. 校验 SHA-256；校验失败时不要烧录。
3. 按该主板说明连接供电、GND、SWDIO、SWCLK，以及需要时的 RST/BOOT。
4. 运行对应系统的一键脚本或官方工具，等待擦除、写入和校验全部成功。
5. 按脚本提示释放 BOOT、断开调试器或重新上电，再测试蓝牙与屏幕。

不同厂商的官方工具和驱动许可不同，因此发布包默认只提供下载地址、经过验证的版本和调用脚本，不直接打包官方安装程序。

macOS/Linux 只有在支持表标为“已验证”时才属于正式支持；脚本存在不等于已经通过实机验证。

## 0.2 候选平台

| 平台 | 0.2 候选主板 | Windows 首刷方式 | Linux/macOS |
|---|---|---|---|
| Goodix | `gr5331_zkc21v`、GR5513/GR5515 ZKC42V/ZKC42VM | GProgrammer；GR551x 识别还需要 J-Link | 待实机验证 |
| OnMicro | `om6626_zkc21b`、`om6626_zkc42b` | J-Link Commander 7.88j 与 OM6626 SDK 设备包 | 待实机验证 |
| Telink | Hanshow Nowa 154R-N、Nebular 420R-N | CH340/串口转 SWS、Python 3、`pyserial` 与经固定版本审核的 pvvx 工具 | 待实机验证 |

Nordic 和 Dialog 主板在本次 0.2 候选中标为“延期”，即使文档列出也没有本次可烧录附件。

## Windows 一键入口

下载并解压 `InkTag-flash-tools-v0.2.0.zip`，再下载同一候选的准确 Full 文件。脚本默认持续等待目标；`-h` 显示参数说明。所有入口都要求传入本机原厂备份，并先按 `firmware/manifest.json` 校验 Full 文件的文件名、长度和 SHA-256。

Goodix 使用官方 GProgrammer 的 `GR5xxx_console.exe`。GR5513/GR5515 的封装识别还需要 [SEGGER J-Link 7.88j](https://www.segger.com/downloads/jlink/)：

```powershell
.\InkTag-flash-tools\windows\flash-goodix.ps1 -Board gr5331_zkc21v `
  -Firmware .\InkTag-gr5331_zkc21v-v0.2.0-full.bin `
  -StockBackup .\stock-flash.bin -ConsoleExe C:\path\GR5xxx_console.exe
```

OM6626 使用 J-Link 7.88j 和公开 [OM6626 SDK 镜像](https://gitee.com/jtcom/OM6626)中的设备包。Full ZIP 不要手工合并或任选其中一份；脚本固定按 state → code → device-information 三阶段执行：

```powershell
.\InkTag-flash-tools\windows\flash-om6626.ps1 -Board om6626_zkc21b `
  -Firmware .\InkTag-om6626_zkc21b-v0.2.0-full.zip `
  -StockBackup .\stock-flash.bin -OmSdkRoot C:\path\OM6626.SDK
```

Telink 使用 Python 3、`pyserial`、CH340/3.3 V UART-to-SWS，以及固定到提交 `6455f50f25dd264ac6820ecacfd64b90c6c80f3e` 的 [pvvx/TlsrComSwireWriter](https://github.com/pvvx/TlsrComSwireWriter)。0.2 中两块 Hanshow 主板仍是实验候选，必须显式确认布局，并保留脚本的完整读回验证：

```powershell
python -m pip install pyserial
.\InkTag-flash-tools\windows\flash-telink.ps1 -Board hs_nowa154 `
  -Firmware .\InkTag-hs_nowa154-v0.2.0-full.bin `
  -StockBackup C:\path\verified-backup -ToolPath C:\path\TLSR825xComFlasher.py `
  -AcknowledgeExperimentalLayout
```

0.2 候选没有宣称 Linux/macOS 首刷合格；不要把 PowerShell 7 能启动脚本等同于已通过实机验证。

## 首刷前检查

- 从 `firmware/manifest.json` 找到准确的 `boardId`，再核对下载文件的 SHA-256。
- 保存同一台设备的完整原厂备份；不要借用同型号另一台设备的备份替代。
- 使用稳压供电并共地。墨水屏刷新电流通常高于仅让 MCU 启动和广播所需的电流。
- 只连接手册要求的 BOOT/RST。成功迁移到 InkTag 后，正常维护应允许直接调试连接。
- 关闭占用同一调试器、串口或蓝牙设备的其他程序。

## 成功判定与恢复

“程序写入完成”还不等于整机通过。至少确认擦除、写入、读回校验、正常上电、正确蓝牙身份、清屏和一次页面刷新。写入中断后不要盲目反复全擦；先按对应平台的 Recovery/首刷入口重新连接。脚本找不到目标时优先检查 VTref、电源、共地、线序和接触，再降低接口速度。

`flash/windows/` 是本候选的公开烧录入口，`flash/runtime/` 是脚本所需的受限运行时，不能单独调用或删改。某个已附带 Full 固件的主板如果缺少可独立运行、带帮助和安全校验的脚本，应视为审核阻断项，不能因为厂商 GUI 能手工写入就宣称“一键烧录已支持”。
