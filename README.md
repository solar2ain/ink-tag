# InkTag Releases

[English](README.en.md)

`solar2ain/ink-tag` 是 InkTag 的发布仓库：允许 Python CLI 源码、编译后的 Web 上位机、已发布固件附件与中英用户文档。严禁固件源码、未编译 WebUI 源码、厂商 SDK、签名私钥和原厂备份。

> 当前仓库处于私有预览阶段。完成逐板实机回归、升级回退测试和第三方许可审计后，才会转为公开发布。

## 本次包含什么

每次发布可以只包含 CLI、WebUI 或经过验证的特定平台固件。请先查看
[`firmware/manifest.json`](firmware/manifest.json)：其中的 `components` 是本次发布组件，
主板的 `artifacts` 为空就表示本次没有发布该固件。`firmware/` 中的主板目录可以只是
后续固件的占位文档，不能把目录存在误认为已有可烧录文件。

## 获取固件

实际发布的固件放在 GitHub Releases，不直接提交到 Git 历史。请选择与“主控 + 主板型号”完全一致的文件：

- `InkTag-<主板>-v<版本>-full.bin|hex|zip`：首次烧录或救砖使用，会覆盖设备固件；OM6626 ZIP 内含固定顺序使用的三份 HEX。
- `InkTag-<主板>-v<版本>-ota.zip`：在 InkTag Web 中升级使用。
- `InkTag-flash-tools-v<版本>.zip`：Windows 一键首刷入口；仍需按手册安装厂商工具。
- `SHA256SUMS.txt`：下载后校验文件完整性。

不要把名称相近但主板不同的固件互刷。支持状态见[支持的主板](docs/zh-CN/supported-devices.md)。

## 使用入口

- `webui/`：GitHub Pages 的部署内容；私有预览期间请从对应 GitHub Release 下载
  `InkTag-WebUI-standalone-*.html`，仓库公开或账户支持私有 Pages 后再启用在线站点
- `webui/manual/`：随 GitHub Pages 一起部署的中英文操作手册
- `cli/`：统一 Python CLI 源码；默认使用中文文档，英文入口见 README.en.md
- 微信小程序：尚未提供合格编译产物；不会以研究源码冒充发布版本
- [Web 上位机使用手册](docs/zh-CN/webui.md)
- [完整烧录说明](docs/zh-CN/flashing.md)
- [OTA 升级说明](docs/zh-CN/ota.md)
- [命令行工具](docs/zh-CN/cli.md)
- [常见问题](docs/zh-CN/faq.md)
- [版本与发布策略](docs/zh-CN/releases.md)
- `flash/windows/`：Goodix、OM6626、Telink 候选主板的一键烧录入口；脚本先核对 Release manifest 与 SHA-256

## 安全提示

首次烧录前请备份原厂固件，并核对供电、电压、SWD/串口接线及主板型号。刷错完整固件可能导致设备无法启动或损坏外设。

GitHub Pages 部署配置与启用说明：[GitHub Pages](docs/zh-CN/github-pages.md).
