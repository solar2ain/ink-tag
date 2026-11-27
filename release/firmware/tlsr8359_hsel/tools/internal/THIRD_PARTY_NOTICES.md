# TLSR flashing-tool notices / TLSR 烧录工具第三方说明

本工具目录包含 InkTag 脚本及独立的格式校验模块。InkTag 自有代码的许可见固件全包顶层的 [LICENSE](../../../LICENSE)。镜像格式模块的来源及精确哈希记录在
`python/inktag_programmer_formats/provenance.json` 和 `tools-manifest.json`。
相关第三方代码保留各自的原许可。

The included InkTag scripts and standalone format codecs use the [InkTag license](../../../LICENSE) at the firmware bundle root. The format-codec provenance and exact hashes are
recorded in `python/inktag_programmer_formats/provenance.json` and
`tools-manifest.json`. Third-party code retains its original terms. The tools directory does not bundle MCU firmware or a renderer.

`pvvx/TlsrComSwireWriter` UART/SWS 后端保留原 Unlicense/public-domain 条款。
完整 `LICENSE` 和 `README.md` 与 `TLSR825xComFlasher.py` 一起位于
`vendor/`，依赖的固定哈希由工具清单校验。

The pvvx/TlsrComSwireWriter UART/SWS backend retains its original
Unlicense/public-domain terms. Its complete `LICENSE` and `README.md` accompany
`TLSR825xComFlasher.py` under `vendor/`; the tool
manifest binds the fixed dependency hashes.

Python、pyserial、cryptography、PyYAML、cffi、pycparser、qrcode 和 colorama
是单独安装的主机依赖，不嵌入此工具目录。版本见 `serial/requirements-tools.txt`，
各依赖随发行提供的许可证及 NOTICE 继续适用；工具不绑定某一固件版本。

Python, pyserial, cryptography, PyYAML, cffi, pycparser, qrcode and colorama are
separately installed host prerequisites, not embedded in this tools directory.
Versions are listed in `serial/requirements-tools.txt`; their own distribution licenses
and notices remain applicable. The tools are not bound to a firmware version.
