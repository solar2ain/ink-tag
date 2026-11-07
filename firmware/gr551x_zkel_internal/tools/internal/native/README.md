# InkTag GR 独立工具包

这是不含固件的 **tools-only 明文目录**。适用目标、工具版本、依赖及文件哈希见
`inktag-goodix-tools.json`。保留完整目录结构；无需 Git、固件源码、SDK、编译器
或已安装的 InkTag CLI。

完整目录说明、配置选择和完整刷机步骤统一见[刷机文档](../../docs/zh-CN/flash.md)。
独立下载目录也可查阅[在线刷机文档](https://github.com/solar2ain/ink-tag/blob/main/docs/zh-CN/flash.md)。
本目录中的操作参数可通过 `tools/setup.ps1`、`check.ps1`、`probe.ps1`、
`backup.ps1`、`flash.ps1` 的 `-h` 或 `--help` 查看。

## 离线依赖

支持 Windows x64、CPython 3.12 x64、Windows PowerShell 5.1 和 PowerShell 7。
包内提供锁定的 Python wheels；setup 在包内建立隔离环境，不修改系统 Python。
setup/check 不启动设备操作，也不安装 USB 驱动或修改全局执行策略。

原生程序不随包再分发。请自行从官方取得并接受许可：

- [SEGGER J-Link 7.88j](https://www.segger.com/downloads/jlink/)。
- [Goodix GProgrammer console 2.0.3.0](https://www.goodix.com/en/software_tool/gprogrammer_ble)，
  保留其配套 JLinkARM.dll 6.18c 与 loader/config 文件。

所需版本、精确哈希和 setup 参数见 `tools/internal/dependencies-windows-x64.lock.json`
及 setup 帮助。其他同名程序或版本不会自动获得兼容资格。驱动按厂商说明手动安装。

## 使用边界

probe/backup 不需要固件。flash 必须显式传入实际 PCB/屏幕对应的 `-Config`
和外部兼容签名完整固件包 `-Firmware`；本包不会默认选择、下载或绑定一版固件。
缺少必要参数时，即使带 `-Yes` 也会在设备访问前拒绝。固件的签名、目标、容量、
布局和 App/Recovery 配对检查继续执行；具体清理与保留范围见统一刷机文档和运行提示。

DryRun 和离线验收不代表实板成功。Full 使用现有受保护后端，工具更新不新增其他
板型或原厂恢复资格；`restore.ps1` 保留关闭保护。未知写入结果应先检查，不能重复执行。

第三方组件保留原许可与版权；详见 `LICENSE`、`THIRD_PARTY_NOTICES.md` 及各依赖文件。
