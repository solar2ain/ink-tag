# InkTag 命令行工具

每个带 CLI 的 GitHub Release 都包含 InkTag-cli-v版本.zip。它需要 Python 3.11
或更新版本，并支持 Windows、Linux 和 macOS。

下载 CLI ZIP 和同一版本的 SHA256SUMS.txt，校验摘要后安装：

```text
python -m pip install ./InkTag-cli-vVERSION.zip
inktag --help
```

常用入口：

```text
inktag device scan
inktag --device InkTag_OM_ device info
inktag display calendar
inktag display stop
inktag slot list
inktag slideshow start --interval 5m
inktag ota inspect InkTag-board-vVERSION-ota.zip
inktag ota update InkTag-board-vVERSION-ota.zip
```

多个设备匹配时必须使用更完整的 --device 或 --address。macOS 显示的 address
通常是 CoreBluetooth UUID。读取命令可加 --json；删除图槽和 OTA 在脚本中必须
加 --yes。

CLI OTA 会验证固件包，再选择 Nordic Secure DFU、Goodix OTAS v2、OnMicro DFU
v3 或 Dialog SUOTA。它不能代替首次烧录和原厂备份。
