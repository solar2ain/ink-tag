# TLSR flashing-tool notices / TLSR 烧录工具第三方说明

本工具目录包含 InkTag 脚本及独立的格式校验模块。项目授权文件随工具保留：
`LICENSE` 和 `cli/LICENSE`；镜像格式模块的来源及精确哈希记录在
`tools/hardware/inktag_programmer_formats/provenance.json` 和 `tools-manifest.json`。
本说明不替换这些文件或第三方项目的原授权。

The included InkTag scripts and standalone format codecs retain the accompanying
`LICENSE` and `cli/LICENSE`. The format-codec provenance and exact hashes are
recorded in `tools/hardware/inktag_programmer_formats/provenance.json` and
`tools-manifest.json`. This notice does not replace those licenses or third-party
terms. The tools directory does not bundle MCU firmware or a renderer.

`pvvx/TlsrComSwireWriter` UART/SWS 后端保留原 Unlicense/public-domain 条款。
完整 `LICENSE` 和 `README.md` 与 `TLSR825xComFlasher.py` 一起位于
`.tools/telink/TlsrComSwireWriter/`，依赖的固定哈希由工具清单校验。

The pvvx/TlsrComSwireWriter UART/SWS backend retains its original
Unlicense/public-domain terms. Its complete `LICENSE` and `README.md` accompany
`TLSR825xComFlasher.py` under `.tools/telink/TlsrComSwireWriter/`; the tool
manifest binds the fixed dependency hashes.

Python、pyserial、cryptography、PyYAML、cffi、pycparser、qrcode 和 colorama
是单独安装的主机依赖，不嵌入此工具目录。版本见 `requirements-tools.txt`，
各依赖随发行提供的许可证及 NOTICE 继续适用；工具不绑定某一固件版本。

Python, pyserial, cryptography, PyYAML, cffi, pycparser, qrcode and colorama are
separately installed host prerequisites, not embedded in this tools directory.
Versions are listed in `requirements-tools.txt`; their own distribution licenses
and notices remain applicable. The tools are not bound to a firmware version.
