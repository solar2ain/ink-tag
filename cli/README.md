# InkTag 命令行工具

中文 | [English](README.en.md)

## 设备凭据是否保存

默认策略暂不改变：设备认证成功后保存可复用控制凭据，不保存明文密码。
`--password-stdin` 只改变输入方式，**不代表禁止缓存**。需要不落盘时使用：

```sh
inktag --no-credential-cache --password-stdin --device IKT_TL_1234 device info
```

该开关不读取已有控制凭据缓存，也不新建、更新或删除它；凭据只在当前进程内存
中使用，OTA 子进程同样遵守。它不会删除以前的缓存，不影响 Apple 会话或 Find
身份库，也不是对操作系统交换区、终端录制或其他程序的内存保护承诺。

InkTag 自有 CLI 源代码采用 [AGPL-3.0-only](LICENSE)。第三方组件保留原许可；内嵌
编译资源仍需完成对应源码及组合许可核查，见[许可范围](LICENSING.md)。此许可不
自动适用于独立的 WebUI 或固件。

需要 Python 3.11 或以上版本。客户端使用本目录的独立虚拟环境，不修改系统 Python。

Windows：运行 `install.ps1`，再运行 `inktag.ps1 --help`。
Linux 与 macOS：运行 `bash install.sh`，再运行 `bash inktag.sh --help`。

首次安装需要下载 Python 依赖。可以使用扫描返回的准确地址或系统标识选择设备。
普通连接默认校时；走时观察请使用 `--no-time-sync`。OTA 完成还需应用重连与镜像确认。

CLI 在设备密码认证成功后，会自动把可用于再次认证的控制凭据保存到用户 InkTag
数据目录内的 `device-passwords.sqlite3`。它不是明文密码，但仍属于敏感凭据；不要
共享、上传或将该文件放入发布附件。`--password-data-dir` 可指定保存目录；
`--password-stdin` 会改用输入的密码认证，但成功后仍会缓存凭据，并不是“不保存”选项。

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
