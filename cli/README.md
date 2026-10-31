# InkTag 命令行客户端

中文 | [English](README.en.md)

InkTag CLI 可以连接设备、发送图片、管理显示内容和执行 OTA，也能启动 WebUI、查询 Find 和运行主机天气任务。命令名为 `inktag-cli`，也可以使用 `inktag`；两者功能相同。

## 快速开始

安装 Python 3.11 或更新版本，从本仓库下载／拉取代码，或解压完整 CLI ZIP，进入同时包含 `cli/` 和 `webui/` 的根目录。推荐使用统一安装入口：

**Windows：**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\install_inktag.ps1
```

**macOS／Linux：**

```sh
sh install_inktag.sh
```

脚本会把 CLI 安装到 `cli/.venv`，并自动安装**本地 Find 查询服务的依赖**；Find 环境单独位于用户数据目录的 `find/runtime/.venv`。安装完成后以后台模式启动本地 WebUI 和 Find 查询服务（`webui start`）；关闭终端不会停止服务。安装完成时会显示可直接复制的启动、停止、状态和日志命令。macOS 新安装的数据和日志位于 `~/.inktag/`；已有旧目录时继续使用原目录，避免丢失会话或标签信息。不会修改系统 Python，也不会自动登录 Apple、连接设备或清除已有数据。

首次安装需要联网。Windows 还需预先安装完整版 7-Zip，首次会下载约 292 MB 的 Apple 认证组件镜像；macOS 使用系统原生认证组件，无需该镜像。Linux 的位置查询仍未完成资格验证。

只安装、不启动：Windows 运行 `powershell -NoProfile -ExecutionPolicy Bypass -File .\install_inktag.ps1 --prepare-only`；macOS／Linux 运行 `sh install_inktag.sh --prepare-only`。后续使用下面两种方式之一，命令参数完全相同。

### 方式一：直接运行客户端脚本（无需激活）

在仓库／完整包根目录运行，脚本使用已安装的 `cli/.venv`：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\cli\inktag.ps1 device scan
```

```sh
bash cli/inktag.sh device scan
```

更新后重新运行统一安装脚本即可。只需要 CLI、不需要 Find 查询时，也可进入 `cli/`，运行原有的 `install.ps1` 或 `bash install.sh`。

上述统一安装入口在本仓库与完整 **CLI ZIP** 中均提供。若仅安装 wheel／sdist，请自行运行 `python -m venv .venv`，Windows 使用 `. .\.venv\Scripts\Activate.ps1`、macOS／Linux 使用 `source .venv/bin/activate` 激活这个环境，再运行 `python -m pip install "下载的 .whl 或 .tar.gz 文件路径"`。解压后含 `pyproject.toml` 的源码目录也可运行 `python -m pip install .`。这类文件不包含完整包根目录的统一安装入口和 Find 查询服务。

### 自行准备 CLI 环境（未使用统一安装时）

先安装 Python 3.11+，再从仓库／完整包根目录创建环境并安装 CLI。Windows：

```powershell
python -m venv cli\.venv
.\cli\.venv\Scripts\python.exe -m pip install .\cli
```

macOS／Linux：

```sh
python3 -m venv cli/.venv
cli/.venv/bin/python -m pip install ./cli
```

上述命令只安装 CLI。需要本地 Find 查询时，优先使用前面的统一安装入口，一并准备查询依赖；无需重建已有且正常的环境。

### 方式二：激活环境后使用 `inktag-cli`

以下激活命令从仓库／完整包根目录运行；自行安装 wheel／sdist 时请使用自己创建的 `.venv` 路径。Windows 在当前 PowerShell 中运行：

```powershell
. .\cli\.venv\Scripts\Activate.ps1
```

macOS／Linux 在当前终端中运行：

```sh
source cli/.venv/bin/activate
```

激活后，本文中的 `inktag-cli`（或 `inktag`）命令可直接使用，也可运行 `python -m inktag_cli`。每个新终端需要重新激活；若不想激活或系统限制激活脚本，继续使用上面的包装脚本，不必修改系统执行策略。退出环境运行 `deactivate`。

```sh
inktag-cli -h
inktag-cli device scan
inktag-cli device -n B892 info
inktag-cli display -n B892 calendar
inktag-cli image -n B892 upload photo.png --slot next --name "旅行"
```

### 从 CLI 启动本地 WebUI

回到解压后的 `InkTag-client` 根目录：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\cli\inktag.ps1 webui start --repo . --enable-tls
```

```sh
bash cli/inktag.sh webui start --repo . --enable-tls
```

如果主要使用网页和 Find，也可直接运行解压根目录的 `powershell -NoProfile -ExecutionPolicy Bypass -File .\install_inktag.ps1`（Windows）或 `sh install_inktag.sh`（macOS／Linux）。这组入口准备本地 WebUI／Find 环境并启动服务，不会在系统里注册 CLI 命令。Find 环境位于用户数据目录的 `find/runtime/.venv`，与上面的 `cli/.venv` 分开；默认数据位置见 [WebUI 使用说明](../docs/zh-CN/webui.md)。

每个命令及子命令都支持 `-h` 和 `--help`，完整参数以当前命令帮助为准。以下示例使用已激活环境中的 `inktag-cli`；未激活时，用包装脚本替换命令开头即可。

扫描、连接、渲染和查询等操作在终端中显示旋转指示和实时 `elapsed`。图片上传同时显示进度；完成或取消后自动收起。输入密码或确认操作时暂停动画。`--json`、输出重定向和日志服务不显示动画。

`device scan` 与其他设备命令的首次发现默认最多 30 秒，连接命令找到目标后会提前继续。设备管理、图片、显示、OTA 和主机任务均使用这个默认值。可在 `device` 后用 `--scan-timeout 秒数` 调整，例如 `device -n B892 --scan-timeout 45 info`；它不改变 OTA 或 Recovery 切换后的等待时限。扫描时会实时显示已发现设备数，下一行用逗号列出蓝牙名。结束后清除动态状态并显示结果表；按 Ctrl+C 取消时，也会列出已发现的设备，退出码为 130。其他命令结束时同样收起等待状态。

连接完成后会显示蓝牙名和地址，无需开启 `--verbose`。原生 OTA 连接会先提示蓝牙链路已连接；这不代表升级已完成。使用 `--json` 时，连接提示以独立 JSON 行写入 stderr，stdout 仍只包含命令结果。

## 选择一台或多台设备

设备参数放在 `device`、`image`、`display` 等对应入口后面，子命令前面：

```sh
inktag-cli device -n B892 info
inktag-cli device -a AA:BB:CC:DD:EE:FF info
inktag-cli device -n B892,6775 info
inktag-cli device -a AA:BB:CC:DD:EE:01,AA:BB:CC:DD:EE:02 health
inktag-cli --json device -n B892,6775 health
```

`-n`／`--ble-name` 匹配蓝牙名称，支持不区分大小写的唯一名称片段。完整名称优先；片段匹配多台时会列出候选，不直接操作。不能用芯片 UID 选择设备。`-a`／`--ble-address` 使用精确蓝牙地址；macOS 使用扫描结果中的 CoreBluetooth 标识。

逗号分隔多个目标。若同时给名称和地址，两份列表必须等长，按位置配对为同一台设备的名称和地址；一台设备不会因此执行两次。重复目标会去重。批量默认逐台执行，分别报告结果；一台失败不会自动重试该操作。按 Ctrl+C 停止余下操作。不同设备也可在不同终端同时运行命令。

连接参数还包括 `--scan-timeout`、`--connect-timeout`、`--no-time-sync` 和设备认证选项。普通连接默认校时；走时观察或需要保留设备时间时使用 `--no-time-sync`。只读诊断按各命令说明执行。

## 命令入口

| 入口 | 用途 |
| --- | --- |
| `device scan / info / status / health / watch / verify / restart` | 扫描、设备信息、状态、健康、观察、核对和重启 |
| `device display`，或 `display` | 日历、时钟、设备页、清屏、停止和轮播 |
| `device image`，或 `image` | 图片转换、算法列表和上传 |
| `device slot` | 列出、显示、下载、删除图片槽 |
| `device setting` | 名称、二维码、屏幕、引脚、按键、字体和密码 |
| `device led / time` | 状态灯、灯光操作、时间读取和同步 |
| `device ota / recovery` | OTA 包检查、升级、进入 Recovery 和返回应用 |
| `device activation` | 查看授权、导出激活请求、安装激活码 |
| `device diagnostic` | 健康、运行状态和走时诊断；功能取决于设备 |
| `device findmy` | 设备端 Find 启用、关闭和状态 |
| `findmy` | 本机 Find 账号、身份、查询、追踪；设备操作也可用 `findmy device` |
| `host` | 主机动态页面、天气数据、模板和定时任务 |
| `webui` | WebUI 前台、后台、状态、停止、重启及日志 |

## 设备设置

```sh
inktag-cli device -n B892 setting show
inktag-cli device -n B892 setting ble-name show
inktag-cli device -n B892 setting ble-name set IKT_OM_B892
inktag-cli device -n B892 setting device-name set "My desk"
inktag-cli device -n B892 setting device-url set "https://example.com/"
inktag-cli device -n B892 setting screen show
inktag-cli device -n B892 setting screen profiles
inktag-cli device -n B892 setting pins show
inktag-cli device -n B892 setting pins set --config pins.json --yes
```

`ble-name` 是用来搜索连接的蓝牙广播名。`device-name` 是设备信息页显示名称，目前为最多 24 字节的可打印英文字符。`device-url` 设置设备信息页二维码的 URL 或文本；`device-url sync` 根据当前蓝牙名更新托管链接。当前固件暂不支持二维码内容回读。

`setting pins` 统一管理 `screen`、`led`、`buttons` 和 `buzzer` 四个 JSON 区域。未提供的区域保留当前值。示例只修改两个按键：

```json
{
  "buttons": [
    {"gpio": 26, "activeLow": true, "pull": "up"},
    {"gpio": 255, "activeLow": false, "pull": "none"}
  ]
}
```

GPIO 255 表示未连接。屏幕和 LED 的格式可参考 `pins show` 返回的 `configuration`。屏幕 BUSY／RESET 等行为由所选驱动决定；板卡 EN 和 LED 保留有效电平设置。不提供 TXD/RXD 配置。按键最多两个，默认低电平有效；蜂鸣器通过模式和引脚配置，无需另一个启用开关。仅能修改设备实际声明可编辑的项目。

屏幕/LED 与按键/蜂鸣器是分步写入；如果失败，结果会列出已经确认的区域，请先读取当前配置再决定下一步。`pins restore-screen-led-defaults` 只恢复屏幕和 LED 默认接线。

```sh
inktag-cli device -n B892 setting button show
inktag-cli device -n B892 setting button set single next
inktag-cli device -n B892 nfc usage
inktag-cli device nfc commands calendar
```

两个按键共用单击、双击、长按动作。NFC 无需选择模式或开关；`device nfc usage` 只读显示设备的使用说明及已声明的手机命令。用 `device nfc commands calendar` 在本地生成命令文本，再在手机 NFC Tools 中选择“写入 → 添加记录 → 文本”。手机写命令需要 Pro 和对应固件支持，读取设备链接不执行设备动作。

字体使用 `setting font info / build / install`。图标显示和日历偏好在 `setting screen mark / calendar`。短蜂鸣器测试使用 `setting buzzer test`。

## 密码与清理

设备密码通过隐藏提示或 `--password-stdin` 输入，不把密码放在命令参数中。默认会保存可复用控制凭据；`--password-stdin` 不代表不缓存。`--no-credential-cache` 禁止读取和写入本机控制凭据，仍保留设备端密码验证与限速。

`setting password status / set / disable` 管理设备密码。`setting reset-settings --yes` 清除普通设置和设备密码，保留图片、Pro、Find 和出厂身份；密码已启用时仍需原密码。`setting clear-user-data --yes` 清除用户图片与播放引用，保留设置、Pro、Find 和出厂身份。`device restart` 只重启。

清理命令可使用 `--expected-device-id` 核验预期设备身份；这是操作前核验，不是设备选择方式。结果未确认时先重连读取状态，不自动重复清理。

## 图片、显示和 OTA

`image process` 离线转换图片，`image algorithms` 查看算法；均不需要设备。上传可指定 `--slot next` 或具体图槽、`--name` 和 `--store-only`，满槽时可显式选择覆盖。批量上传会针对每台设备的实际屏幕处理图片。静态显示不等于设备动态布局。

```sh
inktag-cli device -n B892 display slideshow start --interval 30m
inktag-cli device -n B892 slot list
inktag-cli device ota inspect app.zip
inktag-cli device -n B892 ota update app.zip
inktag-cli device -n B892 recovery enter
inktag-cli device -n B892_OTA recovery exit
```

OTA 使用与主板匹配的已签名包，保留包完整性、签名和更新后身份/状态检查。进入 Recovery 与选择 OTA 包是独立操作。返回应用使用 `recovery exit`（与 `recovery resume-app` 相同）。完整烧录使用独立主控工具，不通过 CLI 代替烧录器。

返回应用也可以用蓝牙短名，例如 `device -n 43D2 recovery exit`；匹配到多台时需指定完整名称或地址。各平台显示相同的连接、请求返回和应用就绪阶段，详细连接诊断用 `--verbose` 查看；`--json` 的原始结果字段保持不变，阶段提示写到 stderr。

## WebUI、本地日志与 Find

```sh
inktag-cli webui serve
inktag-cli webui start --enable-tls
inktag-cli webui status
inktag-cli webui log -f
inktag-cli webui log --service find -f
inktag-cli webui restart --enable-tls
inktag-cli webui stop
inktag-cli findmy identity list
```

`serve` 前台运行，访问日志直接显示在终端；`start` 后台运行，日志写入用户数据目录。启动后自动打开浏览器，可用 `--no-open` 关闭。默认 HTTP，显式提供 `--enable-tls` 或证书参数才使用 HTTPS；重启不隐式继承之前的 TLS 模式。已有服务与请求协议不符时会提示重启。

同一数据目录只运行一个服务，`serve` 与 `start` 共用状态登记；切换 HTTP/HTTPS 使用 `restart`，不需要覆盖证书。若 `status` 提示服务可访问但未登记，请先关闭原来的 `serve` 窗口，再重新启动；只读状态检查不会接管未知进程。

手机访问使用 `webui serve --lan`，自动准备局域网 HTTPS 和访问口令。服务器只记录请求方法、路径、状态码和耗时，不记录请求正文及查询参数。页面的 `#` 路由切换不一定发起 HTTP 请求，因此不一定产生访问日志。

Find 是实验功能。`findmy` 默认识别本机服务实际地址及 HTTPS 证书；其他本地实例可用 `findmy --server URL --ca-cert CA.pem`。不会通过关闭证书验证自动降级连接。账号登录/验证码在 `findmy account` 下，密码和验证码通过隐藏提示输入；`findmy tracking` 管理自动查询及本地历史。CLI 与 WebUI 使用同一份本地会话和身份数据。

## 主机动态页面

模板入口为 `host template`，不再使用顶层 `template`。`host init / import` 创建任务，`host fetch / render / validate` 获取数据、渲染和检查，`host run --once` 可供系统定时任务调用。默认只生成本地预览；显式 `--send` 才发送到设备。自动更新需要主机保持在线并在设备蓝牙范围内。任务绑定实际蓝牙地址及预期设备身份，不能仅依赖名称相同。

更多使用说明见 [InkTag 使用手册](https://solar2ain.github.io/ink-tag-manual/)。许可与第三方声明见项目的许可文件。
