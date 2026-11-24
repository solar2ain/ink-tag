# GR base 本地构建

base 入口使用 Python 标准库和外部 Goodix SDK、Arm GNU 工具链，不需要
Git 历史、InkTag CLI、发布生产器、签名器或私钥。保留所附许可证和第三方声明。
SDK 由使用者另行取得并遵守其许可；此源码包不附带 SDK。实际 BLE 静态库的
SHA256 必须符合 [依赖清单](base-sdk-dependencies.json)。本批使用 Linux
Arm GNU Toolchain 15.2.Rel1；Windows 通过 WSL 调用该工具链。

从源码根目录执行，例如：

```sh
python3 tools/sdk/build-goodix-base.py \
  --build-target gr5331_zkel \
  --sdk-root /path/to/owned-gr533x-sdk \
  --toolchain-root /path/to/arm-gnu-toolchain \
  --output /path/to/new-output
```

另外两个构建目标是 `gr551x_zkel` 和 `gr551x_zkel_internal`，均使用匹配的
GR551x SDK。internal 是独立可执行布局，必须明确选择。SDK 工作目录须独占；
工具不会抢占已有构建锁。输出目录必须尚不存在，已有文件不会被覆盖。

Windows 也可直接用 Windows Python 调用同一脚本，参数使用绝对本地盘路径；
脚本会通过 WSL 编译。各平台目录的 `Makefile.base` 提供相同入口，接受
`SDK_ROOT`、`TOOLCHAIN_ROOT` 和 `OUTPUT` 参数。

输出包含：

- `app-ota.zip`：base OTA 包；原生传输由实际 App、40 字节 IKCI 完整性记录
  和 48 字节 Goodix image-info 组成。
- `application.bin`：与 OTA 中一致的原生传输内容。
- `recovery.bin`：对应的 Recovery 1.0.1、兼容 epoch1、layout-v1。
- `app-full.zip`：独立的 `inktag-gr-base-full-v1` 首装包，绑定同一次构建的
  App、Recovery、默认 GUI 字体和原生初始化配方；不是签名 Pro Full 格式。
- `package-review.json`：实际 App/Recovery 大小、SHA256 和 ELF 校验结果。
- `source-profile.json`：实际选中的源码及物化文件散列。

现有设备只有在同一连接中实际报告支持
CRC-only 策略后，才能通过维护的共享 CLI/WebUI 使用无签名 base OTA。
旧的要求签名的 Recovery 不接受这种包；不能仅凭软件版本或文件名判断。
本批不会为安装 base 而自动替换设备 Recovery。

`tools/sdk/prepare-goodix-base-full.py --package <app-full.zip>` 可离线检查包；
增加 `--config <准确板配置.toml> --output <新目录>` 可生成离线布局核对文件。
该文件没有物理身份回读证明，不能直接作为刷机入口。配置必须选择真实板型，
internal 布局仅匹配 GR5513 的 2.13 英寸板。默认 GUI 位于当前原生布局的
32 KiB 字体分区，包含在 Full 内，不需要 InkTag CLI 或系统字体生成。

正常烧录推荐使用所选主板 `gr5331/tools/flash.bat` 或 `gr551x/tools/flash.bat`。
无参数时按编号选择随附主板配置，再输入外部 `app-full.zip`，核对摘要后输入 `FLASH`。
它先尝试保全可用原地址、厂商普通项及经当次 UID 验证的同机原授权；缺失或无效的
可选记录采用默认初始化。基础版仅保存合格原授权，不启用 Pro；普通设置、密码、Find、
图片索引及健康记录重置。实际连接、芯片/容量、写入或回读失败仍停止，不重放写入。

`generate-bin.bat` 生成通用初始化物料；`flash-bin.bat` 写入已核物料。GUI 字体已嵌入
`mcu.bin`，新物料无需另烧 GUI 文件。用户入口使用系统 PowerShell 5.1/.NET 和既有
原生 Goodix/J-Link，不运行 Python；源码构建依赖不变。完整参数、地址和保全范围见
[烧录说明](clean-flashing.md)。原 Python 首装后端保留其原有门槛和运行依赖。
本批只有离线/假后端验证，不代表已做新的 GR 实机 Full 测试。

base 保留统一的命令编号、请求和事务格式。缺少实现的 Pro 命令返回
`UNSUPPORTED`，不触发存储、显示、无线或激活写入。普通设备 ID 基础保留，
存量密码的普通 V AUTH、限速和异常记录拒绝保留；创建、修改、停用和密码显示属于 Pro。NFC、Find、局刷及 Pro 激活实现不在 base 编译闭包中。

## English

Run `tools/sdk/build-goodix-base.py` with an explicit build target, an owned
external SDK, the Linux Arm GNU 15.2.Rel1 toolchain and a new output directory.
Windows Python uses WSL automatically. Python standard library is sufficient;
Git, the InkTag CLI, a Pro producer, signing tools and private keys are not
build dependencies. Keep the supplied licenses and third-party notices.

Targets are `gr5331_zkel`, `gr551x_zkel` and the explicitly selected distinct
layout `gr551x_zkel_internal`. The external SDK BLE archive hash is pinned in
`base-sdk-dependencies.json`. Existing SDK locks and output files are preserved.

The output contains an unsigned base App OTA, its separately identified Recovery,
and a distinct deterministic `inktag-gr-base-full-v1` first-install package with
the native default GUI and initialization recipe. The maintained
CLI/WebUI must attest the connected Recovery's explicit CRC-only policy before
transferring an unsigned App; software version alone cannot authorize it.
Required-signature older Recoveries remain incompatible with unsigned base OTA.
The build does not install or replace Recovery.

Normal programming uses `gr5331/tools/flash.bat` or `gr551x/tools/flash.bat`.
Without arguments, choose a numbered board and the external Full ZIP, then
review the summary and type FLASH. Usable original information is preserved
when verified; missing/invalid optional records use defaults. Base stores but
ignores a verified original same-unit entitlement. User settings, password,
Find, image index and health state reset. Real connection/capacity/write/readback
failures stop without replaying writes.

`generate-bin.bat` prepares generic materials; `flash-bin.bat` writes them.
GUI is already embedded in `mcu.bin`. Native Windows entries use system
PowerShell 5.1/.NET and existing Goodix/J-Link, without Python. Build dependencies
remain unchanged. See [flashing instructions](clean-flashing.md). The legacy
Python backend retains its existing admission and runtime dependencies.
This batch has offline/fake-backend validation, not new physical Full qualification.

The shared protocol IDs, requests and transactions remain intact. Pro commands
without an implementation return `UNSUPPORTED` without side effects. Ordinary
device identity and ordinary V AUTH for retained password records remain available,
including throttling and fail-closed reads. Password creation, change, disable and
display management remain Pro-only.
