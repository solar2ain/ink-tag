# InkTag 用户 CLI

[English](README.en.md)

密码状态返回 `PERSISTENCE_ERROR` 时，`device info`、`device health` 和 `time show`
可保留基础只读诊断连接，并明确提示密码状态未知；不会校时、尝试密码或进入
Recovery。设备仍可拒绝健康读取。需要保留时间证据时继续使用 `--no-time-sync`。
这不是密码关闭、数据修复或 OTA 成功的证明，其他命令不会获得此例外。

InkTag CLI 是面向普通用户的跨平台蓝牙命令行工具。初版支持设备发现、身份与遥测、
清屏、日历、时钟、设备信息页、停止任务、屏幕配置读取、图片处理与上传、设备图槽、
轮播、硬件诊断和 Nordic、Goodix、OnMicro、Dialog/Renesas 四类 OTA。图片算法
直接运行 WebUI 的同一份实现，不维护一套近似的 Python 复刻。

## 安装

需要 Python 3.11 或更新版本。源码仓库开发安装：

    python -m pip install -e cli
    inktag --help

同时维护多个 worktree 时，editable install 可能仍指向另一个 worktree。实机开发
不要直接假设 `python -m inktag_cli` 会载入当前目录；从仓库根目录使用下面的开发
入口，它会把当前 worktree 的 `cli/src` 放到最前并校验实际导入路径：

    .\tools\dev-inktag-cli.ps1 --device InkTag_GR_ device info

本地 WebUI 与 Find 身份库也由同一 CLI 编排：`inktag webui serve --port 8787` 使用 Python 启动
production 前端、SQLite API 和 Find 服务；运行时不需要 Node.js。Find 数据默认位于主数据
目录的 `find/` 子目录，正常启动无需指定 `--repo`、`--find-data-dir` 或
`--find-private-key`。`inktag find
identity list/import/select` 管理运行中
本机服务的身份。私钥只经 loopback 发送给本机后端，不进入浏览器、设备或日志。
GitHub 静态站点和 standalone 单文件版仅使用浏览器 IndexedDB/localStorage，不提供
Find 后端。

日常建议使用后台生命周期命令：

    inktag webui start --port 8787
    inktag webui status
    inktag webui log --follow
    inktag webui restart
    inktag webui stop

后台日志保存在主数据目录的 `log/`：`webui.log`记录 WebUI 与服务生命周期，
`find.log`记录 Apple 登录、定期查询和 Find 错误。完整说明见
[`../docs/webui-usage.md`](../docs/webui-usage.md)。

GitHub Release 会附带 InkTag-cli-v版本.zip。下载并核对 SHA256SUMS.txt 后可安装：

    python -m pip install InkTag-cli-v0.2.0.zip

Ubuntu/Debian 还需要系统 BlueZ 服务：

    sudo apt-get install bluez
    sudo systemctl enable --now bluetooth

WSL2 默认不能直接使用 Windows 正在占用的蓝牙；实机测试需用 `usbipd` 将独立 USB 蓝牙适配器暂时完整转交给 WSL，转交期间 Windows 不能使用该适配器。

## 常用命令

    inktag device scan
    inktag --device IKT_TL_9903 device status
    inktag --device IKT_OM_ device info
    inktag --address AA:BB:CC:DD:EE:FF device rename IKT_Desk
    inktag --device IKT_Desk device sync-links --sync-managed-qr
    inktag --address AA:BB:CC:DD:EE:FF device label "客厅屏"
    inktag --address AA:BB:CC:DD:EE:FF device qr https://solar2ain.github.io/ink-tag/
    inktag --address AA:BB:CC:DD:EE:FF led status
    inktag --address AA:BB:CC:DD:EE:FF led policy critical
    inktag --address AA:BB:CC:DD:EE:FF display calendar
    inktag --address AA:BB:CC:DD:EE:FF screen calendar --week-start 1 --countdown-name 国庆 --countdown-date 2026-10-01
    inktag display stop
    inktag screen show
    inktag image algorithms
    inktag image process photo.jpg --size 400x300 --color BWRY --algorithm auto
    inktag --device InkTag_GR_ image upload photo.jpg --algorithm 2 --strength 0.8
    inktag --device InkTag_GR_ image upload photo.jpg --slot next --name holiday
    inktag --device InkTag_GR_ image upload photo.jpg --slot next --store-only
    inktag --device InkTag_GR_ image upload photo.jpg --slot 8 --preview preview.png
    inktag slot list
    inktag slot download 2 --output slot-2.png
    inktag slot download 2 --format both --output slot-2
    inktag slot show 2
    inktag slot alias 2 "课程表"
    inktag slot show "课程表"
    inktag slideshow start --interval 5m
    inktag test diagnostic
    inktag led color blue
    inktag led effect blue fast-flash --repeat 5
    inktag led effect white sequence --repeat 3 --step-ms 240
    inktag led pattern "red:120,off:120,green:120,off:120,blue:240" --repeat 2
    inktag led off
    inktag time accuracy --duration 10m --interval 0.23
    inktag test nfc --mode status
    inktag test nfc --mode dump
    inktag test nfc --mode configure --action calendar
    inktag test nfc --mode simulate --action slot --slot 2
    inktag find status
    inktag find keyring-provision --device-name "我的 InkTag" --yes
    inktag find enable --allow-experimental
    inktag find disable
    inktag device verify --expect-board gr551x_zkc42vm --require-ota
    inktag device reset --yes
    inktag ota inspect firmware.zip
    inktag ota update firmware.zip

设备名称允许唯一前缀匹配；多个设备匹配时 CLI 会拒绝猜测并要求使用更精确的
`--device` 或 `--address`。macOS 的 address 通常是 CoreBluetooth UUID，并不等于
设备 MAC。Windows 上显式 `--address` 会绕过容易漏报的扫描，直接尝试缓存 GATT，
但仍严格校验在线设备的服务、特征和协议版本；缓存校验失败才强制刷新服务表。其他
平台按准确地址收到第一条匹配广告后继续连接。形如 `IKT_TL_9903` 的完整出厂名称
也会在精确命中后继续；名称前缀或自动发现则在首个匹配后再聚合一个覆盖普通广播与
Find 复用窗口的短周期，以免把多个设备误判为唯一设备。只有完全没有匹配时才会耗尽
`--scan-timeout`；`device scan --all` 为了列全附近设备仍会使用完整扫描时长。普通
InkTag 扫描会把标准服务 UUID 下推给系统蓝牙后端，减少 Windows 在附近设备较多时的
无关扫描开销。连接较慢时加全局 `--verbose`，CLI 会分别报告扫描轮次、GATT 连接、
固件身份读取、V2 握手和连接就绪耗时；这些计时不会改变协议或设备状态。

`device status` 只读取广播，不建立 GATT 连接。传入普通设备名时，它也会匹配追加
`_OTA` 的 Recovery 名称，并结合广播中的 OTA/DFU 服务 UUID 输出当前广播角色为
`application` 或 `recovery`。

`device rename` 默认同步支持 NFC 的设备 NDEF 链接，但不会覆盖设备信息二维码；CLI
无法从二维码矩阵可靠判断它是否为用户自定义内容。只有确认二维码由 InkTag 管理时，
才使用 `--sync-managed-qr` 显式替换。若链接同步失败，蓝牙名仍已生效；修复连接后运行
`device sync-links` 重试 NFC，或运行 `device sync-links --sync-managed-qr` 同时重试官方
二维码。`--no-link-sync` 可关闭默认的 NFC 同步。

同一个主机蓝牙适配器不要同时交给多个 CLI 进程、浏览器 Web Bluetooth 连接或其他
BLE 调试工具。设备已被 WebUI 连接时通常不再广播，其他进程自然无法扫描到；多个
扫描器同时抢占 Windows/BlueZ 后端也可能造成漏报。实机自动化应串行持有适配器，
并在报告中把扫描、连接、GATT 服务发现和 V2 初始化耗时分别记录。

蓝牙广播名和设备标签是两个独立字段：广播名默认以 `IKT_` 开头；已有 `InkTag_`
名称继续兼容。后缀只能使用 ASCII 字母、数字、下划线、连字符和客户端允许的常用
汉字；设备标签可以使用 UTF-8，
传入一个空白字符串可清除标签。CLI 会按固件回传的字节上限拒绝过长名称。

## 图片处理与上传

`image algorithms` 列出的名称和序号固定对应 WebUI 的算法列表。`image process`
离线生成设备色板 PNG；`image upload` 从设备读取实际可视分辨率、颜色和方向，完成
裁剪/拉伸、自动横竖旋转、缩放、平移、处理预设、对比度、重色和抖动，再按设备
传输格式打包。常用参数可用 `--fit crop|stretch`、`--rotate auto|0|90|180|270`、
`--scale`、`--pan-x`、`--pan-y`、`--preset`、`--contrast`、`--strength` 和
`--heavy-ink` 调整。

不带 `--slot` 时只显示图片。`--slot next` 保存到下一个可用图槽；图槽已满时会拒绝，
加 `--store-only` 会完成图槽提交但不刷新当前墨水屏画面；它必须和 `--slot` 一起使用。
只有显式加 `--force` 才会替换最早的兼容普通图片。`--slot ID` 表示替换指定记录，
不会要求额外的 `--force`。CLI 仅使用 0.2.0 V2 事务协议：等待旧任务取消、所有分块、
图槽提交以及带同一事务号的最终完成事件；失败会显式执行 `TRANSFER_ABORT`，不会把
“数据传完”误报成“屏幕刷新完成”。连接到非 `0x20` 固件时，CLI 会在发送第一条命令
前拒绝连接，不再回退到 V1。

`slot download ID` 使用 `READ_RESOURCE` 严格校验每个分块的槽号、偏移和总长度，
默认按当前兼容 screen profile 的可视区域、方向和控制器 padding 导出 PNG；
`--format raw` 保存设备原始色平面，`--format both` 同时保存两者。命令会报告下载
内容的 CRC-32 和 SHA-256。当前冻结协议不回传记录头中的期望 CRC，因此这里的
CRC-32 是下载内容摘要，而不是与设备私有记录头进行第二次比对。

## 调试入口迁移

后续 BLE 调试统一使用本 CLI。原来的
`tools/hardware/smoke-test-inktag-ble.py` 只保留为历史回归参考，不再增加产品功能。
身份/profile/OTA 服务断言由 `device verify` 提供；状态和原始通知由
`device info`、`device watch`、`test diagnostic` 提供；NFC 状态、用户区、
行为写入和完整模拟由 `test nfc` 提供；显示、图片、图槽、轮播和 OTA 分别使用
对应的正式子命令。旧 `model-id`、`write-mode`、`skip-query` 和任意 GATT
写入属于已退役的协议/传输调试细节，不作为用户接口重新暴露。

`test nfc --mode simulate` 会等待触碰事件以及最终页面/图槽/停止结果，而不是
只等命令入队；`test button` 同样等待所配置动作的最终显示/轮播结果。重启会立即
断开 BLE，因此 `device reset` 必须确认，自动化需带 `--yes`。

`test nfc --mode channel --samples 56 --interval 0.25` 在同一连接上采集
有界只读快照（1–256 次，间隔 0–60 秒）。默认只采集一次，输出格式保持不变。
任一请求失败或取消立即结束，不自动重连或重试；`--verbose` 保留原始通知。
时钟连续性诊断需在子命令前加 `--no-time-sync`。快照不会扩大设备的 NFC
地址范围，也不能证明 SRAM 通道或 EEPROM 写入安全。

全局选项需要放在子命令之前，例如 `inktag --json device info`。删除图槽和
开始 OTA 在非交互环境中必须显式传入 `--yes`。OTA 会先验证容器、布局、
尺寸和 SHA-256，完成后还会校验设备回传的应用版本、字节数、checksum、
Swap 确认状态和 generation。当某个硬件批次没有安全的图槽 Flash 区域时，
`slot`/`slideshow` 会明确拒绝，不会向应用或 OTA 区写入。支持激活的设备可用
`activation request` 读取 `设备码:挑战码`，再用 `activation install` 安装内部
管理器签发的 128 字符 `IKAC1` 授权；`activation status` 可在重连或重启后复核。
`find status` 可只读检查能力、复用窗口和持久配置；当前设备仅支持 Find v2，
设备持有主公钥 P 与滚动密钥 K，不接收主私钥或派生私钥。首次 `find enable`
由规范本地服务生成并保护所有者材料，核对设备提交证明后启用。
`find identity import-local-v2` 可按精确指纹、设备身份和 BLE 名从已有受保护的
本机库恢复 v2 所有者，不换主、不自动选择或开启追踪。`list/select/update/delete`
管理同一身份库，删除必须传入 `--yes`。私钥不会发送到浏览器或设备，也不提供私钥导出命令。仅宣传
`EXPERIMENTAL_FIND` 的板子必须在启用时显式传入 `--allow-experimental`。CLI
的 `find clear --yes` 按设备能力和鉴权规则清除设备端 Find 身份与配置。

普通 `ota update` 在应用验身后显式校时一次。若镜像安装和验身成功，但后续连接或
校时失败，命令报告“部分成功”并以退出码 1 结束；JSON 返回 `ok=false`、
`status="partial-success"`、`applicationVerified=true`、`driverExitCode=0`，
同时保留 `clockSyncError` 和驱动输出。这不代表镜像未安装，不应自动重复 OTA；
排查连接/鉴权后单独运行 `time sync`。走时实验显式使用 `--no-time-sync` 时不会
要求校时；`--skip-verify` 不证明应用验身成功。普通连接的机会性校时仍不阻塞只读操作。

`time accuracy` 在一次不校时的 BLE 连接内读取设备整秒跳变，并以主机单调时钟
为参考拟合 ppm 和每天误差；子秒采样只用于缩小整秒边界范围，不会把 BLE/HFCLK
当成设备走时源。短时结果会同时给出回归 95% 误差范围，发布级结论仍需长时间、
无调试器的恒温/变温实测。
`--interval` 仅支持 0.1–0.5 秒，默认 0.23 秒；5 秒等稀疏采样无法捕获整秒
边界，会在连接前拒绝。BLE 读取过慢也可能导致分辨率不足，这不是设备走时
故障的证据；不要将离散 `time read` 快照当作 ppm 精度测量。

`device health --duration 30 --interval 5` 在同一条 BLE 连接中连续读取健康信息，
立即采样并在结束时再次采样。时长单位为秒，最多一小时；采样间隔至少一秒。
不加 `--duration` 时仍返回原来的单次快照。观察既有计时/睡眠状态时使用：

    inktag --device IKT_TL_ACEE --no-time-sync --json device health --duration 30 --interval 5

输出保留各次原始计数、UTC 采样时间、经过时间及请求耗时，不因重连累加睡眠次数。
计数回退可能表示复位或回绕，不能当作有效增量；没有 `sleepEntries` 的固件仍可
读取其他健康字段。健康计数不能代替电流测量，定期 BLE 查询本身也会唤醒设备。

`test display-health` 在同一条已鉴权连接上刷新一次日历，收到 ACK 后并发读取
健康和时间，再等待该次刷新 COMPLETE。它不修改图槽或主动校时；普通连接仍默认
校时，保留时钟/功耗证据时使用全局 `--no-time-sync`：

```powershell
inktag --device IKT_TL_6F40 --no-time-sync --json test display-health --read-timeout 3 --timeout 100
inktag --device IKT_TL_6F40 --no-time-sync --json test display-health --slot 0
```

`--slot` 接受已有可见图槽的 ID 或别名；不指定则刷新日历。输出分别记录 ACK、
每个读取的耗时/结果、COMPLETE 和总耗时。读取失败仍等待刷新终态，不重复刷新、
不自动取消；终态或读取失败退出码为 1。`overlapConfirmed` 只有两次读取都在
COMPLETE 前应答才为 true；ACK 晚到或刷新过快不算“刷新期间应答”通过。
没有 HEALTH 能力时拒绝启动。此测试会真实全刷屏幕，不能代替电流或光学测量。
采样失败会退出并断开连接，不自动重连后宣称同连接测试成功。

## 初版边界

TLSR 固定 Recovery v3 在传输数据时明确返回 `QUEUE_FULL`，CLI 最多自动
退让一次：断开、等待一秒、重新连接同一设备，从块 0 重启同一个已验证镜像，
并将窗口降为单包；之后的传输重连也保持单包。再次队列满立即失败，整个传输
仍受原有八次尝试上限约束。未知 Recovery 版本、低压、签名、布局或其他
固件拒绝不会因此重试。完成后仍须 App 身份/镜像校验与默认的一次校时；
传输完成不等于 OTA 全流程成功。

- 不提供首次烧录、原厂备份或恢复；这些操作继续使用各端口受保护的公开脚本。
- 不提供任意 GATT 写入或屏幕 GPIO 改写。
- 激活只在准确声明 `ACTIVATION` 能力的平台上启用；其他平台会明确返回不支持，
  不会尝试兼容或降级写入。
