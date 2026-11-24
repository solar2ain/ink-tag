# DA14585 初始化与保全烧录

需要 Windows、Python 3.12 或更新版本、SEGGER J-Link 7.88j，以及单独取得的已许可固定 SRAM helper。BAT 可从资源管理器、任意工作目录的 CMD 或 Windows PowerShell 5.1 启动，不修改执行策略。

- `flash.bat`：英文引导；`flash-zh.bat`：中文引导。二者复用同一实现。
- 双击时先显示接线与操作说明，检查入口旁、入口的 `firmware/`、工具根 `firmware/` 下的 `.bin/.img`。只有一份通过验证的完整 **DAV1 Full** 时显示文件名并自动采用，直接进入主板菜单；多份有效 Full 才列编号选择，没有有效 Full 则弹 Windows 文件选择框。单独 App／OTA 或损坏文件不会被自动采用；显式 `-Firmware` 优先，选定后仍验证。
- 随后从实际随附配置选择主板／屏幕，输入新日志目录和缺少的 helper 路径。主板编号无效或配置缺失会显示具体错误并退出，不静默选默认主板。
- 无参数引导持续等待明确的探针／目标未接入状态，无次数或总时间上限；显示状态，`Ctrl+C` 取消。Full 和主板校验后先说明完整刷机影响并确认，默认否；拒绝或直接回车不等待、不枚举、不探测设备。同意后才持续等待并执行原身份校验。多探针须明确指定 `-Serial`，身份不符或未知探测错误不会自动重试。
- 交互成功、取消或报错后都显示退出码并等按任意键关闭，缺 Python 也保留窗口。带参数调用、自动化及 `-DryRun` 不暂停；`-Yes` 仍要求显式必要参数，返回真实退出码。
- `generate-bin.bat` 询问缺少的 Full／配置／输出目录；`flash-bin.bat` 使用已有物料。`flash-launch.bat` 是两个语言入口共用的启动器，不是单独用户入口。普通选择空输入取消。两种烧录入口都默认持续探测，可按 Ctrl+C 取消。

单探针沿用原后端识别，多探针沿用精确序列号约束。只对已知、写入前、已清理的连接缺失重试；helper、J-Link文件或环境依赖缺失，以及探测身份、供电、擦写、ACK、回读错误立即停止，刷写错误绝不重放。工具包不附固件、厂商工具或受控 helper；引导改进不解除便携写入器原有资格限制。

### English quick start

Double-click `flash.bat` for English or `flash-zh.bat` for Chinese. The tutorial explains SWDIO/SWCLK/GND wiring and board power. A single adjacent valid DAV1 Full is displayed and used automatically, proceeding directly to the board menu. Multiple valid Full files require selection; no valid Full opens a Windows file picker. Standalone App/OTA and corrupt files are excluded. Explicit `-Firmware` takes precedence and remains validated. Then select a bundled board configuration, output directory and external licensed helper.

Both flash.bat and flash-bin.bat wait for a missing programmer or target by default. Press Ctrl+C to cancel. After validating Full and board inputs, the existing default-No confirmation explains the Full installation impact. Declining or pressing Enter performs no waiting, enumeration or target access; only consent enables discovery and the original identity checks. Dependency, ambiguous identity and programming failures stop without replay. The window waits for a close key after success, cancellation or error. Supplied-argument automation and DryRun never pause and preserve the exit code. This tools-only bundle includes no firmware/vendor/helper binary and does not qualify a portable physical writer.

## 离线生成

```powershell
.\generate-bin.bat -Firmware 'D:\Firmware\full.bin' -Config '.\configs\asel29.toml' -Output 'D:\Firmware\asel29-init'
```

`-Firmware` 必须是完整的 DAV1 Full，不能用 App.bin 替代。配置还可选 `asel42.toml` 或 `asel42-ssd1683.toml`。输出：

| 文件 | 外置 SPI Flash 偏移 | 大小 |
| --- | ---: | ---: |
| `spi-000000.bin` | `0x00000` | `0x39000` |
| `spi-03b000.bin` | `0x3B000` | `0x5000` |
| `addresses.json` | 地址、校验值、跳过范围 | — |
| `initialization-template.bin` | 配对与校验用模板 | 256 KiB |
| `config.toml` | 已确认配置 | — |

两段包含 loader、App、Recovery、默认资源、屏幕／引脚和状态。`0x39000–0x3AFFF` 两页厂区必须跳过；模板中的占位字节不表示已保全。窗口软件必须支持该设备的外置 SPI Flash 和跳过区域，不能整片擦除后写模板，也不能把这些偏移当作 MCU 内存地址。

通用物料不带他机 MAC、标签、授权或旧用户记录。已有有效原厂地址优先；地址缺失时，本批 App／Recovery 使用 SDK 随机地址生成器，并在可写 product 页追加同一份 static random 地址记录。重启及两种角色共用。旧 Recovery 不会因仅安装 App OTA 获得此逻辑。

## 检查与烧录

`flash.bat` 是正常／推荐烧录入口：先尝试保全当前设备可用信息，缺失使用默认初始化。`flash-bin.bat` 用于已经生成的初始化物料。

```powershell
.\flash-bin.bat -Input 'D:\Firmware\asel29-init' -Output 'D:\Logs\da-check' -DryRun
.\flash-bin.bat -Input 'D:\Firmware\asel29-init' -Output 'D:\Logs\da-install' -Helper 'D:\Licensed\da-helper.bin' -Serial 123456789
.\flash.bat -Firmware 'D:\Firmware\full.bin' -Config '.\configs\asel29.toml' -Output 'D:\Logs\da-preserve' -Helper 'D:\Licensed\da-helper.bin' -Serial 123456789
```

DryRun 仅检查文件、配置及配对，不连接设备。实际写入需要合法取得的固定 SRAM helper（2832 字节，SHA256 `0f183172ce2d0ce86bf32c27dde76d2d77ceb0ce1a4f974dea1489ba6395dd7c`）和 SEGGER J-Link 7.88j；厂商文件按 `vendor-dependencies.json` 核对。helper 受现有再分发限制，本源码和工具均不随附，也没有公开下载入口。缺少它时明确失败，不显示写入成功。本批没有新增设备 Full／Recovery 安装实测，host 验证不等于实板资格。

两个写入入口均先校验物料并确认完整刷机影响（或接受显式 `-Yes`），同意后才复用保护式后端连接并确认芯片、探针、Flash 和电压，尝试读取可保留资料，再准备当前设备物料。原厂两页和 OTP 不写。有效地址保留；旧资料缺失、空白、损坏或不可读取时按无记录处理，使用默认设置。DA 不适用 Pro 授权，不导入其他平台激活实现。普通设置、时间、图片及用户记录按 Full 范围清理；超出前 256 KiB 的 Flash 保留且不宣称已清除。

擦写、ACK、连接和已写区域回读的真实失败仍会停止，未知结果不重放。无厂区快照时，结果会明确厂区仅跳过、未比较；不能声称完整厂区回读匹配。成功结果仍需核对实际回读范围与唯一启动记录。

默认要求确认，`-Yes` 可用于已明确清理范围的自动执行。`-JLinkExe` 可指定匹配的 J-Link.exe。`-Help` 显示参数。所有日志保存在明确指定的新目录，不自动寻找或覆盖旧物料。不依赖 InkTag CLI、BLE、Find 或运行中的 WebUI。签名旧包的 host 校验另需 cryptography；本源码默认 CRC-only 未签名包只用 Python 标准库。

二维码、备注和链接不是安装前置条件。连接后通过共享 WebUI／CLI 已有允许的配置功能处理；本端未支持的自定义二维码、字体等能力不会由烧录入口开启。
