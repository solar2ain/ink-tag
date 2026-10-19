# 首次烧录与有线恢复

完整烧录用于将原厂设备首次安装为 InkTag、更新固定 Recovery，或在无法无线恢复时维修设备。正常应用更新使用 [OTA](ota.md)。完整烧录可能覆盖原固件或改变未发布的数据布局，请先确认准确主控、PCB 型号和供电。

## 准备

从同一发布包获取准确板型的完整烧录文件、烧录工具和校验清单。核对 `firmware/manifest.json` 中的名称、长度和 SHA-256。所有低层身份、容量、布局和读回检查仍然有效；不得通过改名或强制擦除绕过。

原厂备份建议单独保存。默认入口要求备份；明确接受无法返回原厂状态时可选择 `-SkipStockBackupCheck`。该参数直接跳过原厂备份要求，不需要提供备份文件，不会跳过主控、Flash、供电或镜像检查。

本指南只配置烧录运行环境，不要求编译器、Keil、Arm 工具链或前端构建工具。厂商工具、驱动和外置 Flash loader 需要按其许可单独安装，不随固件包分发。

| 系列 | Windows 烧录环境 | 本包入口 |
| --- | --- | --- |
| GR5331 / GR5513 / GR5515 | 原生 PowerShell、Goodix GProgrammer；GR551x 另需 J-Link 做芯片识别 | `flash-goodix.ps1`，还需对应已提供完整烧录与 runtime |
| OM6626 | 原生 PowerShell、J-Link 与 OM6626 设备包 | `flash-om6626.ps1`；`-OmSdkRoot` 仅用于找到厂商设备包，不要求编译固件 |
| TLSR8359 | Python、pyserial、合格 UART-to-SWS 接线与对应工具 | `flash-telink.ps1`，以本包已提供板型为准 |
| nRF52811 / nRF52832 | J-Link、稳定 SWD 接线；nRF52811 还涉及外置字体区 | 当前公开包尚未提供完整已验证的 Nordic 完整烧录启动入口 |
| DA14585 | J-Link/J-Flash 与 DA14585 外置 Flash loader；不需要 Keil | 当前公开包尚未提供完整已验证的 Dialog 完整烧录启动入口 |

预览中包含完整烧录文件不等于烧录工具已就绪。没有合格入口时不要使用通用 `erase` / `loadfile` 替代，也不要从独立源码仓库复制部分脚本后猜测依赖。请等待对应工具包更新。

## 接线与供电

SWD 平台连接主供电、共地、SWDIO、SWCLK 和探针所需电压参考；VTref 是参考输入，不是可靠的供电输出。TLSR 使用对应 SWS 接口，不按 SWD 接线套用。保持线短、接触稳固，连接前核对准确 PCB，而不是仅看屏幕英寸。

nRF52811 EL022H2 与 EL029H2 不自动共用接线结论。DA asel29 已确认 TP5 为 SWCLK、TP6 为 SWDIO；DA 4.2 大焊盘版按其信号标注核对，不照搬 2.9 寸 TP 编号。DA 本流程不连接 VPP/TP3、不烧 OTP；RST 为高有效，不能直接按普通低有效 nRESET 连接。任何板型都不要接 5 V 或猜测未标明的焊盘。

接线与镜像未确认前不要开始写入。低电压、未知芯片或读回失败时停止并检查现场，不反复全擦。

## Windows 入口

安装各平台的官方驱动和工具。以下命令仅适用于清单中实际提供完整烧录文件的板型；未提供附件时脚本会拒绝。使用 `-h` 查看完整参数。

Goodix：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\flash\windows\flash-goodix.ps1 -Board gr5331_zkc21v -Firmware .\InkTag-gr5331_zkc21v-v0.2.0-full.bin -StockBackup .\stock.bin -ConsoleExe C:\Tools\GProgrammer\GR5xxx_console.exe
```

GR5513/GR5515 选择 `gr5513_zkc42v`、`gr5513_zkc42vm` 或 `gr5515_zkc42vm`，并使用对应完整烧录。GR551x 封装识别另需 J-Link；不要将 GR5513 与 GR5515 的容器互换。

OM6626：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\flash\windows\flash-om6626.ps1 -Board om6626_zkc21b -Firmware .\InkTag-om6626_zkc21b-v0.2.0-full.zip -StockBackup .\stock.bin -OmSdkRoot C:\Tools\OM6626.SDK
```

OM 使用 J-Link 和厂商设备包。完整烧录 ZIP 的 state、code、device-information 三个成员必须按工具固定顺序处理，不要手工合并。其它 PCB 使用相应完整公开板名和完整烧录文件，支持以本包清单为准。

TLSR8359：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\flash\windows\flash-telink.ps1 -Board tlsr8359_nowa154r -Firmware .\InkTag-tlsr8359_nowa154r-v0.2.0-full.bin -StockBackup .\backup -ToolPath C:\Tools\TLSR825xComFlasher.py -AcknowledgeExperimentalLayout
```

TLSR 需要 Python、pyserial 和对应 UART-to-SWS 工具及接线。Nebular 使用准确的 `tlsr8359_nebular420r` 文件。实验性布局要求显式确认，完整读回验证仍保留。

以上入口可显式用 `-SkipStockBackupCheck` 代替 `-StockBackup`。这不授予脚本修复或清除未知区域的权限。

## Linux 与 macOS

CLI 和 WebUI 可用于已安装 InkTag 的设备。完整烧录支持是另一项能力：当前包中没有被标为 Linux 已验证的厂商入口，不能把 PowerShell 或 Windows EXE 能启动当作 Linux 烧录支持。Linux 入口可用于查看支持情况和安装准备；不支持的组合会明确拒绝实际写入。macOS 不提供一键完整烧录。

```sh
bash flash/linux/flash.sh --help
```

Nordic 和 Dialog 的当前完整烧录必须来自对应独立目标。未提供且未验证的入口不应由通用 J-Link 命令替代。请查看[支持表](supported-devices.md)，不要按同芯片家族猜选。

## nRF52811 与 DA 的首次安装区别

nRF52811 的应用位于内部 Flash，字体资源位于外置 Flash。首装必须使用同版本的内部完整烧录与对应字体资源；普通 OTA 不补装字体。当前原生外置写入工具仍需准确版本与实板验证，不把源码仓库中仅通过准备检查的入口当作本包可用工具。

DA 的完整镜像包含启动、应用、固定 Recovery 和字体，不套用 Nordic 的分步字体流程。asel29 默认 SSD1619 128×296 BWR；asel42 默认 UC8176 400×300 BWR，而不是 SSD1683 换装屏。按准确板型选择文件，首装后再核对实际屏幕。

探测和备份也会访问硬件，可能暂停 MCU。不要在走时或功耗观察期间执行。备份可能是旧 InkTag 而非原厂固件，需保留来源和 SHA-256；跳过备份要求不等于可以跳过当前身份、电压或必要的 UICR 保存。

## 烧录后

等待写入和读回验证成功，按提示释放 BOOT 或重新上电。再检查普通应用的准确蓝牙身份、固件版本和屏幕配置。失败时先保留提示并检查供电、共地、线序和工具占用，不要自动全擦或无限重试。
