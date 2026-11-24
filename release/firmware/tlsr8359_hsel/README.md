# TLSR8359 tools-only 明文刷机工具

工具以完整明文目录 `release/tools/tlsr8359_hsel/` 交付，不要求外层 ZIP。
本次工具版本为 `0.2.2.3`；源码 hash 和 buildId 分开记录。
本目录不包含、不下载、不默认选择固件，也不绑定某个固件版本。
probe、backup 无需固件；flash 使用你选择的兼容完整固件 ZIP 和主板配置，支持 CRC-only 基础固件或正常签名的 Pro 固件。三个 BAT 均可无参数启动；需要的文件未提供时弹窗选择，写入前仍需确认。
历史工具与固件组合包仅保留为历史证据，不再作为新推荐工具。

需要 Python 3.11+、Windows PowerShell、CH340 驱动及稳定的供电、SWS、共地接线。
运行以下命令安装独立锁定的 Python 依赖并离线自检：

```powershell
.\firmware\ports\telink\tlsr8359\tools\setup.bat
```

无需安装 InkTag CLI。默认网址、二维码、NFC 数据及包验证使用工具目录内固定的
`tools/hardware/inktag_programmer_formats/` 纯格式模块，不导入系统安装的 CLI。
版本 `0.2.0.4` 补齐 `0.2.0.3` 漏带的默认数据模块和二维码依赖；完整准备阶段已纳入
解包验证。缺少这些依赖会在访问串口前提示，不能只用 `-DryRun` 代替完整交付检查。
版本 `0.2.0.5` 进一步清除交付代码中残留的源码 CLI 搜索路径注入；来源记录保留，
默认数据、镜像字节和烧录行为不变。
版本 `0.2.0.6` 将固定格式模块纳入工具源码输入，源码入口与独立目录均直接使用它，
运行时也不读取源码中的 CLI 目录。升级 CLI 不会改变烧录格式；更新模块须显式从
固定源码提交重新生成，并复核来源和运行文件哈希。
版本 `0.2.0.7` 不再要求旧 Factory 选择标记完整才能烧录：有独立已安装身份／配置
记录定位时，旧标记可缺失或损坏，按选定配置重建。有效蓝牙地址及类型继续保留；
可选地址记录缺失／损坏时仅走既有 legacy／本机 Flash UID 初始化路径，并报告回退。
两个有效地址相冲突仍拒绝。原厂未知字节占用必要校准搬移目标、配置槽或原前缀保存
位置时，提示具体地址及无法安全保留的原因；不会把未知原厂数据直接擦成默认值。
此版本仅修改主机工具，App／Recovery 字节不变，没有新增实板验证结论。
版本 `0.2.0.8` 同步已接受的 HSEL3 内置配置：90°＋原生 X 镜像，与 WebUI／CLI
默认一致。旧 `0.2.0.7` 固定交付不改动；固件仍由用户显式选择，不含新固件或新签名。

版本 `0.2.1.0` 同步当前维护的工具闭包。HSEL4Q_04_20M_33 配置按屏幕 IC
拆成 `HSEL4Q_04_20M_33-SSD1683.toml`（SSD1683，BWR）和
`HSEL4Q_04_20M_33-JD79668.toml`（JD79668AA，BWRY）；旧 YW2427 和无后缀
入口退役。请按实际屏幕选择，配置中的 `screen.ic` 指定控制器，不再输入 BoardNote。

本版包含当前已验证维护脚本的 SWS 时钟处理：优先保留能验证同机 UID／JEDEC 的
已登记链路；只有符合严格停止态、身份及读回检查的 48MHz 捕获才使用既有 24MHz
烧录时钟路径。它不改变固件正常启动的 48MHz 默认，不把 48MHz／921600 说成可靠
完整烧录资格。不要自行套用其他主板的时钟或波特率。导出目录的离线检查不增加
板型、屏幕或现场烧录资格；既有工具快照及固件字节保持原样。

版本 `0.2.1.1` 修正已登记的 1 MiB HSEL4Q_04_20M_33 版型：完整烧录经本机
UID、匹配的 Flash JEDEC 和完整 1 MiB 清理计划验证后，启用已清空的高地址
512 KiB 图库。其他 512 KiB 板型保持原范围；禁止只写低区却启用未清理的高区。
400×300 BWR 通常可提供 20 个 Pro 图槽，Free 仍按原策略限制。App-only OTA
不会更改已安装的 Factory 基础区标志，已有设备需使用修复后的完整烧录工具。

三个入口及主板配置都在 `firmware/ports/telink/tlsr8359/tools/`。
以下端口和配置为示例，须按实际设备选择；工具不扫描 BLE。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\firmware\ports\telink\tlsr8359\tools\probe.ps1 -Chip tlsr8359 -Port COM13
powershell -NoProfile -ExecutionPolicy Bypass -File .\firmware\ports\telink\tlsr8359\tools\backup.ps1 -Chip tlsr8359 -Board YOUR_PCB -Port COM13
.\firmware\ports\telink\tlsr8359\tools\flash.bat -Config .\firmware\ports\telink\tlsr8359\tools\HSEL5_01_54M_00.toml -Firmware 'C:\Downloads\selected-app-full.zip' -Port COM13
```

每个入口支持 `-Help`，`-DryRun` 只做离线检查。完整烧录默认询问 y/N，
只保留同次连接目标原有、签名及本机 UID 绑定均有效的 Pro 原生记录和必要序号，
以及原有身份、蓝牙地址及类型、校准和必要启动数据。不重新签发，不用 BLE 补激活。
清除密码及 QR 控制凭据、Find 设备状态、用户设置／名称、字库、主 Flash 图槽、
轮播／交互状态和旧诊断历史；主机 Find／Apple 库和轨迹从不修改。

为保留 Pro，烧录前需额外读取当前 Settings 双页共 8 KiB，并在擦除前与
Factory、本机 UID 再次核对。只把原有效 token 写入新一代 Settings，旧页及
历史记录清空，不整页恢复。明确无记录或双页全 FF 的 Free／原厂目标正常保持 Free。
旧开发布局、未知非 FF 用户数据、坏签名、异机或不兼容授权不会单独阻止完整烧录；
无法恢复的旧授权将清除，设备初始化为 Free，后续 Pro 需要正常激活。既有 y/N 警告
事先说明这一影响，准备后、写入前显示实际保留／重建结果，不把未知内容说成已证明空。
只采用实际冷启动读取器选择的当前已提交页，不从旧页复活已删除的授权；不相关的损坏
旧页不影响仍有效的当前 Pro。已安装选择器标识的用户引脚／屏幕配置均按选定配置重建。
读取失败／短读、目标 UID 改变、必要工厂身份或校准不确定、包签名／配对／容量错误
仍在擦除前拒绝。不要使用旧备份或外来激活码代替当前板的捕获数据。捕获文件含私密
授权资料，只保存在本次本地证据目录，不公开。

TLSR 当前没有已资格的持久 crash／reset-history 或保留 RAM 故障邮箱后端。
工具省略旧 Settings 历史，并沿用最终整片复位请求；不乱清 SRAM 或未知寄存器，
不把复位请求当作已观察启动。独立 NFC EEPROM 不由烧录器擦除，不声称已清空。
仅有明确资格的 Nebular420 同片高地址图槽会清除；其他未资格高地址不访问。
首次刷机必须保留完整回读；接线稳定后的后续刷机可显式使用
`-SkipReadbackVerify`，结果为未验证。签名、主控/布局/容量、App/Recovery
内部配对、原生 Pro 权威及身份/校准检查始终保留；新 Settings 关键回读也不跳过。
不要借用另一块板的工厂数据。

长操作实时向终端和日志输出；阶段结束行应保留。不要以外层收集完整输出
的方式调用，从而隐藏实时进度。SWS 激活可能复位/暂停目标，并非纯观察。
本包不含 Python、驱动、SDK、编译器、签名工具或完整 BLE CLI。
Linux/macOS `.sh` 入口仅支持离线计划，未取得实际烧录资格。

无需安装依赖可运行 `python firmware/ports/telink/tlsr8359/tools/check.py --content-only`
核对解包文件；完整检查省略该参数。第三方许可见 `THIRD_PARTY_NOTICES.md`。
本包是内部验证交付，不表示全板型验证或公开发行批准。

维护规则：先在主库 `release/programmer-docs/tlsr8359/` 维护文档，审核后冻结版本快照并单向复制到
发行库；本目录不授权发布。MCU 固件字节未改变，不要求重编译或重签名。

版本 `0.2.1.2` 新增 `generate-bin.ps1` 和 `flash-bin.ps1`，原 `flash.ps1` 保留。离线生成需完整固件 ZIP 与配置；0.2.1 必须显式指定 MAC。直接 bin 写入默认不回读，可加 `-Verify`；可选 `-PrepareClock24` 复用停机时钟准备。完整工具和内部依赖需一起下载。仓库根目录可复制命令见[烧录说明](https://github.com/solar2ain/ink-tag/blob/main/docs/flash.md#tlsr离线生成-bin-和直接烧录)。

当前未发布的 `0.2.2.3` 工具支持 CRC-only Base Full 和签名 Pro Full，保留原 `flash.ps1`。BAT 从其所在目录加载 PowerShell 和依赖，请保留完整工具目录。

- `generate-bin.bat -Firmware <Full.zip> -Config <板型.toml> -Output <初始化.bin>` 完全离线生成，省略 MAC 需要 App 与 Recovery 同时包含自动地址能力；旧 0.2.1 固件仍须 `-Mac`。可显式指定唯一 MAC 及 `-AddressType`。
- `flash-bin.bat -Bin <初始化.bin> -Port COM13 -Yes` 原样写入 BIN，不读取旧 Factory/Settings/UID，不保留旧 Pro 或数据；默认不回读，可加 `-Verify`。`-PrepareClock24` 是显式的链路修复选项，使用已有只读 UID/JEDEC 和停机时钟准备，不能和默认路径混为一谈。
- 推荐入口：`flash.bat -Firmware <Full.zip> -Config <板型.toml> -Port COM13 -Yes` 复用 `flash.ps1 -AllowMissingUid`：连接读取必要 Factory、Settings、UID/JEDEC，原厂布局另读必要校准；只保留有效地址、校准及本机有效 Pro，重建普通设置并清除历史、密码、Find 和图库索引。Base 可保留经主机固定公钥校验的本机 token，保持休眠，不启用 Base 的 Pro 功能。

### 无参数启动与文件选择

BAT 均可双击，也可从其他目录调用。无参数运行结束后按任意键关闭窗口，带参调用直接返回退出码。

- `flash.bat` 显示英文指南，`flash-zh.bat` 显示中文指南。先确定完整固件。BAT 同目录只有一个 `*-complete-firmware.zip` 或 `*-full.zip` 时自动选用并校验，有多个时按编号选择，没有时打开文件选择窗口。再选择主板配置。核对校验结果和数据保留范围后，输入 `y` 才继续，直接回车取消。确认后输入 CH340 的实际串口（如 `COM13`）。直接烧录 BIN 或带参数执行也默认持续探测。确认后才开始探测，可在等待时接入设备或按 Ctrl+C 取消。探测通过后仍会检查设备身份和 Flash。
- `generate-bin.bat`：按编号选择同目录配置，弹窗选择完整固件 ZIP，再选择一个新的 BIN 保存位置；全程离线，不接设备。未指定 MAC 时沿用固件支持的首启自动地址初始化。
- `flash-bin.bat`：弹窗选择已生成的 BIN，再输入实际串口并确认。BIN 直写**不需要主板配置**，也不会保留原设备数据；基础固件需同时保留配套的同名 JSON。

交付包中的 `.toml` 配置直接放在 BAT 旁边；也支持同目录的 `.yaml`、`.yml`、`.json` 配置。不会从当前工作目录、`configs/` 或其他子目录自动寻找。找不到时，把对应配置放到 BAT 同目录后再运行。源码中的配置源文件仍在 `configs/`，可复制所需文件到 BAT 旁边，或使用显式 `-Config` 指定任意有效路径。

已有参数只补缺项，不会重新询问。关闭文件窗口、取消主板选择或串口输入，都会在访问硬件前退出；不会自动选择固件，不覆盖已有 BIN。`-Help` 和 `-DryRun` 不弹窗、不访问硬件。自动化可用完整参数及 `-NonInteractive` 禁止缺参引导，写入确认仍用原有 `-Yes`；`-Yes` 不跳过包校验、容量、身份或回读检查。

BDT 等裸 Flash 工具将 BIN 原样从地址 `0` 写入。所有板型均生成 512 KiB（`0x00000–0x7FFFF`），其中已有启动代码、App、Recovery、板型配置及新设置，不需要另写配置或激活。1 MiB Nebular420 的高地址 512 KiB 不包含在 BIN 内；图库容量配置保持不变，已有合法图槽可能保留，后续写入图槽时沿用现有逐扇区擦除流程。离线 BIN 不承担保留式 Full 的全图库清理语义。独立 NFC EEPROM/Flash UID OTP 不在 BIN 内。请核对 BDT 的实际擦除范围；只下载低区并不证明工具不会全片擦除。`flash-bin` 仍可校验和烧录先前生成的合法 1 MiB BIN。

未指定 MAC 时，自动初始化标记在 `0x7F10D`，legacy `0x7F000` 与 typed `0x7F160` 地址均保持 FF。首启复用 SDK 随机源生成合法 Static Random 地址，一次写入并持久保存；App/Recovery 共用此逻辑，重启地址稳定，断电后可恢复单向写入。它不依赖二维码。显式 MAC 同时写 legacy 六字节小端镜像及 typed 带类型/校验记录，标记清零；不能把固定地址 BIN 给多个设备通刷。

离线生成不读取或保留原厂个体校准、旧授权或用户数据，校准采用现有空值回退。新 Base 的 `flash-bin` 必须保留同名 JSON，绑定真实 Full manifest 版本、上下文和 BIN 哈希；JSON 不是发布者认证。签名 Pro 的 BIN-only 路径仍严格验签，失败不会降级成 Base。旧固件无自动地址能力时省略 MAC 是真实不兼容，会在访问硬件前拒绝。

保留式入口只在三次完整 UID 观察均不可用或不一致时，明确记录 UID 缺失并继续 Base，不伪造 UID，也不恢复 Pro。此时只能绑定当前端口、JEDEC 和实时捕获内容，不能宣称不可变同机身份验证。短读/传输失败、芯片或容量不符、关键未知校准冲突、实际写入/回读失败仍会停止；写入不重放，失败不自动启动 App。原 `flash.ps1` 默认继续严格要求有效 UID。

本批只有离线原生构建、故障注入及工具解包验证；不代表新增实板烧录、BDT 型号兼容或公开发布资格。旧版本固件及工具快照不变。
