# 已适配设备

## 主控与设备

| 主控厂商 | 主控 | 已研究／适配的价签系列 |
| --- | --- | --- |
| 深圳市汇顶科技股份有限公司（Goodix） | GR5331、GR5513、GR5515 | ZKC21V、ZKC42V、ZKC42VM |
| 昂瑞微电子（OnMicro） | OM6626 | ZKC21B-NAE4、ZKC42B-NE4、部分 ZKC21V 及 Hanshow 设备 |
| 泰凌微电子（Telink） | TLSR8359 | Hanshow Nowa-154R-N、Nowa-213R-N、Nebular-213R-N、Nebular-420R-N |
| Nordic Semiconductor | nRF52832 | PriceETag；本期不提供固件 |
| Nordic Semiconductor | nRF52811 | AESL／盒马 EL022H2；独立项目，本期不提供固件 |
| Renesas／原 Dialog | DA14585 | AESL-2.9、AESL-4.2／盒马；独立项目，本期不提供固件 |

HS9118 是部分 Hanshow 芯片的封装丝印，已发现不同实际主控，不能仅凭该丝印选择工具或镜像。相同价签型号、主板丝印也可能搭配不同屏幕，应核对实际主控、主板和屏幕。

主控厂商信息见 [Goodix 公司介绍](https://www.goodix.com/zh/about_goodix/profile/overview)。具体屏幕分辨率、颜色、控制器和存储差异见[设备与屏幕](devices.md)。

## 板型与附件状态

请按主控和 PCB 型号选择固件。固件列仅表示本包实际提供的文件，不代表已正式发布或完成实机验证。实验性设备不建议用于量产。未列出固件时，请勿使用相似型号的文件代替。

| 主板 | 主控 | 本次范围 | 状态 | 本次完整烧录 | 本次 OTA | Windows | Linux | macOS |
|---|---|---|---|---:|---:|---|---|---|
| gr5331_zkc21v (`gr5331_zkc21v`) | Goodix GR5331 | 本次候选 | 测试版 | — | — | 已验证 | 待验证 | 待验证 |
| gr5513_zkc42v (`gr5513_zkc42v`) | Goodix GR5513 | 本次候选 | 测试版 | — | — | 已验证 | 待验证 | 待验证 |
| gr5513_zkc42vm (`gr5513_zkc42vm`) | Goodix GR5513 | 本次候选 | 测试版 | — | — | 已验证 | 待验证 | 待验证 |
| gr5515_zkc42vm (`gr5515_zkc42vm`) | Goodix GR5515 | 本次候选 | 测试版 | — | — | 已验证 | 待验证 | 待验证 |
| om6626_zkc21b (`om6626_zkc21b`) | OnMicro OM6626 | 本次候选 | 测试版 | — | — | 已验证 | 待验证 | 待验证 |
| om6626_zkc42b (`om6626_zkc42b`) | OnMicro OM6626 | 本次候选 | 实验性 | — | — | 已验证 | 待验证 | 待验证 |
| tlsr8359_nowa154r (`tlsr8359_nowa154r`) | Telink TLSR8359 | 本次候选 | 实验性 | — | — | 已验证 | 待验证 | 待验证 |
| tlsr8359_nebular420r (`tlsr8359_nebular420r`) | Telink HS9118 / TLSR8359-compatible | 本次候选 | 实验性 | — | — | 已验证 | 待验证 | 待验证 |
| om6626_hsel (`om6626_hsel`) | OnMicro OM6626 | 本次延期 | 实验性 | — | — | 待验证 | 待验证 | 待验证 |
| nrf52811_asel22 (`nrf52811_asel22`) | Nordic nRF52811 | 本次延期 | 测试版 | — | — | 已验证 | 已验证 | 待验证 |
| nrf52832_priceetag (`nrf52832_priceetag`) | Nordic nRF52832 | 本次延期 | 测试版 | — | — | 已验证 | 已验证 | 待验证 |
| da14585_asel29 (`da14585_asel29`) | Renesas/Dialog DA14585 | 本次延期 | 实验性 | — | — | 已验证 | 待验证 | 待验证 |
| da14585_asel42 (`da14585_asel42`) | Renesas/Dialog DA14585 | 本次延期 | 测试版 | — | — | 已验证 | 待验证 | 待验证 |

## 说明

- `gr5331_zkc21v`: 本包未提供此板型的当前固件。
- `gr5513_zkc42v`: 本包未提供此板型的当前固件。
- `gr5513_zkc42vm`: 本包未提供此板型的当前固件。
- `gr5515_zkc42vm`: 本包未提供此板型的当前固件。
- `om6626_zkc21b`: 本包未提供此板型的当前固件。
- `om6626_zkc42b`: 本包未提供此板型的当前固件。
- `tlsr8359_nowa154r`: 本包未提供此板型的当前固件。
- `tlsr8359_nebular420r`: 本包未提供此板型的当前固件。
- `om6626_hsel`: 本包未提供此板型的当前固件。
- `nrf52811_asel22`: 本包未提供此板型的当前固件。
- `nrf52832_priceetag`: 本包未提供此板型的当前固件。
- `da14585_asel29`: 本包未提供此板型的当前固件。
- `da14585_asel42`: 本包未提供此板型的当前固件。
