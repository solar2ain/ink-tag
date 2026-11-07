# Windows tool dependencies

[中文](DEPENDENCIES.md) | English

The tools use frozen private format, signature and QR dependencies from this directory; InkTag CLI is not required. `check.ps1` also exercises the deferred imports used by default configuration and QR encoding.

Use the complete `nrf52811_aesl` tools directory on Windows x64 with Windows PowerShell 5.1 or PowerShell 7. Keep the relative paths intact. The portable Python runtime and the seven pinned archives below are included. No system Python, pip, WSL, Nordic SDK, compiler or private source checkout is needed. Firmware is a separate explicit input.

Install **SEGGER J-Link 7.88j** and its USB driver separately from [SEGGER](https://www.segger.com/downloads/jlink/) under its [terms](https://www.segger.com/products/debug-probes/j-link/tools/terms-of-use/). SEGGER binaries are not redistributed. The tool checks the installed `JLink.exe` and `JLinkARM.dll` against the hashes in `tools/full-dependencies.lock.json`.

Run `powershell -NoProfile -ExecutionPolicy Bypass -File tools/check.ps1` from the tools directory to verify every bundled file and Python import without opening hardware. If the portable runtime is missing, `powershell -NoProfile -ExecutionPolicy Bypass -File tools/setup.ps1` restores it **offline** from `dependencies/`; it does not download files or modify system settings.

The following filenames must remain under `dependencies/`. Each link is the original upstream download source. Sizes are bytes; SHA-256 values are also recorded in `tools/full-dependencies.lock.json`. Do not substitute an unlisted version.

| Archive | Bytes | SHA-256 |
|---|---:|---|
| [cffi-2.1.1-cp313-cp313-win_amd64.whl](https://files.pythonhosted.org/packages/60/a6/8b149b2c3f2e11aaa1618ef64500b45f50f22c57a977a4dff1aff1f91042/cffi-2.1.1-cp313-cp313-win_amd64.whl) | 185688 | `1aa5645c30469b09530c4ebca77ebf8f17618293c58f8549cb1a543a50236e7d` |
| [colorama-0.4.6-py2.py3-none-any.whl](https://files.pythonhosted.org/packages/d1/d6/3965ed04c63042e047cb6a3e6ed1a63a35087b6a609aa3a15ed8ac56c221/colorama-0.4.6-py2.py3-none-any.whl) | 25335 | `4f1d9991f5acc0ca119f9d443620b77f9d6b33703e51011c16baf57afb285fc6` |
| [cryptography-50.0.0-cp311-abi3-win_amd64.whl](https://files.pythonhosted.org/packages/32/2e/c9db68a0c4bfa28e310707527c0ee3a2bd254104d2e02e68f368e197aa4c/cryptography-50.0.0-cp311-abi3-win_amd64.whl) | 3840395 | `bd1c592e4d5974f0d08d4888e432157adba757c66da0246918e43677fafa2d30` |
| [pycparser-3.0-py3-none-any.whl](https://files.pythonhosted.org/packages/0c/c3/44f3fbbfa403ea2a7c779186dc20772604442dde72947e7d01069cbe98e3/pycparser-3.0-py3-none-any.whl) | 48172 | `b727414169a36b7d524c1c3e31839a521725078d7b2ff038656844266160a992` |
| [python-3.13.16-embeddable-amd64.zip](https://www.python.org/ftp/python/3.13.16/python-3.13.16-embeddable-amd64.zip) | 11439788 | `589dc1e9d02549ca5f680710307ff9b77f6e3ffc4aa3efccd7fa54eeeafac94b` |
| [pyyaml-6.0.3-cp313-cp313-win_amd64.whl](https://files.pythonhosted.org/packages/97/c9/39d5b874e8b28845e4ec2202b5da735d0199dbe5b8fb85f91398814a9a46/pyyaml-6.0.3-cp313-cp313-win_amd64.whl) | 154090 | `79005a0d97d5ddabfeeea4cf676af11e647e41d81c9a7722a193022accdb6b7c` |
| [qrcode-8.2-py3-none-any.whl](https://files.pythonhosted.org/packages/dd/b8/d2d6d731733f51684bbf76bf34dab3b70a9148e8f2cef2bb544fccec681a/qrcode-8.2-py3-none-any.whl) | 45986 | `16e64e0716c14960108e85d853062c9e8bba5ca8252c0b4d0231b9df4060ff4f` |

Bundled license files remain with the runtime and installed packages. See `LICENSE`, `LICENSES/`, and `NOTICE.txt`.
