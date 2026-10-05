# Supported boards

This table is generated from the private source repository allowlist. Firmware columns list files actually attached to this Release; experimental and future entries are not hardware-qualified releases.

| Board | MCU | Release scope | Status | Full in this release | OTA in this release | Windows | Linux | macOS |
|---|---|---|---|---:|---:|---|---|---|
| ZKC21V (`gr5331_zkc21v`) | Goodix GR5331 | Current candidate | Beta | — | — | Qualified | Pending | Pending |
| ZKC42V v1.2 (GR5513) (`gr5513_zkc42v`) | Goodix GR5513 | Current candidate | Beta | — | — | Qualified | Pending | Pending |
| ZKC42VM v1.2 (GR5513) (`gr5513_zkc42vm`) | Goodix GR5513 | Current candidate | Beta | — | — | Qualified | Pending | Pending |
| ZKC42VM (GR5515) (`gr5515_zkc42vm`) | Goodix GR5515 | Current candidate | Beta | — | — | Qualified | Pending | Pending |
| ZKC21B (`om6626_zkc21b`) | OnMicro OM6626 | Current candidate | Beta | — | — | Qualified | Pending | Pending |
| ZKC42B (`om6626_zkc42b`) | OnMicro OM6626 | Current candidate | Experimental | — | — | Qualified | Pending | Pending |
| Hanshow Nowa 154R-N (`hs_nowa154`) | Telink TLSR8359 | Current candidate | Experimental | — | — | Qualified | Pending | Pending |
| Hanshow Nebular 420R-N (`hs_nebular420`) | Telink HS9118 / TLSR8359-compatible | Current candidate | Experimental | — | — | Qualified | Pending | Pending |
| Alibaba Cloud AESL 2.1/2.9 (`aesl22_nrf`) | Nordic nRF52811 | Deferred | Beta | — | — | Qualified | Qualified | Pending |
| PriceETag 1.54-inch (`nrf52832_priceetag`) | Nordic nRF52832 | Deferred | Beta | — | — | Qualified | Qualified | Pending |
| AESL 2.9-inch (`aesl29_da`) | Renesas/Dialog DA14585 | Deferred | Experimental | — | — | Qualified | Pending | Pending |
| AESL 4.2-inch (`aesl42_da`) | Renesas/Dialog DA14585 | Deferred | Beta | — | — | Qualified | Pending | Pending |

## Notes

- `gr5331_zkc21v`: 0.2 candidate. Full, normal signed OTA, exact-identity reconnect, display/image/Find/activation persistence pass; destructive OTA and signature-rejection matrices remain.
- `gr5513_zkc42v`: Shares the lowest-common-denominator application and OTA with both ZKC42VM variants; NFC/RGB pass here. Long-run UC8176 clock drift, destructive OTA and frozen-artifact regression remain gates.
- `gr5513_zkc42vm`: Reduced ZKC42V variant without NFC/RGB. It uses an exact first-flash container and the shared application/OTA; frozen-artifact SSD1683 regression remains.
- `gr5515_zkc42vm`: Shares the application/OTA with GR5513 variants and has a separate 1 MiB first-flash container. The 224 KiB boundary, signature rejection and interruption matrix need this-chip regression.
- `om6626_zkc21b`: Shares application/OTA with ZKC42B. Full, display, NFC, image/slideshow, normal OTA, one interrupted retransmission and carry-soak baseline pass.
- `om6626_zkc42b`: The 2 MiB auxiliary store passes full-capacity write/read/display and OTA retention; active-write power loss, full interruption/signature matrices and edge optical regression remain.
- `hs_nowa154`: Experimental 0.2 candidate with the shared 192 KiB application/Recovery. Layout-v3 Full, normal/interrupted OTA, RC timebase soak and frozen-artifact regression remain.
- `hs_nebular420`: Experimental 0.2 candidate. Layout-v3, display, high-address records, activation and Find baselines pass. Complete 1 MiB ownership, phone NFC, full-window verification and interrupted OTA remain gates.
- `aesl22_nrf`: Deferred from this release; no new 0.2 firmware is exported. A reduced core capability set will be regressed and released separately.
- `nrf52832_priceetag`: Deferred from this release; no new 0.2 firmware is exported. Current hardware regression remains in the Nordic platform branch.
- `aesl29_da`: Deferred; the complete public screen catalog is restored, while exact-panel, persistence, OTA and power regression remain.
- `aesl42_da`: Deferred; no new 0.2 firmware is exported. The complete public screen catalog is restored, while persistence, display, OTA and power regression remain.
