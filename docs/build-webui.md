# 构建 InkTag WebUI

[English](build-webui.en.md)

本页介绍如何在电脑上预览 WebUI 和生成网页产物。直接打开仓库的 [standalone HTML](../webui-standalone/InkTag-WebUI.html) 或使用[在线 WebUI](https://solar2ain.github.io/ink-tag/) 时，可以跳过以下构建步骤。

## 启动开发预览

准备 Git、Node.js 24 和 npm，从仓库根目录进入 `webui/`。用下面的命令启动开发页面：

```sh
cd webui
npm ci
npm run dev -- --host 127.0.0.1
```

在本机 Chrome 或 Edge 打开终端给出的地址，默认是 `http://127.0.0.1:5173/`。按 Ctrl+C 结束。此方式适合改页面、预览以及通过蓝牙管理设备。Find 查询需要 [CLI 本地服务](zh-CN/webui.md#安装与访问)。

## 生成静态站和离线版

在已执行过 `npm ci` 的 `webui/` 目录运行：

```sh
npm run build
```

构建输出位于 `webui/dist/static/` 和 `webui/dist/standalone/`。静态版供 CLI 本地服务和 HTTPS 静态站使用。仓库提供的离线版统一在 [webui-standalone/](../webui-standalone/InkTag-WebUI.html)，可打开其中的 HTML，并保留旁边的 `assets/`。打包候选时会用本地新构建更新这份离线版。普通安装和使用无需 Node.js。

重编后回到仓库根目录，重新运行客户端安装脚本即可启动新版 WebUI。

- Windows 运行 `install_inktag.cmd`。
- macOS／Linux 运行 `sh install_inktag.sh`。

如果只改了网页，可用[客户端脚本](../cli/README.md)运行 `webui restart --repo .`；已激活 CLI 虚拟环境时，直接运行 `inktag-cli webui restart --repo .`。要继续使用 HTTPS，请附上 `--enable-tls` 或原来的证书参数。

## 重建内置页面预览

普通 WebUI 构建会校验并使用仓库随附的预览 WASM。修改共用日历或时钟渲染后，在已激活 [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) 4.0.18 的 Linux、macOS 或 WSL 环境中，从仓库根目录运行以下命令，再重建 WebUI。

```sh
python3 tools/emulator/build-native-font-preview.py --emcc emcc
```

`cli/src/inktag_cli/_assets/nativePreview.inputs.json` 列出对应的 C 源码、构建命令和校验和。WebUI 和 CLI 使用同一份预览产物。

## 相关构建与安装

- CLI 需要 Python 3.11 或更新版本。只开发 CLI 时，可在自己的虚拟环境中从根目录运行 `python -m pip install -e ./cli`。使用 Find 或本地 WebUI 时，按[客户端安装指南](../cli/README.md)准备完整环境。
- 固件构建按主控选择对应的 SDK 和工具链，在仓库根目录运行 `make`，按 `BUILD_TARGET` 选择平台。每个目标的参数、输出位置和版型见[固件构建指南](build-firmware.md)。使用预编译固件时按[烧录指南](zh-CN/flash.md)选正确的 App OTA 或 Full。
