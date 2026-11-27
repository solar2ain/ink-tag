# 第三方组件与许可 / Third-party components and licenses

PN532／PCR532 NFC 工具自带的读卡器依赖保留 GNU GPL v3，来源及修改见 [PN532 provider](tools/nfc/provider/NOTICE.md)。 The bundled PN532 provider retains GNU GPL v3. Its origin and modifications are listed in the provider notice.

InkTag 使用下列第三方组件。它们保留各自版权和许可，不因 InkTag 的项目许可而改用另一种许可。适用范围见[许可说明](LICENSING.md)。

| 组件 | 许可 | 许可正文 |
| --- | --- | --- |
| @noble/curves 2.0.1 / @noble/hashes 2.0.1 | MIT | [curves](webui/licenses/noble-curves-2.0.1-LICENSE.txt)、[hashes](webui/licenses/noble-hashes-2.0.1-LICENSE.txt) |
| React / React DOM 19.2.8 | MIT | [React](webui/licenses/react-19.2.8-LICENSE.txt)、[React DOM](webui/licenses/react-dom-19.2.8-LICENSE.txt) |
| React Router 7.18.2 | MIT | [React Router](webui/licenses/react-router-7.18.2-LICENSE.txt) |
| qrcode 1.5.4 | MIT | [License](webui/licenses/qrcode-1.5.4-LICENSE.txt) |
| zustand 5.0.15 | MIT | [License](webui/licenses/zustand-5.0.15-LICENSE.txt) |
| dijkstrajs 1.0.3 | MIT | [License](webui/licenses/dijkstrajs-1.0.3-LICENSE.txt) |
| scheduler 0.27.0 | MIT | [License](webui/licenses/scheduler-0.27.0-LICENSE.txt) |
| Dexie 4.4.5 | Apache-2.0 | [License](webui/licenses/dexie-4.4.5-LICENSE.txt)、[NOTICE](webui/licenses/dexie-4.4.5-NOTICE.txt) |
| Leafer UI 2.2.10 | MIT | [Leafer UI](webui/licenses/leafer-ui-2.2.10-LICENSE.txt) |
| RgbQuant.js | MIT | [RgbQuant.js](webui/licenses/RgbQuant-MIT.txt) |
| Lucide，含 Feather 衍生图标 | ISC / MIT | [Lucide](webui/licenses/Lucide-LICENSE.txt) |
| epdoptimize | Apache-2.0 | [epdoptimize](webui/licenses/epdoptimize-Apache-2.0.txt) |
| Ma Shan Zheng 字体 | SIL Open Font License 1.1 | [字体许可](webui/licenses/MaShanZheng-OFL.txt) |

CLI 的 Python 依赖及附带组件见 [CLI 第三方声明](cli/THIRD_PARTY_NOTICES.md)。完整 CLI 包中 Find 查询组件的许可随包提供。

部分插件、字体和图片的许可信息尚未齐备，本表不代表全部素材都已取得再分发许可。第三方许可原文保留其原有语言和内容。

The components listed above retain their own copyright and licenses. InkTag project terms do not relicense them. See the linked license texts and [CLI third-party notices](cli/THIRD_PARTY_NOTICES.md). The complete CLI bundle also supplies the license for its Find query component.

License information for some plugins, fonts and images is not yet complete; this list does not grant redistribution rights to every asset. Original third-party license texts are retained in their original form and language.

EPD-nRF5（GPL-3.0）是 InkTag 的早期参考。DA14585 和 nRF52811 仍保留部分来自上游的历法和字模资源，各项来源与许可见端口声明。[DA14585](firmware/ports/dialog/da14585/SOURCE_NOTICES.md) · [nRF52811](firmware/ports/nordic/nrf52811/THIRD_PARTY_NOTICES.md)

EPD-nRF5 (GPL-3.0) was an early reference for InkTag. See the port notices for the sources and terms of the calendar and bitmap font resources retained by DA14585 and nRF52811. [DA14585](firmware/ports/dialog/da14585/SOURCE_NOTICES.md) · [nRF52811](firmware/ports/nordic/nrf52811/THIRD_PARTY_NOTICES.md)

## 创作字体 / Creation fonts

| 字体 / Font | 许可 / License | 原文与声明 / License text and notices |
| --- | --- | --- |
| 缝合像素 / Fusion Pixel Font | SIL OFL 1.1 | [Fusion Pixel and upstream notices](webui/src/assets/fonts/FusionPixel-NOTICES.txt) |
| Z 工坊像素黑体 CN / Z Labs Pixel CN | SIL OFL 1.1 | [Z Labs Pixel CN](webui/src/assets/fonts/OFL-ZLabsPixel.txt) |
| 目哉像素 / Muzai Pixel | SIL OFL 1.1 | [Muzai Pixel](webui/src/assets/fonts/OFL-MuzaiPixel.txt) |
| 寒蝉点阵体 / Chill Bitmap 7x | 随附 SIL OFL 1.1 及字形来源声明 / SIL OFL 1.1 and accompanying notices | [上游 OFL](webui/src/assets/fonts/OFL-ChillBitmap.txt) · [7x 发行声明](webui/src/assets/fonts/ChillBitmap7x-LICENSE.txt) |
| 文泉驿点阵宋体 / WenQuanYi Bitmap Song | GPL-2.0，附字体嵌入例外 / GPL-2.0 with the upstream font embedding exception | [许可与来源](https://sources.debian.org/copyright/license/xfonts-wqy/1.0.0~rc1-8/) · [BDF 源文件与许可](webui/public/assets/wenquanyi-bitmap-song-source.tar.gz) |

[字体版本、校验和与重建入口 / Versions, checksums and rebuilding](webui/src/assets/fonts/README.md)
