# nRF52832 Windows 离线工具包

中文 | [English](README.en.md)

工具和固件分开交付。此 tools-only 包包含 probe、backup、flash、离线自检及固定版本依赖，
不包含、下载、搜索或默认选择固件。工具版本独立于固件版本。
当前 Windows 包的 HW56 完整写入仍处于资格门内，flash 只开放离线 DryRun；
probe/backup 的真实操作需要准确设备权限。本包暂未获公开发行资格。

## 离线准备

将完整工具目录复制到普通可写目录，支持中文和空格。使用 Windows x64 的 PowerShell 5.1 或 7：

```powershell
./tools/setup.ps1
./tools/check.ps1 -JLinkExe 'C:/Program Files/SEGGER/JLink_V788j/JLink.exe'
```

setup 只解压 dependencies/ 内 CPython 3.13.16 和依赖，不安装到系统、不联网。
check 仅读文件和核对依赖，不启动探针、不访问串口或蓝牙。
J-Link 不随包分发；请从 [SEGGER 官方](https://www.segger.com/downloads/jlink/)取得
完整 7.88j 安装包并遵守其授权。驱动安装可能需要管理员权限。
该软件限原装 SEGGER 或授权 OEM 探针使用。

## 三个入口

probe 和 backup 不需要固件，先查看不访问设备的计划：

```powershell
./tools/probe.ps1 -Chip nrf52832 -DryRun
./tools/backup.ps1 -Chip nrf52832 -Board 'PriceETag V1.7' -DryRun
```

在取得准确设备权限并通过依赖检查后，去掉 DryRun 才执行对应 SWD 操作。
Board 是操作者填写的完整 PCB 标签，不用于推断芯片。
真实备份包含两次独立 Flash/FICR/UICR 读取及匹配校验；失败捕获保留，不报成功。

flash 必须显式指定你另外持有的原生签名 Full 包：

```powershell
./tools/flash.ps1 -Firmware 'D:/firmware/app-full.zip' -Config ./tools/configs/PriceEtag_V1.7.toml -DryRun
```

缺少 Firmware 会在硬件访问前拒绝；包内清单不能代替该参数。
DryRun 验证原生 App 签名、主控、布局、容量、配置及固件内部 App/Recovery 配对。
去掉 DryRun 当前仍拒绝完整写入，不得以检查通过代替 HW56 实板资格。
Full 清除密码、Find、用户字体、内部图片、设置及崩溃／健康／复位历史，默认 y/N。
只保留同次准确设备读取并通过公共验签及真实芯片 UID 绑定校验的完整原生 Pro；
双物理页全空或当前日志没有授权记录时保持 Free；已完整读回的原厂、未知旧布局、
损坏或异机旧授权告警后按默认值初始化为 Free，不恢复无效授权。
不接受旧备份或手填授权，不备份整片 Flash；额外读取 8 KiB 原生日志用于判定授权。
保留 FICR/UICR 身份、蓝牙地址及类型、校准；独立外置 Flash 不访问、不声称已清理。
全部 Flash 算法及回读结束后，CPU 保持停止，仅直接 SWD 清理实际 64 KiB RAM 和
RESETREAS，再回读 RAM、身份及两份启动记录，通过后仅执行一次原生复位。
公共验签依赖缺失、读写结果未知或回读不符均拒绝后续写入／复位，不自动重放。
Recovery 目前只有容器成员完整性，没有独立签名。

RetryDetect 默认关闭，仅允许显式重试首次检测，不重放未知写入或失败回读。
依赖见 tools/dependencies.lock.json；工具不会自动下载固件或运行 BLE 操作。
本包的探针依赖闭包是 J-Link；源码中的 DAPLink/Linux/macOS 路径未因此获得资格。
没有厂商 SDK、编译器、Git、完整 CLI 或 BLE 运行时依赖。

## 文档与归档

说明先在主库 release/platform-tools/nrf52832/ 维护，再随审核后的工具包单向复制。
工具以 release/tools/nrf52832_priceetag/ 完整明文目录交付；本期工具版本 0.2.0.3，buildId 单列。
版本材料由集成线归档到主库 release/versions/<version>/，工具和固件分别记录。
历史 combined 包仅保留旧证据，不作为新推荐工具。许可见 LICENSE、LICENSING.md、
THIRD_PARTY_NOTICES.md 和依赖自带许可；设备或公开发布权限不由本文授予。

## 工具私有格式闭包

probe／backup／flash 不启动 InkTag CLI，也不导入其 app、transport 或 BLE 模块。
配置、默认二维码、原生记录和字体校验采用工具私有 `nordic_tool_format` 冻结快照，
来源及 SHA 见该目录的 provenance.json；无需安装或寻找可变的 CLI。
二维码的 qrcode、验签的 cryptography 等离线依赖仍按独立锁文件校验。
`powershell-cli.ps1` 仅提供本地帮助和参数检查，不是 InkTag CLI 进程。
工具交付验证包含真正 prepare、绑定计划重载、假回读、原厂全空／Free／同 UID Pro、
异机旧授权告警转 Free 和缺依赖写前失败，并使用导入拦截器确认不依赖外部 inktag_cli。
这些离线检查不代替物理 Full 或改变现有导出写入门禁。
