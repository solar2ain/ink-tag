# InkTag 0.2.0

InkTag 首版正式发布。通过浏览器或命令行，让电子价签变成日历、时钟、电子相框和随身防丢标签。

[打开在线 WebUI](https://solar2ain.github.io/ink-tag/) · [图文使用手册](https://solar2ain.github.io/ink-tag-manual/) · [玩法合集](https://github.com/solar2ain/ink-tag/blob/main/docs/zh-CN/projects.md)

## 选择使用方式

| 你想做什么 | 下载与说明 |
| --- | --- |
| 直接在线使用 | [打开 WebUI](https://solar2ain.github.io/ink-tag/)，无需安装。 |
| 本地 WebUI、CLI 和 Find 轨迹查询 | 下载 `InkTag-cli-v0.2.0-20261031.2.zip`，需要 Python 3.11 或更新版本。 |
| 离线打开网页 | 完整解压 `InkTag-WebUI-standalone-v1.2.0-20261031.2.zip`，打开 `InkTag-WebUI.html`，保留旁边的资源目录。 |
| 已下载或拉取整个仓库 | 也可直接打开 `webui-standalone/InkTag-WebUI.html`，无需启动本地服务。 |
| 自行托管网页 | 下载 `InkTag-WebUI-static-v1.2.0-20261031.2.zip`。 |

电脑推荐 Chrome／Edge，iPhone／iPad 推荐 [Bluefy](https://apps.apple.com/app/id1492822055)，Android 使用 Chrome。设备连接需要浏览器的蓝牙授权；在线版和离线网页可控制 Find 广播，查询和记录轨迹需要本地客户端。

## 安装本地客户端

安装 Python 3.11+，完整解压 CLI ZIP，在同时包含 `cli/`、`webui/` 和安装脚本的根目录打开终端。拉取整个仓库的用户也在仓库根目录运行。

**Windows PowerShell：**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\install_inktag.ps1
```

**macOS／Linux：**

```sh
sh install_inktag.sh
```

安装脚本会创建独立 Python 环境，安装 CLI 和本地 Find 查询依赖，然后在后台启动服务并打开浏览器。关闭终端不会停止服务。首次安装需要联网；Windows 还需安装完整版 7-Zip，首次会下载约 292 MB 的 Apple 认证组件；macOS 使用系统组件。Linux 的 Find 位置查询暂未完成测试，建议使用 Windows 或 macOS 查询。

macOS 新安装的数据和日志保存在 `~/.inktag/`；已有旧数据目录时继续使用原目录，保留账号会话、标签和轨迹。只安装、不启动可在安装命令末尾加 `--prepare-only`。

### 后续启动、查看状态和停止

在同一根目录运行，客户端脚本会使用已安装的 Python 环境，无需手动激活：

**Windows：**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\cli\inktag.ps1 webui start --repo .
powershell -NoProfile -ExecutionPolicy Bypass -File .\cli\inktag.ps1 webui status
powershell -NoProfile -ExecutionPolicy Bypass -File .\cli\inktag.ps1 webui stop
```

**macOS／Linux：**

```sh
bash cli/inktag.sh webui start --repo .
bash cli/inktag.sh webui status
bash cli/inktag.sh webui stop
```

如安装时指定了数据目录或端口，请使用安装结束时输出的对应命令。Find 轨迹记录需要电脑和本地服务持续运行。更多设置见[本地客户端说明](https://github.com/solar2ain/ink-tag/blob/main/docs/zh-CN/webui.md)。

### 已安装旧版本？

先用原来的客户端执行 `webui stop`，再更新仓库或解压新版完整 CLI ZIP，并重新运行统一安装脚本。保留用户数据目录，不需要删除账号、标签或轨迹。更新客户端不会自动升级设备固件。

### 直接使用 CLI

例如扫描附近设备：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\cli\inktag.ps1 device scan
```

```sh
bash cli/inktag.sh device scan
```

也可以激活 `cli/.venv` 后使用 `inktag-cli`，例如 `inktag-cli device -n B892 info`。激活方法、批量操作和完整命令见 [CLI 手册](https://github.com/solar2ain/ink-tag/blob/main/docs/zh-CN/cli.md)。单独提供的 wheel／sdist 适合只需 CLI 的用户；需要网页和 Find 查询时请选择完整 CLI ZIP。

## 固件安装与 OTA

- **已安装 InkTag**：在 WebUI 固件更新页面连接设备，检查在线更新，或选择匹配的本地 OTA 包，然后开始升级。等待设备重新连接并显示升级结果后再继续操作。
- **原厂设备或有线恢复**：选择与实际主控、主板匹配的完整固件，按对应目录 README 准备烧录器和配置，复制适合主板的一行命令。不要把 OTA 包当作完整固件使用。
- 固件在本 Release 附件和[仓库 firmware 目录](https://github.com/solar2ain/ink-tag/tree/main/firmware)均可取得；每个主板目录包含 `ota/`、`full/` 和 `tools/`。

| 主控 | 固件、工具与烧录说明 |
| --- | --- |
| GR5331 | [gr5331_zkel](https://github.com/solar2ain/ink-tag/tree/main/firmware/gr5331_zkel) |
| GR5513／GR5515 | [gr551x_zkel](https://github.com/solar2ain/ink-tag/tree/main/firmware/gr551x_zkel) · [内部存储版本](https://github.com/solar2ain/ink-tag/tree/main/firmware/gr551x_zkel_internal) |
| OM6626 | [om6626_zkel](https://github.com/solar2ain/ink-tag/tree/main/firmware/om6626_zkel) |
| TLSR8359 | [tlsr8359_hsel](https://github.com/solar2ain/ink-tag/tree/main/firmware/tlsr8359_hsel) |
| nRF52832 | [nrf52832_priceetag](https://github.com/solar2ain/ink-tag/tree/main/firmware/nrf52832_priceetag) |
| nRF52811 | [nrf52811_aesl](https://github.com/solar2ain/ink-tag/tree/main/firmware/nrf52811_aesl) |
| DA14585 | [da14585_aesl](https://github.com/solar2ain/ink-tag/tree/main/firmware/da14585_aesl) |

烧录工具独立于 CLI，不会自动选择或下载固件。**本次 nRF52832 和 DA14585 便携工具尚未开放实际完整烧录**，可用操作请以各自 README 为准；OTA 不受这一工具限制影响。首次使用前请查看[适配设备](https://github.com/solar2ain/ink-tag/blob/main/docs/zh-CN/supported-devices.md)、[已实测屏幕](https://github.com/solar2ain/ink-tag/blob/main/docs/zh-CN/tested-screens.md)和[烧录说明](https://github.com/solar2ain/ink-tag/blob/main/docs/zh-CN/flash.md)。

## 本次客户端更新

- 统一安装脚本自动准备 Find 查询依赖，并在后台启动本地服务。
- 安装完成后给出启动、停止、状态和日志命令。
- 改善苹果账号登录失败时的提示，帮助检查账号、普通登录密码及连接情况。

下载后可使用 `SHA256SUMS.txt` 核对文件。Find 为实验功能；请妥善保管导出的标签信息、轨迹备份和口令。功能与使用范围见 [README](https://github.com/solar2ain/ink-tag#readme)和[许可说明](https://github.com/solar2ain/ink-tag/blob/main/LICENSING.md)。

---

## English

InkTag 0.2.0 is the first stable release. Turn compatible electronic shelf labels into calendars, clocks, photo frames and portable Find tags.

[Open WebUI](https://solar2ain.github.io/ink-tag/) · [English guide](https://solar2ain.github.io/ink-tag/manual/en/index.html)

### Choose your download

- **Online:** open WebUI without installing anything. Use Chrome/Edge on desktop, Bluefy on iPhone/iPad, or Chrome on Android.
- **Local WebUI, CLI and Find queries:** download `InkTag-cli-v0.2.0-20261031.2.zip`. Python 3.11+ is required.
- **Offline browser use:** fully extract `InkTag-WebUI-standalone-v1.2.0-20261031.2.zip`, then open `InkTag-WebUI.html`, keeping its resources alongside it. A full repository download also includes `webui-standalone/InkTag-WebUI.html`.
- **Self-hosting:** use `InkTag-WebUI-static-v1.2.0-20261031.2.zip`.

### Install and run locally

Open a terminal in the extracted root containing `cli/`, `webui/` and the installer. On Windows run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\install_inktag.ps1
```

On macOS/Linux run:

```sh
sh install_inktag.sh
```

The installer prepares separate Python environments for CLI and Find, starts the services in the background and opens the browser. Closing the terminal leaves them running. First setup needs internet access. Windows also needs full 7-Zip and downloads approximately 292 MB of Apple components; macOS uses system components. Find location queries on Linux have not completed testing; use Windows or macOS for queries. New macOS profiles use `~/.inktag/`; existing profiles keep their previous data directory. Add `--prepare-only` to install without starting services.

For later use, run `webui start --repo .`, `webui status` or `webui stop` through `cli/inktag.ps1` or `bash cli/inktag.sh`, as shown above. Use the exact commands printed by the installer if you selected a custom profile. Keep the computer and local services running to record Find locations; online and standalone WebUI can control Find broadcasts but cannot query locations themselves.

To update, stop the old services first, update the repository or extract the new complete CLI ZIP, then rerun the installer. Keep your data directory to retain accounts, tags and tracks. For CLI commands, use the launcher scripts directly or activate `cli/.venv` and run `inktag-cli`. The separate wheel/sdist is for CLI-only installations. See the [CLI guide](https://github.com/solar2ain/ink-tag/blob/main/docs/en/cli.md) and [local WebUI guide](https://github.com/solar2ain/ink-tag/blob/main/docs/en/webui.md).

### Firmware and programmer tools

Existing InkTag devices can check for updates in WebUI or select a matching local OTA file. Wait for reconnection and the upgrade result. First installation or wired recovery needs a matching full image, programmer and board configuration; an OTA file is not a full image. The controller links above contain firmware, plaintext tools and bilingual instructions.

Programmer tools are independent of CLI and require an explicitly selected firmware file. **The nRF52832 and DA14585 portable tools do not currently enable actual full flashing**; follow their READMEs for available operations. This restriction does not affect OTA. Consult [supported devices](https://github.com/solar2ain/ink-tag/blob/main/docs/en/supported-devices.md), [tested screens](https://github.com/solar2ain/ink-tag/blob/main/docs/en/tested-screens.md) and the [flashing guide](https://github.com/solar2ain/ink-tag/blob/main/docs/en/flash.md).

This client update improves dependency setup, background startup and Apple sign-in error guidance. Verify downloads with `SHA256SUMS.txt`. Find is experimental; protect exported tag information, track backups and passwords. See [licensing](https://github.com/solar2ain/ink-tag/blob/main/LICENSING.md) for usage terms.
