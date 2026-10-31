# 下载与文件校验

中文 | [English](../en/releases.md)

从项目的 [GitHub Releases](https://github.com/solar2ain/ink-tag/releases) 下载所需文件并阅读该版本说明。尚未列出附件时，表示暂不可下载。本版提供 WebUI、Python CLI、使用手册、已适配主板的 OTA 与完整固件，以及独立的明文烧录工具。烧录工具位于仓库的 `firmware/<主控_主板>/`；执行烧录时必须另行提供匹配的完整固件。

WebUI、CLI 和设备固件的版本可以不同，不必一起升级。离线 ZIP 可直接打开，静态 ZIP 适合自行托管，CLI 本地版用于本机部署；CLI 安装见[CLI 使用说明](cli.md)。

## 核对下载是否完整

将下载文件的 SHA256 与同版本 `SHA256SUMS.txt` 对照：

```powershell
Get-FileHash -Algorithm SHA256 文件名
```

Linux 使用 `sha256sum 文件名`，macOS 使用 `shasum -a 256 文件名`。摘要不同、下载中断或文件被修改时，重新下载，不要继续安装。

校验和不能代替发布者签名。安装设备固件时，须核对准确主控、板型及兼容性，并保留客户端和设备的签名检查。没有对应附件时不要使用名称相近的其他型号文件。
