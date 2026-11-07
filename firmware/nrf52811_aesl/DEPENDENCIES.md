# Windows 工具依赖

中文 | [English](DEPENDENCIES.en.md)

工具使用目录内固定的私有格式编码器、验签和二维码依赖，不需要安装 InkTag CLI。`check.ps1` 也会验证默认配置和二维码编码所需的延迟依赖。

在 Windows x64 上使用完整的 `nrf52811_aesl` 工具目录，支持 Windows PowerShell 5.1 和 PowerShell 7。请保持目录结构。工具已包含便携 Python 运行环境及下列七个固定版本归档，无需系统 Python、pip、WSL、Nordic SDK、编译器或私有源码。固件需作为独立输入指定。

请从 [SEGGER](https://www.segger.com/downloads/jlink/) 单独安装 **J-Link 7.88j** 及 USB 驱动，并遵守其[使用条款](https://www.segger.com/products/debug-probes/j-link/tools/terms-of-use/)。工具不重新分发 SEGGER 程序；使用前会核对已安装的 `JLink.exe` 和 `JLinkARM.dll`，对应哈希见 `tools/full-dependencies.lock.json`。

在工具目录运行 `powershell -NoProfile -ExecutionPolicy Bypass -File tools/check.ps1`，可检查全部文件及 Python 依赖，不打开硬件。便携运行环境缺失时，运行 `powershell -NoProfile -ExecutionPolicy Bypass -File tools/setup.ps1`，从 `dependencies/` **离线**恢复，不下载文件、不修改系统设置。

下列文件需保留在 `dependencies/`。链接指向原始上游下载来源；大小单位为字节，SHA-256 同时记录在 `tools/full-dependencies.lock.json` 中，不可直接替换为其他版本。

| 归档 | 大小 | SHA-256 |
|---|---:|---|
| [cffi-2.1.1-cp313-cp313-win_amd64.whl](https://files.pythonhosted.org/packages/60/a6/8b149b2c3f2e11aaa1618ef64500b45f50f22c57a977a4dff1aff1f91042/cffi-2.1.1-cp313-cp313-win_amd64.whl) | 185688 | `1aa5645c30469b09530c4ebca77ebf8f17618293c58f8549cb1a543a50236e7d` |
| [colorama-0.4.6-py2.py3-none-any.whl](https://files.pythonhosted.org/packages/d1/d6/3965ed04c63042e047cb6a3e6ed1a63a35087b6a609aa3a15ed8ac56c221/colorama-0.4.6-py2.py3-none-any.whl) | 25335 | `4f1d9991f5acc0ca119f9d443620b77f9d6b33703e51011c16baf57afb285fc6` |
| [cryptography-50.0.0-cp311-abi3-win_amd64.whl](https://files.pythonhosted.org/packages/32/2e/c9db68a0c4bfa28e310707527c0ee3a2bd254104d2e02e68f368e197aa4c/cryptography-50.0.0-cp311-abi3-win_amd64.whl) | 3840395 | `bd1c592e4d5974f0d08d4888e432157adba757c66da0246918e43677fafa2d30` |
| [pycparser-3.0-py3-none-any.whl](https://files.pythonhosted.org/packages/0c/c3/44f3fbbfa403ea2a7c779186dc20772604442dde72947e7d01069cbe98e3/pycparser-3.0-py3-none-any.whl) | 48172 | `b727414169a36b7d524c1c3e31839a521725078d7b2ff038656844266160a992` |
| [python-3.13.16-embeddable-amd64.zip](https://www.python.org/ftp/python/3.13.16/python-3.13.16-embeddable-amd64.zip) | 11439788 | `589dc1e9d02549ca5f680710307ff9b77f6e3ffc4aa3efccd7fa54eeeafac94b` |
| [pyyaml-6.0.3-cp313-cp313-win_amd64.whl](https://files.pythonhosted.org/packages/97/c9/39d5b874e8b28845e4ec2202b5da735d0199dbe5b8fb85f91398814a9a46/pyyaml-6.0.3-cp313-cp313-win_amd64.whl) | 154090 | `79005a0d97d5ddabfeeea4cf676af11e647e41d81c9a7722a193022accdb6b7c` |
| [qrcode-8.2-py3-none-any.whl](https://files.pythonhosted.org/packages/dd/b8/d2d6d731733f51684bbf76bf34dab3b70a9148e8f2cef2bb544fccec681a/qrcode-8.2-py3-none-any.whl) | 45986 | `16e64e0716c14960108e85d853062c9e8bba5ca8252c0b4d0231b9df4060ff4f` |

随附许可保留在运行环境及各依赖包中，另见 `LICENSE`、`LICENSES/` 和 `NOTICE.txt`。
