# PN532／PCR532 传图工具

[English](README.en.md)

支持配有 PN532／PCR532 串口读卡器的电脑。工具自动识别支持 NFC 的 InkTag 高级版 Type 2 和 Type 4 标签。请打开对应读卡器的串口模式，从仓库或完整客户端 ZIP 的根目录运行。需要 Python 3.11 或更新版本。

```powershell
python -m pip install -r tools/nfc/requirements.txt
.\tools\nfc\inktag-nfc.cmd ports
.\tools\nfc\inktag-nfc.cmd send-image "photo.jpg" --port COM18
```

在 macOS／Linux 上，把上面的 `.\tools\nfc\inktag-nfc.cmd` 换成 `python tools/hardware/inktag_nfc.py`，串口名以 `ports` 列出的为准。传图时只贴一个标签，保持位置直到工具提示可移开。

| 命令 | 用途 |
| --- | --- |
| `ports` | 列出电脑可用的串口 |
| `read` | 读取标签的 NFC 链接或文本 |
| `probe` | 查看可传图设备的屏幕尺寸及颜色 |
| `send-image "photo.jpg"` | 转换图片并传给价签，可加 `--wait-refresh` 等待刷屏完成 |
| `send-cmd "日历"` | 发送 NFC 文本命令。支持图片名称的固件也可以直接发送已保存的名称，例如 `send-cmd "日落"` |
| `clear` | 清屏，保留图槽和设备设置 |
| `sync-time` | 用电脑当前时间校时，保留设备时区和当前页面 |

NFC 回执表示设备收到命令。刷新可能还在进行，请以屏幕为准。

InkTag 工具代码采用 GNU AGPL-3.0-only。读卡器依赖的来源和许可见 [PN532 provider](provider/NOTICE.md)。
