# InkTag CLI 与本地 WebUI

中文 | [English](README.en.md)

本包包含命令行客户端、本地 WebUI 和 Find 查询服务，不需要另下本地服务包。蓝牙可由浏览器或 CLI 连接。以下命令均从同时包含 `cli/` 和 `webui/` 的解压根目录运行。

## 安装、运行与 Find 记录

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

首次安装需要联网。macOS 使用系统原生认证组件，无需 7-Zip 或下面的 Apple 镜像。Linux 的位置查询仍未完成资格验证。

**Windows 安装准备：**

1. 从 [7-Zip 官网](https://www.7-zip.org/download.html)下载适合系统的 Windows `.exe` 安装程序（通常为 x64；ARM 电脑选择 ARM64），安装完整版，保留默认目录 `C:\Program Files\7-Zip`。不要只下载 `7zr.exe`。
2. 安装 7-Zip 时按 Windows 提示授予管理员权限；没有权限时，请使用管理员账户完成安装。InkTag 首次准备 Find 查询服务时，会用其中的 `7z.exe` 解压约 292 MB、经过校验的 Apple 认证组件镜像，不会安装 iTunes。组件已完整缓存后无需再次解压。
3. 然后运行上面的 InkTag 安装命令。若出现“拒绝访问”或 `WinError 5`，使用有权访问该数据目录的管理员账户，右键 PowerShell，选择“以管理员身份运行”，再进入 InkTag 目录重新执行安装命令。请核对报错中的账户和路径；不要删除原有数据目录。切换账户或使用新的 `--data-dir` 会使用另一份资料。

正常使用本人可写的数据目录时，无需一直以管理员身份运行 InkTag。

只安装、不启动：Windows 运行 `powershell -NoProfile -ExecutionPolicy Bypass -File .\install_inktag.ps1 --prepare-only`；macOS／Linux 运行 `sh install_inktag.sh --prepare-only`。安装完成后，下面两种使用方式任选一种，命令参数完全相同。

#### 方式一：直接运行客户端脚本（无需激活环境）

在仓库／完整 CLI 包根目录运行。脚本会使用 `cli/.venv` 中的 Python，不需要把 `inktag-cli` 加到系统 PATH。

**Windows PowerShell：**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\cli\inktag.ps1 device scan
```

**macOS／Linux：**

```sh
bash cli/inktag.sh device scan
```

其他操作只需把末尾的 `device scan` 换成相应命令，例如 `device -n B892 info`。查看帮助用 `--help`。如果还没有 `cli/.venv`，先运行上面的安装脚本；客户端脚本本身不安装 Python。

#### 方式二：激活 Python 环境后运行 `inktag-cli`

使用前面的统一安装脚本准备好 `cli/.venv`，再在当前终端激活：

**Windows PowerShell：**

```powershell
. .\cli\.venv\Scripts\Activate.ps1
inktag-cli device scan
```

**macOS／Linux：**

```sh
source cli/.venv/bin/activate
inktag-cli device scan
```

每个新终端需重新激活，退出用 `deactivate`。如果系统限制激活脚本，直接使用方式一，不必修改系统执行策略。

只需要 CLI、打算自行安装 Python 环境时，可先安装 Python 3.11+，再按 [CLI 手册](docs/zh-CN/cli.md)创建虚拟环境并安装 `cli/`；需要 Find 查询时推荐使用统一安装脚本，以便一并准备查询依赖。更新后重新运行安装脚本即可。

下面用 `inktag-cli` 表示已激活环境的写法；使用方式一时，替换命令开头即可：

```console
inktag-cli device --ble-name IKT_TL_1234 info
inktag-cli display --ble-name IKT_TL_1234 calendar
inktag-cli image --ble-name IKT_TL_1234 upload photo.jpg --slot next --name photo
inktag-cli device --ble-name IKT_TL_1234 slot list
inktag-cli device --ble-name IKT_TL_1234 display slideshow start --interval 5m
inktag-cli device --ble-name IKT_TL_1234 display slideshow stop
```

将蓝牙名称和图片路径替换为实际内容。操作同一设备前，先断开它在 WebUI 中的连接。更多命令、密码缓存及故障排查见 [CLI 手册](docs/zh-CN/cli.md)。

### 本地服务：开启 Find 设备轨迹记录

本仓库与完整 CLI ZIP 均包含本地 WebUI 和 Find 查询服务，无需另外下载服务包。首次使用运行统一安装脚本；后续仍使用同一套客户端命令启动：

**Windows：**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\cli\inktag.ps1 webui start --repo . --enable-tls
```

**macOS／Linux：**

```sh
bash cli/inktag.sh webui start --repo . --enable-tls
```

已经激活 Python 环境时，也可运行：

```console
inktag-cli webui start --repo . --enable-tls
```

打开本地 WebUI 的 Find 页面，登录查询账号，登记标签或导入已有标签信息，点击“开启记录”。保持本地服务及网络连接，服务会按设定间隔查询并保存收到的位置报告；不保证每次查询都有新位置。安装及手机局域网访问见 [WebUI 使用说明](docs/zh-CN/webui.md)，功能说明见 [Find 查询与标签迁移](docs/zh-CN/find.md)。

## 手机与局域网访问

将上面的 `webui start --repo . --enable-tls` 换成 `webui serve --repo . --lan` 即可通过同一客户端脚本开启局域网访问。已激活环境时运行：

```console
inktag-cli webui serve --repo . --lan
```

启动窗口会显示 HTTPS 地址、公共证书和本次访问密码，用户名为 `inktag`。手机连接同一局域网，并信任自己电脑生成的公共证书。iPhone／iPad 推荐 Bluefy，Android 首选 Chrome。自定义监听、证书和数据目录等参数见 `inktag-cli webui start --help`；未激活时用客户端脚本替换命令开头。

## 数据与会话

默认数据目录为 Windows `%LOCALAPPDATA%/InkTag`、macOS `~/.inktag/`（已有安装继续使用原目录）、Linux `$XDG_DATA_HOME/inktag`（未设置时为 `~/.local/share/inktag`）。需要指定目录时，给 `webui start` 传入绝对路径 `--data-dir`，后续启动继续使用同一目录。

重新启动不会清除标签、轨迹和同机加密会话；Apple 会话仍可能到期，届时重新登录，不必重新登记标签。换电脑请在 Find 页面导出／导入标签，不能只复制认证文件。静态站点和 standalone 不提供位置查询后端。

检查和停止服务：已激活环境时运行 `inktag-cli webui status`、`inktag-cli webui stop`；未激活时用同一客户端脚本替换开头。更新后重新运行统一安装入口。
