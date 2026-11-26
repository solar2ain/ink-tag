# OM6626 Windows 烧录工具

工具版本 **0.2.0.20**。安装 SEGGER J-Link/J-Flash 7.88j 后，在 Windows 上双击 [flash-zh.bat](tools/flash-zh.bat)。工具会自动使用仓库附带的 [OM6626 设备支持文件](device-pack/SOURCE.md)，并按主板丝印提示选择配置。

[烧录与版型说明](../INSTALL.md) · [English](README.en.md)

将 `payload/` 与同级的 `records/` 一同保留。完整刷机前请备份所需资料。运行入口核对随附工具清单，`-DryRun` 只校验。固件地址和完整烧录的数据范围见[地址说明](ADDRESSES.md)。
