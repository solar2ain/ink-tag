# CLI 许可 / CLI licensing

InkTag 自有 CLI 源码采用 [GNU AGPL-3.0-only](LICENSE)。第三方组件保留原有版权和许可，
见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 与 `licenses/`。

内置日历、时钟和字库预览的公共源码位于仓库的 `firmware/common/gui/`，构建入口为
`tools/emulator/build-native-font-preview.py`。字体及 Adafruit GFX、u8g2 等组件遵循各自条款。
Pro 固件与私有动态布局引擎的许可独立，不因 CLI 开源而变更。

InkTag-owned CLI source is licensed under [GNU AGPL-3.0-only](LICENSE). Third-party
components retain their respective copyrights and licenses; see the notices and
`licenses/`. Built-in calendar, clock and font-preview sources are provided under
`firmware/common/gui/`, with the host build entry under `tools/emulator/`.
Pro firmware and the private dynamic-layout engine have separate terms.
