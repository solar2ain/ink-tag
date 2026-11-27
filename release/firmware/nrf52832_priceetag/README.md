# nRF52832 PriceETag 工具

双击 `flash.bat` 使用英文，双击 `flash-zh.bat` 使用中文。只找到一份可用 Full ZIP 时直接采用，多份时列出选项，没有时弹文件选择框。接着选择主板并确认完整刷机。确认后才开始探测设备。

完整固件在 [full/](full/)，板型配置见[安装说明](INSTALL.md)。BAT 使用
Windows x64 自带的 PowerShell 5.1/.NET。连接硬件时另行安装原版 SEGGER
J-Link 7.88j；程序检查 EXE/DLL 的固定哈希。

发布包在 flash-images/ 提供按板型命名的 HEX，BAT 复用 full/ 中的完整包复算校验。HEX 约 1.47 MB，是覆盖 512 KiB 内部 Flash 的 Intel HEX 文本。若自行运行 generate-hex.bat，其输出目录会包含用于校验的 source-full.zip，方便独立携带。

双击 BAT 可输入必要路径，空输入取消；命令行也接受完整参数。中文和含空格
路径需要引号。完成返回 0，拒绝、写入或读回失败返回非零，不自动重刷。

正常烧录推荐使用 `flash.bat`：先尝试保留可用的同机原信息，缺失时使用默认值。
进入主板菜单后，按丝印 PriceETag V1.7 和配置名选择编号。确认前核对板型、
固件以及保留和清理范围，再确认写入。留空取消。已提供 `-Config` 或 `-Firmware`
时跳过对应提问。自动化使用完整参数与 `-Yes`，缺必要参数会报错。
探针沿用既有 J-Link 选择和序列号固定流程，按需用 `-JLinkSerial` 明确选择。

离线生成初始化材料，不连接旧设备：

```bat
generate-hex.bat -Firmware "D:\固件\app-full.zip" -Config "configs\PriceEtag_V1.7.toml" -Output "D:\初始化 材料"
```

交互模式会自动使用唯一匹配的 Full ZIP，离线生成时可通过 `-Firmware` 指定。工具验证 App/Recovery 配对及实际原生布局；
Base 使用 CRC 完整性校验，Pro 使用固定发行公钥验证签名。存在但无效的签名
不会按无签名处理。配置支持 TOML/JSON；本入口不支持 YAML。

输出包括覆盖 512 KiB 主 Flash 的 `main-flash.hex`、地址说明、文件哈希和经过
清理的固件依赖包。HEX 从内部 Flash 地址 0 开始，不包含 FICR/UICR 或外部 Flash。
材料不带旧设备 MAC、名称、二维码、Pro 或用户数据。
启动地址由芯片 FICR/SDK 提供，二维码之后从 WebUI 导入。

安装精确生成的干净材料：

```bat
flash-hex.bat -ImageDirectory "D:\初始化 材料" -JLinkSerial 20090928
```

此操作写入初始化 HEX，会清除旧激活和普通用户数据。正常烧录使用推荐入口：

```bat
flash.bat -Firmware "D:\固件\app-full.zip" -Config "configs\PriceEtag_V1.7.toml" -JLinkSerial 20090928
```

保留式入口先读芯片身份及必要旧信息，保留工厂/校准/地址区域与经公钥和
芯片 UID 验证的同机 Pro。目标是 Base 时也保留记录，Base 不因此获得 Pro
功能。旧信息缺失、空白、损坏、不可读或激活属于异机时按无旧信息初始化，
不阻止基础安装。普通设置、密码、Find 配置、图片、字体、二维码和健康记录
不保留。连接、芯片/容量、擦写、读回或清理失败仍然报错。

`generate-hex.bat` 无参数时按固件、主板配置和输出目录依次询问，仍只离线
生成材料；`flash-hex.bat` 只询问生成材料目录，使用其中已绑定的配置。

两种安装都在确认后写入一次，并验证完整内部 Flash、FICR/UICR，再清理并
复位。`-DryRun` 仅离线验证和打印计划，不访问设备；`-Yes` 可明确跳过交互
确认。可用 `-JLinkExe` 指定受固定哈希约束的原版程序路径。
操作记录保存在 `%LOCALAPPDATA%\InkTag\programmer\nrf52832\` 的独立目录，
避免 PowerShell 5.1 的深路径限制；失败记录也会保留。

本批 BAT 已做离线和模拟设备验证，完整硬件刷机资格独立记录；不能用
DryRun 或一次 App OTA 代替完整刷机验证。旧 probe/backup、pyOCD 和 YAML
流程仍由原有 Python 工具包提供，不属于这份无 Python 目录。
