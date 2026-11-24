# nRF52811 初始化与保全式烧录

这些入口使用 Windows PowerShell 5.1、Windows .NET Framework 和现有 J-Link 后端，**不安装或捆绑 Python，不使用 InkTag CLI、蓝牙或 Find 服务**。生成物料不接设备，也不需要安装 J-Link；实际写入需要 SEGGER J-Link 7.88j、USB 驱动及 SWD 接线。J-Link 软件按 SEGGER 许可自行安装，工具包不附带厂商 DLL 或私有 SDK。

适用主板为提供配置的 EL022HWRL / CAL_VER06，实际芯片必须是 nRF52811、192 KiB Flash / 24 KiB RAM，并装有该板型支持的 512 KiB 外置 Flash。配置文件选择原屏或已经适配的换屏，不能凭同主控推断其他主板兼容。

双击 `flash.bat` 使用英文，或双击 `flash-zh.bat` 使用中文；从其他当前目录启动也可。启动教程会说明 J-Link 7.88j、SWDIO / SWCLK / GND 接线、主板供电、初始化范围及取消方法。工具旁、相邻 `firmware` 或 `full` 目录里只有一份有效 Full ZIP 时自动采用，有多份时才询问选择；没有可用文件时弹出 Windows 文件选择框。OTA 包和单独 App 不能作为 Full。固件验证通过后，才从实际随附的 `native-configs` 中选择主板丝印及屏幕配置。

固件和主板校验通过后，先说明完整刷机影响并询问是否继续，默认否。回答否或直接回车会退出，不枚举探针、不访问串口或目标。同意后才持续等待 USB 探针和已供电的 SWD 目标，没有等待次数或总时限；状态会持续显示，按 Q 或 Esc 可取消。写入前仍检查目标芯片、容量及既有身份和安全条件。菜单留空取消；编号无效、依赖缺失、校验或烧录失败都会显示原因，按任意键关闭。完整刷机会重建普通设置并使旧图片失效。只有写入前探测可重试，写入失败不会自动重放。

在 CMD 中也可运行下列带参数命令。已有参数不会重复询问；`-Yes` 和 `-DryRun` 沿用原行为，带参数的 BAT 结束时不等待按键。自动化请提供完整路径并加 `-Yes`；该路径遇到缺探针或目标连接失败会立即报错。单个 J-Link 自动选用，多探针按既有后端列出的序列号用 `-Snr` 指定。`generate-hex.bat` 询问生成所需配置、Full 和新输出目录；`flash-hex.bat` 询问已有物料目录。

正常、推荐的完整烧录入口是 `flash.bat`。它先尝试保全可用原信息，缺失时按默认初始化；完整烧录会重建普通设置并使旧图片失效，保全范围见下文。

```bat
flash.bat -Firmware "D:\Firmware\app-full.zip" -Config ".\native-configs\EL022HWRL.toml" -Yes
```

需要先离线生成通用 HEX 物料时，使用以下两个入口：

```bat
generate-hex.bat -Firmware "D:\Firmware\app-full.zip" -Config ".\native-configs\EL022HWRL.toml" -Output "D:\Firmware\new initialization"
flash-hex.bat -Materials "D:\Firmware\new initialization" -Yes
```

`-Firmware` 是外部提供的完整 Full ZIP，包含匹配的 App、Recovery、SoftDevice / MBR 和 GUI。不要传入 OTA ZIP 或单独的 App.bin。工具本身不内置或下载固件。`-Help` 查看参数；`-DryRun` 只校验文件、不打开探针。多探针时用 `-Snr SERIAL`，也可以用 `-JLinkExe "C:\...\JLink.exe"` 指定安装位置。

## 生成的物料

| 文件 | 地址空间 | 起始地址 | 写入字节数 |
| --- | --- | --- | --- |
| `internal.hex` | MCU 内部 Flash | `0x00000000` | 196608 B |
| `settings.hex` | 板上外置 SPI Flash | `0x00000000` | 8192 B |
| `gui-assets.hex` | 板上外置 SPI Flash | `0x00002000` | 12288 B |

`addresses.json` 记录地址、范围和文件 SHA-256。`source-full.zip` 与 `board.toml` 保留生成来源，BAT 写入前会重新校验并对照生成字节。请将整个物料目录一起保存。

内部 HEX 包含 MBR / S112、App、Recovery 和两页重新初始化的 SDK 启动设置。板型引脚、屏参及首次显示记录在外置默认设置中。通用物料不含旧设备 UID、MAC、设备二维码、备注、密码、健康记录或激活数据。原厂 FICR 中的终身地址由现有 Nordic SoftDevice 在启动时读取，App 和 Recovery 沿用相同地址；不需要在生成物料之前读取设备，也不写入测试 MAC。二维码和备注可在连接 WebUI 后通过现有设置入口导入。

内部与外置 Flash 是不同地址空间。只写 `internal.hex` 不是完整安装；普通窗口工具若不能驱动该板外置 SPI Flash，请使用 BAT 路径。不要选择全芯片 Recover / Unlock 或覆盖 FICR / UICR。

## 写入和保全范围

两种写入都会初始化普通用户设置，清除旧 QR / 备注及健康、崩溃和重启记录，并使旧图片索引失效。外置图片像素只按原有例外清除四字节图头，**不是安全擦除整幅像素**。这不备份旧用户数据。

`flash.bat` 会先尝试读取原 UID、地址和 UICR。可选旧资料空白、损坏或读取不到时会逐项记录，仍使用默认初始化；制造地址所在的 FICR、UICR 和外置 `0x5000..0x9FFF` 保留区都通过写入范围跳过。工具不把另一台设备的制造信息填入通用 HEX。nRF52811 是独立基础版，不读取、复制或生成 Pro 激活记录。

芯片和真实容量不匹配、连接失败、电压超出既有 J-Link 2.7–3.6 V 范围、原生写入结果未知或回读不符都会失败。失败不会自动重放写入，也不会通过额外重启掩盖失败。只有内部整片、外置设置 / GUI 和图头回读全部符合后，才清理易失状态并执行最后一次重启。日志分开记录生成成功、已确认写入和回读匹配；这不等于应用功能或屏幕目视验收。

`-Output` 可指定一个新的操作日志目录。J-Link 7.88j 要求该目录路径较短、使用 ASCII 字符；空格支持。目录已存在时拒绝覆盖，以保留失败证据。

源码开发中的 `make full` / `build.ps1` 仍使用 Python、WSL 和 Nordic / Arm 编译工具链。这是研发构建依赖，与以上三个用户烧录入口分开。
