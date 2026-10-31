# 主板配置

中文 | [English](README.en.md)

`EL022HWRL.toml` 用于 nRF52811 EL022HWRL 主板（CAL_VER06）和原 2.13 英寸黑白红屏；`EL022HWRL-A0.toml` 用于同一主板连接 A0 屏。

完整工具包默认使用此配置。在源码工作区可显式指定：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/ports/nordic/nrf52811/tools/flash.ps1 -Firmware firmware-full.zip -Config firmware/ports/nordic/nrf52811/tools/configs/EL022HWRL.toml -DryRun
```

此命令只校验完整固件与配置，不连接探针。同一个预编译完整固件包可在烧录时写入不同的屏幕与 RGB 引脚配置，无需重新编译。已有设备可通过应用 OTA 接入引脚配置，保留有效旧设置；应用 OTA 不会建立或替换出厂配置。

`[gpio.display]` 可配置 MOSI、SCLK（`clock`）、CS、DC、RESET、BUSY 和供电 EN，格式为 `P0.xx`。前六路必须全部连接或全部设为 `NONE`；EN 可选，关闭屏幕时也必须为 `NONE`。所有已连接引脚必须互不重复，避开低频晶振、外部 Flash、BS 和复位等保留资源。屏幕信号电平由驱动和配方决定；`enable_active_low` 为必填布尔值，仅配置 EN 的极性（`false` 高有效，`true` 低有效）。屏幕部分不接受旧的 `active_low` 列表。

设备依次使用 WebUI 保存的用户覆盖、完整烧录写入的出厂默认值、工厂记录缺失时的主板固定值。WebUI 可读取默认与当前配置、修改引脚以及恢复默认接线。修改成功后立即生效并持久保存；显示、传图或灯测试进行中不能更换接线。清除设置会移除用户引脚覆盖并恢复默认配置；清除图片保留引脚配置。应用 OTA 保留已有的出厂与用户接线配置。损坏或无法读取的记录不会被当成缺失记录。

`[gpio.led]` 可配置红、绿、蓝三路引脚及各自的 `active_low` 极性。未连接的颜色设为 `NONE`，并从 `active_low` 中移除。默认配置符合 EL022HWRL 的原装接线；Recovery 灯效使用已验证的主板固定接线。工具继续校验芯片身份、实际容量、签名和回读结果。

字段结构与主库 Nordic 配置一致：`[screen]`、`[gpio.display]` 和 `[gpio.led]`。`note = "CAL_VER06"` 记录本板备注；主控、固件家族及原生编码由工具根据丝印生成。
