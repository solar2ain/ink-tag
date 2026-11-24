# 第三方依赖与资源 / Third-party dependencies and resources

| 依赖／资源 / Component | 用途／保留条款 / Use and retained terms |
| --- | --- |
| Adafruit GFX | 基础绘图；BSD-style，声明保留在 [源文件](compat/common/gui/Adafruit_GFX.c) 和头文件中 |
| u8g2 | 字体绘制；BSD-style，见 [源文件](compat/common/gui/u8g2_font.c) 和 [头文件](compat/common/gui/u8g2_font.h) |
| Lunar-Solar-Calendar-Converter | 两组农历年表，各 55 word／220 B；isee15，MIT，[许可全文](compat/common/gui/licenses/lunar-calendar-MIT.txt) |
| WenQuanYi bitmap 字模 | WQY9：4,315 B；WQY12：4,830 B。保留 [字体文件](compat/common/gui/inktag_gui_fonts.c) 中的原字体名称和声明，字体条款单独适用 |
| Helvetica bitmap 字模 | helvB14：287 B；helvB18：389 B。原 Adobe／Digital Equipment Corporation 版权记录保留在 [字体文件](compat/common/gui/inktag_gui_fonts.c) 中 |
| 历法文字与节日记录 | 8 组文字共 648 B，见 [日历](compat/common/gui/inktag_calendar.c)；15 条公历、11 条农历节日记录见 [显示实现](compat/common/gui/inktag_gui_default.inc)。既有 GPLv3 文本见 [LICENSES](LICENSES/GPL-3.0.txt) |
| 节气生成工具 | [生成器和来源记录](build-support/calendar/README.md)；使用 Skyfield 1.53（MIT）与 JPL DE440s。星历、Python 包为外部生成输入；普通固件编译不需要它们 |
| Renesas/Dialog DA145xx SDK 与 ROM 库 | DA14585 平台依赖；供应商许可，适配文件保留原声明，SDK 单独取得 |
| Keil / Arm Compiler | 外部编译工具链；供应商许可，版本见 [编译说明](BUILDING.md) |

各组件保留原有版权、许可条件和免责声明；具体资源来源见 [SOURCE_NOTICES.md](SOURCE_NOTICES.md)。
Each component retains its original copyright, license conditions and disclaimer.
