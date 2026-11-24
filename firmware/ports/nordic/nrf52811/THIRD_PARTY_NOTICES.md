# 第三方组件与许可 / Third-party notices

根目录 [LICENSE](LICENSE) 的适用范围以 [NOTICE.md](NOTICE.md) 为准。以下材料保留其原始许可、版权和免责声明；完整条款以各组件的许可文本及源文件声明为准。

Root [LICENSE](LICENSE) applies only as described in [NOTICE.md](NOTICE.md). The following material retains its original license and copyright notices. For the complete terms, refer to each component's license text and source-file notices.

## Adafruit GFX

**中文：** [Adafruit_GFX.c](compat/common/gui/Adafruit_GFX.c) 保留 Adafruit Industries 2013 年的 BSD 风格声明与免责声明，也保留所含例程的 olikraus 2018 年声明。

**English:** [Adafruit_GFX.c](compat/common/gui/Adafruit_GFX.c), Adafruit Industries 2013; BSD-style notice and disclaimer retained in the file. The file also retains olikraus 2018 notices for incorporated routines.

## U8g2 for Adafruit GFX

**中文：** [u8g2_font.c](compat/common/gui/u8g2_font.c) 及[头文件](compat/common/gui/u8g2_font.h) 保留 olikraus 2018 年的 BSD 风格声明与免责声明。

**English:** [u8g2_font.c](compat/common/gui/u8g2_font.c), [header](compat/common/gui/u8g2_font.h); olikraus 2018, BSD-style notice and disclaimer retained.

## Nordic 应用及 BLE 服务组件 / Nordic legacy application / BLE service

**中文：** [平台 main.c](compat/nordic/src/main.c)、[inktag_epd_service.c](compat/common/epd/inktag_epd_service.c) 及[头文件](compat/common/epd/inktag_epd_service.h) 中的 Nordic 2012／2014 年声明援引 Nordic Standard Software License Agreement，须原样保留。

**English:** [platform main.c](compat/nordic/src/main.c), [inktag_epd_service.c](compat/common/epd/inktag_epd_service.c), [header](compat/common/epd/inktag_epd_service.h); Nordic 2012/2014 notices refer to the Nordic Standard Software License Agreement and must remain intact.

## Nordic Recovery 与配置 / Nordic Recovery and configuration

**中文：** [Recovery main.c](app/bootloader/src/main.c)、[分配器头文件](app/bootloader/src/nrf_crypto_allocator.h)、[Recovery sdk_config.h](app/bootloader/src/sdk_config.h) 和[共享 sdk_config.h](compat/nordic/config/sdk_config.h) 保留各自文件中的 Nordic 原始条件，包括 Nordic 芯片使用限制。

**English:** [Recovery main.c](app/bootloader/src/main.c), [allocator header](app/bootloader/src/nrf_crypto_allocator.h), [Recovery sdk_config.h](app/bootloader/src/sdk_config.h), [shared sdk_config.h](compat/nordic/config/sdk_config.h); original Nordic conditions remain in each header, including Nordic-chip restrictions.

## 外部 Nordic SDK／S112／密码库／工具链运行库 / External Nordic SDK / S112 / crypto / toolchain runtime

**中文：** Nordic SDK 源码和工具链需另行取得。预编程 Full 和内部 Flash HEX 包含 Nordic MBR 和 S112 二进制。其使用遵循 Nordic 原许可。依赖版本见 [SDK lock](build-support/sdk/sdk-lock.json)，构建步骤见[编译说明](BUILDING.md)。

**English:** Obtain the Nordic SDK source and compiler toolchain separately. Prebuilt Full packages and internal-flash HEX files include the Nordic MBR and S112 binaries under Nordic’s original terms. See the [SDK lock](build-support/sdk/sdk-lock.json) and [build instructions](BUILDING.md).

## 农历年份表 / Lunar year tables

**中文：** [inktag_calendar.c](compat/common/gui/inktag_calendar.c) 中的 `solar_1_1` 与 `lunar_month_days` 各为 55 个字、220 B。直接来源为 Lunar-Solar-Calendar-Converter 提交 `55d4e547947594736f7c9e4a35521ecc1c7342d8`；保留 isee15 2020 年版权及 [MIT 声明](compat/common/gui/licenses/lunar-calendar-MIT.txt)。相同记录也出现在 EPD-nRF5 中。

**English:** `solar_1_1` and `lunar_month_days`, 55 words / 220 B each in [inktag_calendar.c](compat/common/gui/inktag_calendar.c). [MIT notice](compat/common/gui/licenses/lunar-calendar-MIT.txt), copyright2020 isee15; direct source Lunar-Solar-Calendar-Converter commit55d4e547947594736f7c9e4a35521ecc1c7342d8. The same records also appear in EPD-nRF5.

## 中文历法文字资源 / Chinese calendar text resources

**中文：** 八份定宽文字表 `Lunar_MonthString`、`Lunar_MonthLeapString`、`Lunar_DateString`、`Lunar_DayString`、`Lunar_ZodiacString`、`Lunar_StemStrig`、`Lunar_BranchStrig`、`JieQiStr` 共 648 B，保留 [EPD-nRF5 GUI/Lunar.c](https://github.com/tsl0922/EPD-nRF5/blob/7e3196193997bd6a4644610c41d54a76eff88760/GUI/Lunar.c) 中的条目。当前声明位于 [inktag_calendar.c](compat/common/gui/inktag_calendar.c)；上游 GPLv3 文本保留于 [LICENSES/GPL-3.0.txt](LICENSES/GPL-3.0.txt)。

**English:** Eight fixed-width text tables (`Lunar_MonthString`, `Lunar_MonthLeapString`, `Lunar_DateString`, `Lunar_DayString`, `Lunar_ZodiacString`, `Lunar_StemStrig`, `Lunar_BranchStrig`, `JieQiStr`), 648 B in total, retain the entries in [EPD-nRF5 GUI/Lunar.c](https://github.com/tsl0922/EPD-nRF5/blob/7e3196193997bd6a4644610c41d54a76eff88760/GUI/Lunar.c). Current declarations: [inktag_calendar.c](compat/common/gui/inktag_calendar.c); upstream GPLv3 text retained in [LICENSES/GPL-3.0.txt](LICENSES/GPL-3.0.txt).

## 固定节日查表数据 / Fixed observance lookup data

**中文：** [inktag_gui_default.inc](compat/common/gui/inktag_gui_default.inc) 的 15 条公历和 11 条农历日期／名称记录，保留 [EPD-nRF5 GUI/GUI.c](https://github.com/tsl0922/EPD-nRF5/blob/7e3196193997bd6a4644610c41d54a76eff88760/GUI/GUI.c) 中 `festivals`／`festivals_lunar` 的条目。上游 GPLv3 文本保留于 [LICENSES/GPL-3.0.txt](LICENSES/GPL-3.0.txt)。

**English:** The 15 civil-date and 11 lunar-date/name records in [inktag_gui_default.inc](compat/common/gui/inktag_gui_default.inc) retain the entries of `festivals` / `festivals_lunar` in [EPD-nRF5 GUI/GUI.c](https://github.com/tsl0922/EPD-nRF5/blob/7e3196193997bd6a4644610c41d54a76eff88760/GUI/GUI.c). Upstream GPLv3 text retained in [LICENSES/GPL-3.0.txt](LICENSES/GPL-3.0.txt).

## 节气资源生成依赖 / Solar-term resource generation

**中文：** [InkTag 生成器](build-support/calendar/generate-solar-terms.py) 与[来源记录](build-support/calendar/solar-terms-source.json) 使用 JPL DE440s 和采用 MIT 许可的 [Skyfield 1.53](https://github.com/skyfielders/python-skyfield/tree/1.53) 计算资源。星历核的使用与再分发遵循 [NAIF 规则](https://naif.jpl.nasa.gov/naif/rules.html)；星历核与 Python 包属于外部生成输入。

**English:** [InkTag generator](build-support/calendar/generate-solar-terms.py) and [source record](build-support/calendar/solar-terms-source.json): calculated from JPL DE440s using [Skyfield 1.53](https://github.com/skyfielders/python-skyfield/tree/1.53), MIT. Kernel use and redistribution follow [NAIF rules](https://naif.jpl.nasa.gov/naif/rules.html); the kernel and Python packages are external generation inputs.

## 文泉驿位图字体子集 / WenQuanYi bitmap subsets

**中文：** [inktag_gui_fonts.c](compat/common/gui/inktag_gui_fonts.c) 中的 `u8g2_font_wqy9_t_lunar`（4,315 B）和 `u8g2_font_wqy12_t_lunar`（4,830 B）与 [EPD-nRF5 GUI/fonts.c](https://github.com/tsl0922/EPD-nRF5/blob/7e3196193997bd6a4644610c41d54a76eff88760/GUI/fonts.c) 分发的子集字节相同。保留原字体名称与现有声明；原字体数据许可与图形引擎许可分别适用。

**English:** `u8g2_font_wqy9_t_lunar` (4,315 B) and `u8g2_font_wqy12_t_lunar` (4,830 B) in [inktag_gui_fonts.c](compat/common/gui/inktag_gui_fonts.c) are the same subset bytes distributed in [EPD-nRF5 GUI/fonts.c](https://github.com/tsl0922/EPD-nRF5/blob/7e3196193997bd6a4644610c41d54a76eff88760/GUI/fonts.c). The source font names and existing notices are retained; original font-data terms apply separately from the graphics-engine license.

## Helvetica 位图字体子集 / Helvetica bitmap subsets

**中文：** [inktag_gui_fonts.c](compat/common/gui/inktag_gui_fonts.c) 中的 `u8g2_font_helvB14_tn`（287 B）和 `u8g2_font_helvB18_tn`（389 B）与 [EPD-nRF5 GUI/fonts.c](https://github.com/tsl0922/EPD-nRF5/blob/7e3196193997bd6a4644610c41d54a76eff88760/GUI/fonts.c) 的子集一致。保留源文件中的 Adobe 1984／1987 年、Digital Equipment Corporation 1988／1991 年版权记录，原字体数据条款继续适用。

**English:** `u8g2_font_helvB14_tn` (287 B) and `u8g2_font_helvB18_tn` (389 B) in [inktag_gui_fonts.c](compat/common/gui/inktag_gui_fonts.c) match the subsets in [EPD-nRF5 GUI/fonts.c](https://github.com/tsl0922/EPD-nRF5/blob/7e3196193997bd6a4644610c41d54a76eff88760/GUI/fonts.c). Adobe 1984/1987 and Digital Equipment Corporation 1988/1991 copyright records remain in the source; original font-data terms apply.

## 分发时保留声明 / Retaining notices in distributions

保留源文件中的声明。分发二进制时，须在配套材料中复现每个链接组件要求的版权、条件及免责声明；单独提供路径或链接索引不能替代发行包必须包含的完整声明。

Keep source notices intact. For binary distribution, reproduce the copyright, conditions and disclaimers required by every linked component in the accompanying materials. A path/link index alone is not a substitute for the complete notices required in a release package.
