# InkTag 0.2.1

新增部分 4.2 寸屏幕的 Pro 时钟局刷适配，改进 Find 标签管理，并修复日历显示、设备更新和本地客户端使用中的问题。

[图文使用手册](https://solar2ain.github.io/ink-tag-manual/) · [打开 WebUI](https://solar2ain.github.io/ink-tag/) · [使用本地客户端](https://github.com/solar2ain/ink-tag/blob/main/docs/zh-CN/webui.md)

## 新增功能（Feature）

- **4.2 寸 Pro 时钟局刷**：新增 SSD1683、SSD1619／SSD1619A、UC8276 的 400×300 黑白红屏幕适配，平台与设备范围如下。

  | 平台 | 主板／设备 | 适配屏幕 |
  | --- | --- | --- |
  | Goodix GR5513／GR5515 | ZKEL 标准版主板，使用 `gr551x_zkel` 固件；对应 ZKC42V／ZKC42VM 的兼容配置 | SSD1683、SSD1619／SSD1619A、UC8276，4.2 寸 400×300 黑白红 |
  | Telink TLSR8359 | 汉朔 HSEL 主板，使用 `tlsr8359_hsel` 固件；包括匹配 SSD1683 配置的 Nebular-420R-N 三色装配及兼容换屏主板 | SSD1683、SSD1619／SSD1619A、UC8276，4.2 寸 400×300 黑白红 |
  | Nordic nRF52832 | PriceETag 主板，使用 `nrf52832_priceetag` 固件并改接兼容屏幕 | SSD1683、SSD1619／SSD1619A、UC8276，4.2 寸 400×300 黑白红 |

  需要激活 Pro，并匹配屏幕接线和配置；这项功能仅用于时钟数字区域。不同屏幕的残影和显示效果可能不同，具体实测搭配见[已实测屏幕](https://github.com/solar2ain/ink-tag/blob/main/docs/zh-CN/tested-screens.md)。本次新增的 4.2 寸局刷不适用于 OM6626、GR5331、GR5513 内置图片版、nRF52811 或 DA14585。

- **Find 标签管理**：点击已登记标签即可切换当前标签并查看最新位置；导出增加全选、清空选择，默认不选中。单台标签或不含轨迹的资料支持复制为字符串。
- **设备测试**：按设备能力提供时间设置、日期偏移和电池电压测试。
- **自编译固件更新**：nRF52811、DA14585 开源版支持未签名 App OTA，需先安装允许未签名更新的开源 Recovery；已有签名的固件仍需通过签名验证，官方固件的验证方式保持不变。

## 问题修复（Bug Fix）

- **节气日期**：更新节气日期数据，改善日历中的节气显示。
- **日历显示**：改善方屏、2.66 寸和 4.2 寸屏幕的排版，修复部分文字截断、内容拥挤和六行月份显示不协调的问题。
- **Find 地图与身份管理**：修复部分环境下单点地图不显示、最新位置与所选标签不同步的问题；完善身份删除与私钥备份提醒。
- **OTA 与恢复模式**：改善设备更新后的重连、状态显示和自动校时；CLI 退出恢复模式统一默认等待上限为 90 秒。
- **本地客户端安装与启动**：完善 Windows 的 7-Zip 依赖及数据目录访问权限提示，修复后台启动日志中文乱码。

> 更新时请选择匹配主控与主板的 OTA 固件，保持供电并等待设备返回。App OTA 不会替换 Recovery；nRF52811、DA14585 的独立显示资源也需要配套有线更新。首次安装及有线恢复请阅读对应主板的烧录说明。

---

English

Adds Pro clock partial-refresh support for selected 4.2-inch displays, improves Find tag management, and fixes calendar, device-update and local-client issues.

## Feature

- **Pro clock partial refresh on 4.2-inch displays**: adds support for 400×300 black/white/red panels using SSD1683, SSD1619/SSD1619A or UC8276 on the following platforms.

  | Platform | Boards/devices | Adapted displays |
  | --- | --- | --- |
  | Goodix GR5513/GR5515 | Standard ZKEL boards using `gr551x_zkel`, with compatible ZKC42V/ZKC42VM configurations | SSD1683, SSD1619/SSD1619A, UC8276; 4.2-inch, 400×300, black/white/red |
  | Telink TLSR8359 | Hanshow HSEL boards using `tlsr8359_hsel`, including the three-color Nebular-420R-N with its matching SSD1683 profile and compatible replacement-screen boards | SSD1683, SSD1619/SSD1619A, UC8276; 4.2-inch, 400×300, black/white/red |
  | Nordic nRF52832 | PriceETag boards using `nrf52832_priceetag`, rewired for a compatible display | SSD1683, SSD1619/SSD1619A, UC8276; 4.2-inch, 400×300, black/white/red |

  Pro activation and matching wiring/configuration are required. This feature updates the clock digits only. Ghosting and visual quality vary by panel; see [tested displays](https://github.com/solar2ain/ink-tag/blob/main/docs/en/tested-screens.md) for tested combinations. This new 4.2-inch support does not apply to OM6626, GR5331, the GR5513 internal-image build, nRF52811 or DA14585.

- **Find tag management**: select a registered tag by clicking its row to view its latest location. Export adds select-all and clear-selection controls, with nothing selected by default. A single tag or an export without tracks can be copied as text.
- **Device tests**: time-setting, date-offset and battery-voltage tests are available according to device capabilities.
- **Self-built firmware updates**: open-source nRF52811 and DA14585 builds support unsigned App OTA with an installed open-source Recovery that permits it. Signed images still require a valid signature; official firmware verification is unchanged.

## Bug Fix

- **Solar-term dates**: update solar-term date data used by the calendar.
- **Calendar display**: improve square, 2.66-inch and 4.2-inch layouts, addressing clipped text, crowded content and uneven six-row month layouts.
- **Find maps and identity management**: fix missing single-point maps and latest-location panels showing the wrong tag; improve identity-deletion and private-key backup guidance.
- **OTA and Recovery**: improve reconnection, status information and clock synchronization after updates. CLI Recovery exit now has a consistent 90-second default wait limit.
- **Local-client setup and startup**: improve Windows guidance for 7-Zip and data-directory permissions, and fix garbled Chinese in background-startup logs.

> Select the OTA firmware matching your controller and board, keep power connected and wait for the application to return. App OTA does not replace Recovery; the separate display resources on nRF52811 and DA14585 also require a matching wired update. Follow the board's flashing guide for first installation or wired recovery.
