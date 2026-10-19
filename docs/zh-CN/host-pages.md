# 主机动态页面：天气站与日期牌

中文 | [English](../en/host-pages.md)

主机动态页面由电脑获取数据、渲染为图片，再通过蓝牙发送到墨水屏。它不会向设备安装
脚本，也不依赖浏览器持续打开。运行任务的电脑须持续开机，推送时设备须在蓝牙范围内。

## 1. 在画板编辑

打开 WebUI「创作」，选择「天气站」模板或新建「主机动态页面」。天气模板由可编辑
控件组成，支持移动、四角缩放、复制、配色和普通文字、表格、图片、二维码、笔迹。
添加控件区末尾的「天气」方形按钮仅在主机动态页面显示，可展开添加天气组合或单个控件。
日期使用强调色，温度保持黑色，图表可用颜色区分。灰色电子纸背景只用于预览，导出图片仍为白底。
日期数字用红色，年月日单位用黑色。文字控件默认透明背景，可在属性栏切换透明或屏幕色板中的颜色。
小于 400×300（含对应竖屏）的图片类预置模板不再列出；仍可在空白画板添加图片框。
图片控件可调整抖动算法、对比度、抖动强度及重色模式，参数保留在原稿中，预览和导出复用。
二维码可拖动四角或输入像素尺寸连续缩放，不限于整数倍；绘制保留四格留白和黑白硬边。
竖屏课程表不再提供预置模板；横屏、方屏课表及已保存的页面保留。

| 天气控件 | 显示内容 |
| --- | --- |
| 地点、温度、体感、湿度、风速 | 当前天气数值 |
| 年月日、星期、农历 | 天气数据所在日期；默认模板显示日期而非当前时刻 |
| 天气图标、天气状况 | 对应天气代码的图形和文字 |
| 24小时温湿度 | 默认黑色气温柱、红色湿度线，共用时间轴；左右分别为 °C 和 % |
| 降水概率柱状图 | 可单独添加的降水趋势 |
| 未来天气 | 最多五日预报，按控件宽度排布 |
| 更新时间 | 可选的观测时间控件，默认模板不使用 |

在右侧「天气数据源与预览」设置地点名称、经纬度和时区，点击「应用数据源」。
可点击「获取天气预览」联网取数，或导入 CLI 生成的天气快照。首次使用演示数据，
数据状态只在编辑器侧栏提示，不进入屏幕画面；获取实际天气后再启用任务。来源署名
保留在天气数据设置和快照元数据中，默认模板及 CLI 导出不强制叠加来源文字。
没有逐小时湿度的旧快照只显示气温柱，不用当前湿度推算全天湿度。
小屏优先大字温度；横向大屏将未来天气排在当前天气右侧，下半区展示气温柱与湿度线。
方屏和窄竖屏使用对应排版，不缩小整幅大屏界面。预览和编辑不会启动任务或连接设备。

点击「导出主机任务」下载 `*.inktag.json`。原稿保留控件和绑定，不只是截图，且不会
自动附加设备地址、密码或账号信息。原生设备控件、设备变量、设备字库和图槽轮播
不是主机天气功能；不支持的内容在导出时明确提示。旧本地原稿不会被自动删除。

## 2. 导入、渲染与检查

使用最新版 CLI，在原稿所在目录运行：

```console
inktag host import weather.inktag.json --config weather.toml
inktag host validate weather.toml
inktag host render --scene weather.inktag.json --demo -o preview.png
inktag host fetch weather.toml --output forecast.json
inktag host render --scene weather.inktag.json --snapshot forecast.json -o weather.png
```

原稿名称不是固定的，请替换为实际文件名。`import` 创建可编辑的 TOML，不覆盖已有文件，
默认 `enabled = false`。原稿由相对路径引用；请保留它，迁移电脑时一并复制。
画板尺寸和色板来自原稿，不能通过 `render --size/--palette` 偷换；修改后重新核对任务。

主机使用本机字体，中文默认寻找系统中文字体。找不到时添加 `--font-file`，例如：

```console
inktag host render --scene weather.inktag.json --demo --font-file "C:/Windows/Fonts/msyh.ttc" -o preview.png
```

也可在任务中写 `font_file = "C:/Windows/Fonts/msyh.ttc"`。macOS/Linux 使用当地已安装
字体的真实路径。字体不随原稿分发；浏览器与主机的字体度量可能略有差异，请检查 CLI
生成的图片。普通富文本保留颜色、字号、对齐、字间留白与基线偏移，控件边界外的内容会裁切。

天气来自 [Open-Meteo](https://open-meteo.com/en/docs)。请求只包含配置的位置、时区及
天气字段，不含设备身份、密码、账号或 Find 信息。免费接口适用于非商业用途；商用应
遵循提供者条款。来源署名保留在快照元数据中。示例/离线快照不能用于定时推送。

## 3. 绑定设备并启用

```console
inktag --device IKT_TL_1234 host target
```

该命令读取一次准确目标，不校时、不启用任务、不推送。将返回的 `address` 和
`expected_device_id` 填入 TOML 对应的 `[[jobs]]`，然后设置 `enabled = true`：

```toml
[[jobs]]
id = "desk"
scene = "weather.inktag.json"
source = "home"
width = 400
height = 300
locale = "zh-CN"
interval_minutes = 60
enabled = true
device = "IKT_TL_1234" # 可选显示名称
address = "AA:BB:CC:DD:12:34" # 替换为实际读取的主机蓝牙地址
expected_device_id = "0123456789ABCDEF" # 替换为设备报告的真实身份
```

Windows/Linux 使用 MAC；macOS 使用本机 CoreBluetooth UUID，不是可跨电脑复用的 MAC。
浏览器的 `BluetoothDevice.id` 不是这里的地址。名称仅作显示，运行器不按名称重选设备。
改名不影响绑定；地址改变需重新绑定。TOML 是本地配置，含设备身份，不要提交到公共仓库。

```console
inktag host run weather.toml --once
inktag host run weather.toml --once --send
inktag host run weather.toml --send
inktag host status weather.toml
```

不加 `--send` 仅生成本机 PNG。`--once` 只评估一次到期任务；不加时持续运行，Ctrl+C
停止。后台推送使用已保存的正常认证凭据并检查 Pro，不等待交互式密码。尺寸、色板或
身份不匹配时拒绝发送，不改屏幕配置、图槽或绑定。具体设备的推送仍需实板验证。

## 4. 定时运行与停止

推荐每 60 分钟更新。同一任务的预览、推送状态独立；像素未变化不重发，错过周期合并
为一次，失败保留上次成功状态并退避，不主动清屏。修改原稿会触发重新渲染；修改尺寸
后应同步 TOML。定时任务以 `[sources.home]` 为准，改地点时同步编辑其坐标与时区。

Windows「任务计划程序」可配置每 5 分钟运行一次：程序填写安装环境中的 `inktag.exe`，
参数填写 `host run "D:/InkTag/weather.toml" --once --send`。配置文件和状态目录均使用
绝对路径，并使用能够访问蓝牙与已保存凭据的普通用户身份，不要同时运行两个不同状态库。

Linux/macOS 的用户级任务同样调用 `inktag host run /绝对路径/weather.toml --once --send`。
macOS 蓝牙权限取决于实际启动环境；系统服务权限不能视作终端已授权，需在该环境验证。
关闭任务计划或设置 `enabled = false` 可暂停；设备上的 STOP 不会停止电脑定时任务。

状态默认保存在用户 InkTag 数据目录的 `host-pages/`，与 WebUI/Find 服务分开管理。
需要指定时使用 `--data-dir /绝对路径/InkTag`。离线验证可运行
`host run weather.toml --once --fixture forecast.json --data-dir /绝对路径/测试目录`，
此模式不得与 `--send` 同用。联网预览失败不会阻止编辑，失败也不会伪装成最新数据。

## 5. 日期牌

在「创作 → 日期牌」选择屏幕尺寸。大字日期、星期、农历和天气均为可编辑控件，
横屏左右分栏，竖屏上下排布，小屏优先保留日期和当前天气。日期数字用强调色，温度为黑色。
大字日期复用设备时钟的绘制数字，不依赖额外字体文件；可调整尺寸和颜色。

日期取自天气快照的当地日期，不显示持续走动的当前时间。使用与天气站相同的数据源、
设备绑定和推送流程；每天更新可在导入时选择 1440 分钟间隔。

```console
inktag host import date-card.inktag.json --config date-card.toml --interval-minutes 1440
inktag host render --scene date-card.inktag.json --demo -o date-card.png
inktag host validate date-card.toml
inktag host run date-card.toml --once
```

导入后任务仍为关闭状态。演示渲染使用 `--demo`，实际天气用 `host fetch` 获取快照后
通过 `--snapshot` 渲染。显式启用与 `--send` 推送流程见上文，本机预览不会连接设备。
