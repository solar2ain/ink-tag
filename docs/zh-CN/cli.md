# 命令行工具

CLI 使用同一套 Python 客户端，提供 Windows、Linux 和 macOS 启动入口。需要 Python 3.11 或以上版本；首次安装会安装所需 Python 依赖。macOS 的蓝牙设备标识通常是系统 UUID，不一定是 MAC 地址。

## 安装和启动

解压 CLI 包后，在包含 `cli/` 的发布目录运行：

Windows：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\cli\install.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\cli\inktag.ps1 --help
```

Linux 或 macOS：

```sh
bash cli/install.sh
bash cli/inktag.sh --help
```

也可使用 `python -m pip install ./cli` 安装后运行 `inktag`。ZIP 包内的客户端位于 `inktag-cli/`，请在该目录运行安装入口。无需修改系统 Python 或使用管理员权限。

ZIP 包内路径是 `inktag-cli/`，不是 `cli/`；进入该目录后，省略命令中的 `cli/` 前缀。安装入口只建立运行环境，不编译设备固件。Linux 需可用的系统蓝牙服务与适配器；macOS 首次使用请允许终端/Python 的蓝牙权限。缺少 Python 的 `venv` 功能时，应先安装系统提供的对应组件。

下方示例使用已安装到虚拟环境的 `inktag` 命令；使用包内启动入口时，可将 `inktag` 替换为 Windows 的 `powershell -NoProfile -ExecutionPolicy Bypass -File .\inktag.ps1`，或 Linux/macOS 的 `bash inktag.sh`。

在发布仓库中，可运行 `inktag webui start --repo .` 启动附带的 `webui/` 页面。仅安装 CLI ZIP 不含 WebUI 页面；Find 服务请使用独立的本地服务包。未安装 Find 时，WebUI 的其他功能仍可使用。手机局域网访问使用 `inktag webui serve --repo . --lan`，详见 [WebUI 手册](webui.md)。

## 选择设备和读取信息

```text
inktag device scan
inktag --device IKT_NRF_1234 device info
inktag --device IKT_NRF_1234 screen show
inktag --device IKT_NRF_1234 diagnostic runtime
inktag --device IKT_NRF_1234 diagnostic clock
```

将 `IKT_NRF_1234` 替换为扫描返回的准确蓝牙名。`--device` 也接受唯一前缀；多台设备可见时使用完整名。可改用 `--address ADDRESS`，其中地址必须来自扫描，macOS 通常返回 UUID。使用 CLI 前断开 WebUI 对同一台设备的连接。

普通连接和正常 OTA 默认校时一次。走时观察使用 `--no-time-sync`，避免改变被测时间。只读输出可按命令帮助选择 JSON 格式。

`diagnostic runtime` 查看温度、电压及可用状态；`diagnostic clock` 查看设备与本机时间偏差。这些诊断不自动校时。没有有效值不代表零；支持范围与读数来源见[设备说明](devices.md)。

## 屏幕、时间和页面

```console
inktag screen profiles
inktag --device IKT_NRF_1234 screen set SSD1619_128x250_BWR_HEMA213
inktag --device IKT_NRF_1234 time sync
inktag --device IKT_NRF_1234 display calendar
inktag --device IKT_NRF_1234 display clock
inktag --device IKT_NRF_1234 display device-info
inktag --device IKT_NRF_1234 display clear
inktag --device IKT_NRF_1234 device label "Living room"
```

屏幕示例仅适用于对应盒马屏；其他设备使用 `screen profiles` 返回的匹配名称。原生宽高、可视区、方向等高级设置见 `screen set --help`。备注最多 24 个可打印 ASCII 字节，不修改蓝牙广播名。`display clear` 清除画面，不删除已存图片。

日历配置示例：

```console
inktag --device IKT_NRF_1234 screen calendar --week-start 1
inktag --device IKT_NRF_1234 screen calendar --countdown-name 假期 --countdown-date 2027-01-01
inktag --device IKT_NRF_1234 screen calendar --clear-countdown
```

`0` 为周日，`1` 为周一；倒计时名称最多 4 个字符。断电后恢复的是历史时间，首次使用或更换电池后应校时。

## 图片、图槽和轮播

```console
inktag --device IKT_NRF_1234 image upload photo.jpg --preview preview.png
inktag --device IKT_NRF_1234 image upload photo.jpg --slot next --name landscape --wire-encoding auto
inktag --device IKT_NRF_1234 slot list
inktag --device IKT_NRF_1234 slot show landscape
inktag --device IKT_NRF_1234 slot download 0 --output photo-0.png
inktag --device IKT_NRF_1234 slideshow start --interval 5m
inktag --device IKT_NRF_1234 slideshow stop
```

不指定 `--slot` 时直接显示，`--slot next` 保存到空闲槽，`--slot ID` 替换指定位置；加 `--store-only` 只保存不刷屏。图槽 ID 可以不连续，以 `slot list` 为准。名称与类型在上传或替换时保存，不支持单独改名或改类型。名称最多 35 个 UTF-8 字节；`--content-type` 的可选值见帮助。

`--wire-encoding auto` 按设备能力与收益选择压缩，`none` 发送原始数据；不要照搬旧版本的 `--compression` 参数。RLE 只减少传输量，不增加存储容量。替换图片中途断电可能丢失目标槽旧图，修改屏幕几何前先下载需要保留的图片。

删除单槽或全部图片都有确认提示：

```console
inktag --device IKT_NRF_1234 slot delete 0
inktag --device IKT_NRF_1234 slot clear
```

## 设备维护与设置清理

`diagnostic runtime`、`diagnostic health` 和 `diagnostic clock` 保留正常的只读设备诊断。`led locate` 用于寻找设备，`display ghost-clean` 用于残影清理；`nfc status` 查看 NFC 状态，`nfc action --help` 查看页面切换配置。操作范围以设备实际声明的能力为准。辅助开发的波形试验、输入模拟与 OTA 故障注入不包含在用户发行版中。

`device reset-settings` 恢复普通设置并保留图片；`device clear-user-data` 删除用户图片并保留设置。两者不是工厂重置。阅读确认内容后再执行；启用密码的设备仍需正常认证，修改或删除密码需新验证原密码。

## OTA 与 Recovery

```console
inktag ota inspect firmware-ota.zip
inktag --device IKT_NRF_1234 ota update firmware-ota.zip
inktag --device IKT_NRF_1234 recovery enter
inktag --device IKT_NRF_1234_OTA recovery resume-app
```

文件名替换为匹配主控的实际 OTA 文件：Nordic、GR、OM、TLSR 使用对应发布 ZIP，DA 使用 `.img`。不要上传完整烧录。日常更新保留默认校时；只有明确的走时连续性测试才使用 `--no-time-sync`。

安全退出要求已安装 Recovery 提供能力、应用完整有效且没有未完成写入；旧 Recovery 可能只支持更新。结果未知时不要重发退出或写入命令。详细恢复流程见[OTA 与 Recovery](ota.md)。

CLI 和 WebUI 使用同一套身份、密码、签名、电压与兼容性检查。可用命令不代表每台设备都有对应硬件或权限；不支持的功能会明确拒绝。CLI 包本身不包含 WebUI 页面，需要本地网页服务时另下载[本地服务包](webui.md)。

## 主机动态页面与天气站

主机负责取数、渲染与定时调度，设备接收普通图片。电脑需要持续运行，推送时设备需在蓝牙范围内。
可在 WebUI「创作 → 天气站」编辑控件和天气数据源，再点击「导出主机任务」。
CLI 导入该原稿，保留画板尺寸、色板、几何布局与天气绑定。
详细流程和系统任务计划示例见[主机动态页面手册](host-pages.md)。

```console
inktag host import weather.inktag.json --config weather.toml
inktag host render --scene weather.inktag.json --demo -o preview.png
inktag host fetch weather.toml --output forecast.json
inktag host render --scene weather.inktag.json --snapshot forecast.json -o weather.png
```

原稿通过路径引用，请勿删除。不同主机的字体可能略有差异，可通过 `--font-file` 或任务
的 `font_file` 指定本机字体，再检查 CLI 图片。导入任务默认关闭且未绑定设备。
也可继续使用以下 CLI 内置排版：

```console
inktag host templates
inktag host locations 杭州 --language zh
inktag host init weather.toml --latitude 30.27 --longitude 120.15 --label 杭州
inktag host render weather-paper --demo --size 400x300 --locale zh-CN -o preview.png
inktag host fetch weather.toml --output forecast.json
inktag host render weather-paper --snapshot forecast.json --locale zh-CN -o weather.png
inktag host validate weather.toml
inktag host jobs weather.toml
inktag host run weather.toml --once
inktag host status weather.toml
```

天气来自 [Open-Meteo](https://open-meteo.com/)，位置检索使用其 [GeoNames 数据](https://open-meteo.com/en/docs/geocoding-api)。
免费接口适用于非商业用途且无需 API Key，商用需遵循服务条件。只向数据提供者发送配置的位置、时区及天气字段，不发送设备身份、密码、账号或 Find 数据。
中文字体取自本机；未找到时用 `--font-file` 指定。演示数据会标注“示例”，不是实时天气。

TOML 中每个 `[[jobs]]` 引用一个 `scene` 原稿或内置 `template`，以及一个 `[sources.*]` 数据源，并分别设置尺寸、颜色、
目标及 `interval_minutes`。同一模板可供不同尺寸的屏幕使用，也可为同一地点复用数据。
任务默认 `enabled = false`，检查预览后再启用；建议每 60 分钟更新。

`run --once` 只评估一次到期任务，可由系统任务计划调用；不加 `--once` 则持续运行。
默认仅生成本机 PNG，不连接设备。添加 `--fixture forecast.json --data-dir ./weather-test` 可离线演练。
只有显式加 `--send` 才允许推送，任务中还需主机发现的 `address` 和真实的
`expected_device_id`（设备报告的 16 位十六进制身份，不是蓝牙地址）。
`device` 仅为显示名称，不用于每次按名称重新寻找目标。Windows/Linux 使用 MAC，
macOS 使用本机 CoreBluetooth UUID；后者不是 MAC，换电脑需重新绑定。
不要填入浏览器的 `BluetoothDevice.id`。改名不影响绑定，地址变化则需重新绑定；
不会自动连接另一台同名设备。
`inktag --device IKT_TL_1234 host target` 可读取一次绑定信息，将返回的 `target` 字段填入任务；
重名时改用 `--address`。此命令不校时、不启用任务、不推送页面。
推送沿用普通认证与图片传输，并按自定义动态模板策略检查 Pro 激活状态；后台缺少已保存凭据时失败，不等待密码输入。
本增量尚待实板验证，不代表已完成全部设备资格确认。

像素未变化不重复发送，错过的周期合并处理。失败不主动清屏，成功记录保留并延时重试；预览与推送状态独立。
将 `enabled` 改为 `false` 或用 Ctrl+C 停止运行器。设备 STOP 不会停止电脑上的任务。
