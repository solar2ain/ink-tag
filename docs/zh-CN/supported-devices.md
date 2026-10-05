# 支持的主板

此表由私有源码仓库的发布白名单生成。固件列只表示本次 Release 实际附带的文件；`实验性` 和 `规划中` 不代表已经完成实机验证。

| 主板 | 主控 | 本次范围 | 状态 | 本次 Full | 本次 OTA | Windows | Linux | macOS |
|---|---|---|---|---:|---:|---|---|---|
| ZKC21V (`gr5331_zkc21v`) | Goodix GR5331 | 本次候选 | 测试版 | — | — | 已验证 | 待验证 | 待验证 |
| ZKC42V v1.2 (GR5513) (`gr5513_zkc42v`) | Goodix GR5513 | 本次候选 | 测试版 | — | — | 已验证 | 待验证 | 待验证 |
| ZKC42VM v1.2 (GR5513) (`gr5513_zkc42vm`) | Goodix GR5513 | 本次候选 | 测试版 | — | — | 已验证 | 待验证 | 待验证 |
| ZKC42VM (GR5515) (`gr5515_zkc42vm`) | Goodix GR5515 | 本次候选 | 测试版 | — | — | 已验证 | 待验证 | 待验证 |
| ZKC21B (`om6626_zkc21b`) | OnMicro OM6626 | 本次候选 | 测试版 | — | — | 已验证 | 待验证 | 待验证 |
| ZKC42B (`om6626_zkc42b`) | OnMicro OM6626 | 本次候选 | 实验性 | — | — | 已验证 | 待验证 | 待验证 |
| Hanshow Nowa 154R-N (`hs_nowa154`) | Telink TLSR8359 | 本次候选 | 实验性 | — | — | 已验证 | 待验证 | 待验证 |
| Hanshow Nebular 420R-N (`hs_nebular420`) | Telink HS9118 / TLSR8359-compatible | 本次候选 | 实验性 | — | — | 已验证 | 待验证 | 待验证 |
| Alibaba Cloud AESL 2.1/2.9 (`aesl22_nrf`) | Nordic nRF52811 | 本次延期 | 测试版 | — | — | 已验证 | 已验证 | 待验证 |
| PriceETag 1.54-inch (`nrf52832_priceetag`) | Nordic nRF52832 | 本次延期 | 测试版 | — | — | 已验证 | 已验证 | 待验证 |
| AESL 2.9-inch (`aesl29_da`) | Renesas/Dialog DA14585 | 本次延期 | 实验性 | — | — | 已验证 | 待验证 | 待验证 |
| AESL 4.2-inch (`aesl42_da`) | Renesas/Dialog DA14585 | 本次延期 | 测试版 | — | — | 已验证 | 待验证 | 待验证 |

## 说明

- `gr5331_zkc21v`: 0.2 候选；Full、正常签名 OTA、精确身份重连、显示/图槽/Find/激活保持已通过，破坏性 OTA 与签名拒绝矩阵仍待完成。
- `gr5513_zkc42v`: 与 GR5515/GR5513 ZKC42VM 共用最低公共配置应用和 OTA；本板 NFC/RGB 已实测。长期 UC8176 时钟漂移、破坏性 OTA 与冻结产物复验仍是门禁。
- `gr5513_zkc42vm`: ZKC42V 的精简无 NFC/RGB 变体；使用专用首刷容器与共用应用/OTA，准确 SSD1683 配方的冻结产物回归仍待完成。
- `gr5515_zkc42vm`: 与 GR5513 变体共用应用/OTA；1 MiB 首刷容器独立。224 KiB 布局边界、签名拒绝和中断矩阵需在本芯片复验。
- `om6626_zkc21b`: 与 ZKC42B 共用应用/OTA；Full、显示、NFC、图槽/轮播、正常 OTA、一次中断重传和随身烤机基线已通过。
- `om6626_zkc42b`: 2 MiB 辅助图槽已通过整库写读显示与 OTA 保持；活跃擦写掉电、完整中断/签名矩阵和四边光学复验仍待完成。
- `hs_nowa154`: 0.2 实验候选；共用 192 KiB 应用/Recovery，需完成 layout-v3 Full、正常/中断 OTA、RC 时基烤机和冻结产物复验。
- `hs_nebular420`: 0.2 实验候选；layout-v3、显示、高地址图槽、激活和 Find 基线已测。完整 1 MiB 原始所有权、手机 NFC、全窗验证和中断 OTA 仍是门禁。
- `aesl22_nrf`: 本次延期，不生成 0.2 新固件；后续按明确裁剪的核心能力单独回归和发布。
- `nrf52832_priceetag`: 本次延期，不生成 0.2 新固件；当前实机回归由 Nordic 平台分支独立完成。
- `aesl29_da`: 本次延期；完整公共屏幕目录已恢复，仍需精确屏幕、持久化、OTA 和功耗回归。
- `aesl42_da`: 本次延期，不生成 0.2 新固件；完整公共屏幕目录已恢复，仍需持久化、显示、OTA 和功耗回归。
