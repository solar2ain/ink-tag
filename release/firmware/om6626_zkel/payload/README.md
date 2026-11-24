# OM Windows 原生 tools-only 本地候选

双击 `flash.bat` 使用英文，双击 `flash-zh.bat` 使用中文。可在工具包 `payload` 同级放置 Full ZIP。找到一份时直接采用，多份时列出选项，没有时打开文件选择框。核对主板后先确认完整刷机，默认取消。确认后才开始探测 USB J-Link 和 SWD。带参数执行时也会持续探测，按 Esc 可取消。

工具版本 **0.2.0.16**，与固件版本独立。此目录不含固件、SDK、编译器、便携 Python、
CLI 或 BLE/Find 服务。尚未公开发布，也不代表实机烧录验收。ZK/HS 分开打包，
本批两目录合计四个配置；同主控的两种固件家族不可互换。

完整保留 `payload/`、其旁边的 `records/` 和 `delivery.json`。先进入 `payload/`：

```bat
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\setup.ps1
tools\flash.bat -Firmware "D:\firmware\app-full.zip" -Config "tools\configs\PCB.toml" -JLinkSerial 20090928
tools\probe.bat -Chip om6626 -JLinkSerial 20090928
tools\backup.bat -Chip om6626 -Board PCB -OutputDir "D:\backups" -JLinkSerial 20090928
```

将 PCB 文件名和探针序列号替换为本机准确值；可用配置在 `tools/configs/`。
刷机 BAT 无参数时提示选择，空输入取消。有参数可自动执行；0 成功、
1 错误、2 取消。`-Help` 显示帮助，`-DryRun` 只校验，不打开探针。
运行入口先核完整工具清单，缺项/额外文件/篡改/链接均拒绝；请将备份输出
放在候选目录外，不向只读工具闭包添加日志或数据。

用户依赖为系统 Windows PowerShell5.1/.NET、合法授权的 J-Link/J-Flash7.88j
及固定 OM 设备包。SEGGER/厂商文件不随附、不自动下载，也不向源码库/SDK回退。
安装后可用 `tools/check.ps1 -NativeInstalled` 对照 `records/dependencies.json`
核文件 SHA；它不连接硬件。实际写入前仍由原维护后端核版本、芯片/容量和上下文。
开发端可以用 Python 包装工具或编译固件，用户运行这套工具不需要 Python。

基础版使用外部已编译组件清单及其 OTA/Recovery 文件；高级版使用外部正常签名 Full ZIP。
固件损坏、错家族、错配对、签名失败均在设备
访问前拒绝。TOML/JSON 支持本轮入口，旧 YAML/旧 Python PS1 不在此目录内。

`flash.bat` 尝试保留有效地址和必要厂商参数。
ZK 保留从当前主板读出的有效签名授权，由 Pro 固件启动后核对 NFC UID。
Full 按最终蓝牙地址生成带默认蓝牙名的设备二维码，并为 Pro 初始化同一条 NFC 链接。
OM42B 的外置图片保留。
厂家范围、地址及 Full 清除范围见 [ADDRESSES.md](ADDRESSES.md)。

写入、独立回读和屏幕验收分别记录。详细实现说明保留在 runtime 平台目录的 `INITIALIZATION.md`/英文版。
