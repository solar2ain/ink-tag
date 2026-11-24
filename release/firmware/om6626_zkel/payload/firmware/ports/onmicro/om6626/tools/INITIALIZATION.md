# OM 初始化与保全烧录

双击 `flash.bat` 使用英文，双击 `flash-zh.bat` 使用中文。先选完整固件，再选主板。核对主板和固件后，先确认完整刷机。确认后才等待 USB J-Link 和 SWD，按 Esc 可取消。误选或出错时，按任意键关闭窗口。

选定 USB 探针后，工具在一个 J-Link 会话内持续探测 SWD。看到持续探测就绪提示后，如有需要再正常给主板上电。捕获成功后，工具确认主板仍保持暂停，才进入原有维护流程。

适用四个 `configs/*.toml` 主板配置，App 0.2.2、Common Recovery 1.0.1，epoch1/layout-v1。
本批为离线预检和工具验证；实机 Full、Recovery 更新及新 Pro 签名尚未执行。

在本目录运行下面的入口；所有路径均可含空格。无参数时会提示选择，空输入取消。

```bat
flash.bat -Firmware "D:\firmware\app-ota-components.json" -Config "configs\ZK_EL_OM21BA_V1.1.toml" -JLinkSerial 20090928
```

示例序列号必须替换为当前连接的准确探针。多个探针时不自动猜选。
`-DryRun` 只校验物料，不访问探针；退出码：0 成功，1 错误，2 用户取消。
操作日志中的 `WRITE-VERIFIED.json` 表示写入及独立回读匹配。

用户运行依赖为 Windows PowerShell 5.1、系统 .NET，以及既有合法授权的
J-Link/J-Flash 7.88j 和维护的 OM 设备包。此入口不运行 Python/pip/venv、
InkTag CLI、BLE 或 Find 服务。开发构建、签包和原生格式对照仍可使用 Python。
TOML 接受维护预设使用的语法，也可使用相同 schema 的 JSON；YAML 仍属于旧
`flash.ps1` 的 Python 路径，不能据此声称旧入口已去除 Python。
保留完整仓库目录层级及 `tools/jlink-tools.ps1`、`tools/powershell-cli.ps1`、
`tools/hardware/activation-preservation.ps1` 和本目录的 `internal/`、日志呈现、
只读项目与回调依赖。不要只复制单个 BAT。

基础版固件输入为 `package-onmicro-base-ota.py` 输出的组件 JSON、OTA ZIP 和
Common Recovery BIN；这是开发端工具，用户端只消费已编译文件。
Pro 沿用正常交付的签名 Full ZIP，保留原发行公钥、上下文及 App/Recovery 配对检查。
用户入口不接受替换公钥或导入授权 token。刷机时可为当前设备初始化合法地址。二维码、备注和链接可在连接 WebUI 后设置。

保全入口尝试读取旧资料，空白、损坏或读取失败的可选记录按缺失处理。
保留有效原地址和必要厂商参数。
ZK 保留从当前主板读出的有效签名授权，由 Pro 固件启动后核对 NFC UID。
刷入基础固件时可以保留授权记录，之后刷回 Pro 再验证。无有效记录时继续正常安装。
连接、芯片/实际容量、必需厂家数据、擦写或回读失败仍然报错，失败写入不自动重放。

写入跳过厂家 boot/CPFT 0x00000–0x03FFF、runtime MBR 0x80000–0x81FFF，
以及 ZK CONF 0xFF000–0xFFFFF。原生厂家加载器仍会更新 ROM CRC 启动元数据
0x1184–0x118D 和必要的 0x2000 备份；工具独立计算并回读验证这些厂家动作。
必须有可用厂家 ROM 启动格式（0x1182=0x60、0x11FE=55AA），全片擦空而丢失该
厂家格式的芯片不能由本工具伪造修复。
外置 Flash 不访问、不宣称已擦除；OM42B 外置图片保留。
Full 会清空普通设置、密码/Find、内部图片、用户字体及健康记录。写入后应完整掉电重启。
