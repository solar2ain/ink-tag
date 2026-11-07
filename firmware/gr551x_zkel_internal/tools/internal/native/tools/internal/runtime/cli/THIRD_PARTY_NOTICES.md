# CLI 第三方组件 / CLI third-party components

InkTag 自有 CLI 代码采用 PolyForm Noncommercial 1.0.0。第三方组件保留各自的版权和许可，不附加 InkTag 非商用限制；项目许可正文见 [LICENSE](LICENSE)，适用范围见 [LICENSING.md](LICENSING.md)。

- **epdoptimize 1.3.0**：Apache-2.0。完整正文见 `licenses/EPDOPTIMIZE-LICENSE.txt`。
- **Python 依赖**：安装时会下载 bleak、cryptography、mini-racer、Pillow、qrcode、rich、typer 及其依赖。各包的许可随对应 Python 包提供。
- **图形和字体组件**：当前包含有 JavaScript 组件及编译后的渲染模块。编译组件的完整对应源码和相关素材许可信息尚未齐备，见 `licenses/COMPILED-RESOURCES.txt`。

InkTag-owned CLI code uses PolyForm Noncommercial 1.0.0. Third-party works retain their own copyrights and licenses without additional NonCommercial restrictions; see [LICENSE](LICENSE) and [LICENSING.md](LICENSING.md).

- **epdoptimize 1.3.0**: Apache-2.0; full text in `licenses/EPDOPTIMIZE-LICENSE.txt`.
- **Python dependencies**: installation downloads bleak, cryptography, mini-racer, Pillow, qrcode, rich, typer and their dependencies. Each Python package supplies its own license information.
- **Graphics and font components**: JavaScript components and a compiled renderer are included. Complete Corresponding Source and some asset-license information are not yet supplied; see `licenses/COMPILED-RESOURCES.txt`.

- **Lunar-Solar-Calendar-Converter**：原生预览中的农历年表来自 isee15 的 MIT 授权数据；许可正文见 `licenses/lunar-calendar-MIT.txt`。
- **Lunar-Solar-Calendar-Converter**: the native preview uses lunar year records by isee15 under MIT; full text in `licenses/lunar-calendar-MIT.txt`.

- **节气日期 / Solar-term dates**：使用固定 JPL DE440s 星历及 Skyfield 1.53 按 UTC+08:00 独立生成；不是运行时联网查询，也不是从旧项目表中提取。来源：[JPL](https://naif.jpl.nasa.gov/pub/naif/generic_kernels/spk/planets/)、[Skyfield](https://rhodesmill.org/skyfield/almanac.html#solar-terms)。The dates are generated offline from pinned ephemeris inputs; astronomy packages are generation tools, not CLI runtime dependencies.
- **保留的内置字体 / Retained bitmap fonts**：`u8g2_font_wqy9_t_lunar`、`u8g2_font_wqy12_t_lunar`、`u8g2_font_helvB14_tn`、`u8g2_font_helvB18_tn` 保留现有数据及原字体条款；这些子集也见于 [EPD-nRF5 的字体文件](https://github.com/tsl0922/EPD-nRF5/blob/7e3196193997bd6a4644610c41d54a76eff88760/GUI/fonts.c)。Their existing font copyrights and terms remain separate from the renderer and this solar-term replacement.
