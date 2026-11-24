# GR 初始化 BIN 与保全刷机 / Clean initialization and preservation

在所选主板的 `gr5331/tools` 或 `gr551x/tools` 目录运行对应的 BAT。
正常烧录推荐使用 `flash.bat`，先尝试保全可用原信息；缺失时采用默认初始化。
用户端需要 Windows PowerShell 5.1/.NET；生成物料不需要 Python、SDK 或设备。
实际读写沿用 Goodix GProgrammer 的原生 console、DLL/loader 与 SEGGER J-Link
7.88j。请按厂商许可单独安装，并用 `-ConsoleExe` / `-JLinkExe` 指定路径；工具
不下载固件、不捆绑私有 SDK，也不调用 InkTag CLI、BLE 或 Find 服务。
旧 Python 工具保留；新原生入口支持维护的 TOML 配置和等价 JSON，YAML 继续使用旧入口。

```bat
generate-bin.bat -Firmware "D:\Firmware\app-full.zip" -Config "configs\ZK_EL_GR42V_V1.2_w_NFC.toml" -Output "D:\Firmware\new materials"
flash-bin.bat -Materials "D:\Firmware\new materials" -ConsoleExe "D:\Goodix\GR5xxx_console.exe"
flash.bat -Firmware "D:\Firmware\app-full.zip" -Config "configs\ZK_EL_GR42V_V1.2_w_NFC.toml" -ConsoleExe "D:\Goodix\GR5xxx_console.exe"
```

双击 `flash-zh.bat` 打开中文向导，双击 `flash.bat` 打开英文向导。工具旁只有一份通过验证的
Full ZIP 时，向导会显示文件名并直接进入主板菜单。有多份时按编号选择，没有时打开文件选择框。
校验固件和主板配置后，向导会显示完整刷机的影响。输入 `FLASH` 确认后才会枚举探针、等待设备并核对实际芯片。
按 Enter 或输入否会直接取消。等待设备时可按 Esc 取消。如果设备还没上电，等候提示出现后再上电。
`flash.bat`、`flash-zh.bat` 和 `flash-bin.bat` 在每个等待窗口内连续检测 SWD。出现错误或流程完成后，按任意键关闭向导。
缺少厂商工具路径时按提示输入。检测到多个探针且未提供 `-Snr` 时选择准确探针。
核对设备后会显示实际保留项和清理范围。`-Yes` 自动化调用仍须显式选择多个探针中的准确序列号。
`-Help` 显示完整参数。
`generate-bin.bat` 和 `flash-bin.bat` 仅提示其各自缺少的输入。
带参数运行时保留原有提示和退出码，结束后自动返回。传入完整固件及配置时可用
`-DryRun` 进行离线验证。
自动调用可显式传 `-Yes`。退出码 0 表示该入口
所执行的流程完成，非零表示失败，具体阶段保存在 `-Evidence` 指定的新目录。

`generate-bin` 仅接收完整的外部 Full ZIP，检查实际 App、Common Recovery、板型、
布局、CRC 和适用的发行签名，再生成 `mcu.bin`（已包含 GUI 字体）、`flash-map.json`
及绑定的组件/配置副本。App-only BIN/OTA 不是完整初始化镜像。旧固件如果没有
首次启动地址持久化能力，会在访问设备前拒绝。版本保持 App 0.2.2、Recovery 1.0.1、
epoch 1 / layout-v1，基础版与 Pro 使用同一 Common Recovery。

| 实际芯片 / 布局 | MCU BIN 起始地址 | 字节数 | NVDS 相对偏移 / 字节数 |
| --- | --- | --- | --- |
| GR5331 | `0x00200000` | 524288 | `0x7E000` / 8192 |
| GR5513 标准 | `0x01000000` | 524288 | `0x7C000` / 16384 |
| GR5515 标准 | `0x01000000` | 1048576 | `0xFC000` / 16384 |
| GR5513 internal | `0x01000000` | 524288 | `0x7E000` / 8192 |

GUI 字体已按地址表包含在 `mcu.bin` 中，它是唯一的 MCU 烧录项。新物料目录不再另放
`gui-font.bin`；校验使用绑定组件包中的资源（Pro 使用工具内既有默认资源），并保留 GUI
哈希和完整 MCU 内容比对。不要把 GUI 再作为另一个烧录项。
ROM 启动程序、OTP/eFuse 和 Flash 安全寄存器不属于此 MCU Flash 写入计划；不执行
全芯片 erase-all 命令。MCU Flash 中可改写的启动描述、App、Recovery、资源、
日志和用户区按完整初始化范围重建。厂家不可变的芯片身份/校准来自上述独立厂家区；
可完整解码的旧 SDK NVDS 普通覆盖项在保全流程中保留，缺失或损坏时使用 SDK 默认值。
GR5513 独立 SPI 图片 Flash 沿用已有 Full 的例外，不擦除、不声称图片物理内容已清除；
MCU 内图片索引会清空。不要把地址窗口或链接区域当作物理容量，工具仍须实际识别芯片。

通用 BIN 不包含另一台设备的 MAC、UID、Pro 或用户资料，也不要求先备份旧 Flash。
首次启动优先使用有效的实际 SDK 地址；没有可用地址时使用既有 GR UID 静态地址回退，
保存为类型明确的 NVDS 记录并回读。App 和 Recovery 共用该记录，不在重启时换地址。
保全时优先有效的带类型记录；SDK 的 C001 公有地址项可以缺失或与 Static Random 不同。
类型记录无效时再使用可用 SDK 地址；不能容纳的可选 SDK 覆盖项会整体回默认值并报告。
默认基础显示可以启动；个性二维码、备注、链接等连接 WebUI 后再配置。

正常烧录使用 `flash.bat`：
先识别实际芯片/容量和准确探针，再尝试读取旧资料。有效地址、可解码的 SDK 普通项和
经过固定公共信任根、本次不可变 UID、产品/feature/epoch 校验的同机 Pro 原记录会保留。
刷入基础版也保留合格原授权记录，但基础版忽略它、不提供 Pro 能力；Pro 候选还须绑定
自身激活公钥。工具不接受外部 token，不允许配置替换公钥，不签发或重签授权。
UID/记录缺失、不支持、读取失败或校验失败会逐项告知，不能保全的可选记录按无记录处理。
普通设置、密码、Find、图片索引、崩溃/健康记录会重置。
`generate-bin.bat` 生成通用初始化物料；`flash-bin.bat` 写入该物料并按默认值初始化 MCU 用户资料。

连接失败、芯片/容量不符、损坏输入和真实擦写/完整回读失败仍会停止，不自动重放写入。
写入完成后必须逐字节回读匹配，随后沿用已有 watchdog 状态清理和一次原生复位。
生成成功、写入完成、回读匹配、实际显示/蓝牙功能分别记录；主机假设备测试不代表
已经完成本批实物 Full 或低功耗测试。

## English

Run the BAT entries in the exact board's `gr5331/tools` or `gr551x/tools`
directory. The native path uses Windows PowerShell 5.1/.NET, with no Python,
pip, virtual environment or bundled Python. Offline generation needs no device
or SDK. Hardware operations reuse the installed native Goodix GProgrammer
console/DLLs/loaders and SEGGER J-Link 7.88j, supplied separately under their
licenses. Pass `-ConsoleExe`, optional `-JLinkExe`, and an exact `-Snr` when
multiple probes are attached. Existing Python backends remain available;
native entries accept maintained TOML and equivalent JSON, while YAML remains
on the existing Python entry. No CLI, BLE or Find service is started.

The examples above accept paths containing spaces. Double-click `flash.bat`
for the English guide or `flash-zh.bat` for Chinese. A single verified local Full
ZIP is used automatically and the board menu follows. The guide lists multiple
verified packages or opens a Windows file picker if none qualify. After validating
the Full ZIP and board, the guide explains the impact and asks you to type `FLASH`.
Press Enter or answer No to cancel before device access. After confirmation, the
guide waits for the target and verifies its identity. If the board is powered
off, power it on after the wait prompt appears. `flash.bat`, `flash-zh.bat`, and
`flash-bin.bat` poll SWD continuously during each detection window. Press Esc to
cancel the wait. The guide waits for a key after success or failure.
Calls with arguments return normally with their exit code. Use `-DryRun`
with firmware and configuration already selected for offline validation.
Multiple probes require a choice unless `-Snr` is supplied. Actual retained and
cleared data are shown after the device check. `generate-bin.bat`
and `flash-bin.bat` ask only for their own missing inputs. `-Help` lists options.
Writes require typing `FLASH`
or explicitly supplying `-Yes`. Exit code 0 means the requested workflow
completed; nonzero means failure. Use a new `-Evidence` directory for each run.

`generate-bin` validates an explicit external Full ZIP and produces the complete
MCU BIN (including GUI), address map and bound component/config copies. An App BIN
or App-only OTA is insufficient. Both actual App and Common Recovery must
include first-boot address persistence. App 0.2.2, Recovery 1.0.1, epoch1 and
layout-v1 remain unchanged; Base and Pro share Common Recovery. The address
table above gives exact internal Flash extents, not inferred physical capacity.

The generic image contains no other device's identity, entitlement or user
data. First boot prefers the usable SDK address, otherwise the existing GR
UID-based static-address fallback, then persists and reads back the typed NVDS
record shared by App and Recovery. Preservation prefers the valid typed record;
the SDK C001 public override may be absent or different for static random.
An invalid typed record falls back to a usable SDK address. Optional SDK
overrides that cannot fit initialize defaults as a group, with a warning.
Personalized QR/link data can be configured
through WebUI after connection; it is not a flashing prerequisite.

Use `flash.bat` for normal programming. It first attempts to
retain a usable address, fully decoded optional SDK overrides and the original
same-unit Pro record verified with the fixed public root, fresh immutable UID
and product/feature/epoch policy. Base retains a valid original authorization
as data and ignores it at runtime; Pro also checks its candidate App's activation
key binding. There is no external-token input, configurable trust root or issuer.
`generate-bin.bat` prepares generic initialization materials; `flash-bin.bat`
writes those materials and initializes MCU user data using defaults.
Missing, unsupported, unreadable or invalid optional data initializes defaults;
failure to prove same-unit entitlement drops that record instead of refusing
Base installation. Settings, password, Find, image index and crash/health state
are reset.

The MCU write plan excludes ROM, OTP/eFuse, Flash security registers and external
SPI image Flash. It rebuilds mutable boot descriptors and the complete ordinary
MCU Flash range without an erase-all command. Immutable factory identity/trim
remains in its separate manufacturer space; readable optional native overrides
are retained, otherwise the existing SDK defaults apply. The existing external
image-Flash exception is explicit, never reported as physically cleared.

Connection, exact-chip/capacity, input integrity, write and complete readback
failures remain fatal. No failed or unknown write is replayed. Successful
readback precedes the existing watchdog-cause cleanup and one native reset.
Generation, writing, matching readback and actual display/BLE qualification are
distinct facts; fake-device host tests do not qualify a new physical Full run.

New materials contain no separate top-level `gui-font.bin`: GUI is already
embedded in `mcu.bin`. Verification uses the bound component resource (or the
existing Pro default template), retains its hash and compares the complete MCU
bytes. Historical materials retain their existing GUI binding when read.
