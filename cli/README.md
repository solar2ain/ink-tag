# InkTag 命令行工具

中文 | [English](README.en.md)

需要 Python 3.11 或以上版本。客户端使用本目录的独立虚拟环境，不修改系统 Python。

Windows：运行 `install.ps1`，再运行 `inktag.ps1 --help`。
Linux 与 macOS：运行 `bash install.sh`，再运行 `bash inktag.sh --help`。

首次安装需要下载 Python 依赖。可以使用扫描返回的准确地址或系统标识选择设备。
普通连接默认校时；走时观察请使用 `--no-time-sync`。OTA 完成还需应用重连与镜像确认。

完整发布目录中的操作说明位于 `docs/zh-CN/cli.md`，也可阅读
[在线手册](https://solar2ain.github.io/ink-tag/manual/zh-CN/cli.html)。
## 画板天气站与主机任务

在 WebUI「创作 → 天气站」编辑并设置数据源，点击「导出主机任务」。然后运行：

```console
inktag host import weather.inktag.json --config weather.toml
inktag host render --scene weather.inktag.json --demo -o preview.png
inktag host fetch weather.toml --output forecast.json
inktag host render --scene weather.inktag.json --snapshot forecast.json -o weather.png
```

原稿文件需保留，任务默认关闭、未绑定设备。详细配置、设备绑定与系统定时方法见发行目录
`docs/zh-CN/host-pages.md` 或[在线手册](https://solar2ain.github.io/ink-tag/manual/zh-CN/host-pages.html)。
字体使用主机本地文件，必要时添加 `--font-file`；检查主机预览后再启用任务。

也可使用 CLI 内置排版：

```console
inktag host templates
inktag host render weather-paper --demo --size 400x300 -o preview.png
inktag host locations 杭州 --language zh
inktag host init weather.toml --latitude 30.27 --longitude 120.15 --label 杭州
inktag host validate weather.toml
inktag host run weather.toml --once
```

任务默认关闭，编辑 TOML 并查看预览后再启用。运行器默认只生成本机图片，只有显式
`--send` 才允许按准确设备身份推送；推送仍待实板验证。省略 `--once` 可持续运行。
任务使用主机蓝牙地址绑定，名称仅作显示。`inktag --device IKT_TL_1234 host target`
可读取绑定信息，不校时或推送。Windows/Linux 使用 MAC，macOS 使用本机 CoreBluetooth UUID。
主机须保持运行，Open-Meteo 免费接口适用于非商业用途。完整用法见发行版
`docs/zh-CN/cli.md`。WebUI 负责编辑与导出，CLI 负责定时运行，不会自动开始推送。
