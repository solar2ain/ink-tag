#ifndef __FONTS_H
#define __FONTS_H

#include "u8g2_font.h"

/**
 * 文字列表:
所有 ASCII 字符 (32-128)
正月二月三月四月五月六月七月八月九月十月冬月腊月闰
初一初二初三初四初五初六初七初八初九初十
十一十二十三十四十五十六十七十八十九二十
廿一廿二廿三廿四廿五廿六廿七廿八廿九三十
星期一二三四五六日周
猴鸡狗猪鼠牛虎兔龙蛇马羊
庚辛壬癸甲乙丙丁戊己
申酉戌亥子丑寅卯辰巳午未
小寒大寒立春雨水惊蛰春分清明谷雨立夏小满芒种夏至小暑大暑立秋处暑白露秋分寒露霜降立冬小雪大雪冬至
年月日
离还有天
℃
元旦节情人节妇女节植树节愚人节清明节劳动节青年节儿童节建党节建军节教师节国庆节
母亲节父亲节万圣节感恩节平安夜圣诞节
春节元宵节龙抬头端午节七夕节中元节中秋节重阳节寒衣节腊八节除夕
休班
 */
/* Fixed 16-byte resource, not the 24-byte IKFA user-upload container. */
#define INKTAG_GUI_FONT_ASSET_MAGIC 0x44464B49u /* IKFD */
#define INKTAG_GUI_FONT_ASSET_SCHEMA 1u
#define INKTAG_GUI_FONT_ASSET_HEADER_SIZE 16u
#define INKTAG_GUI_FONT_WQY9_SIZE 4315u
#define INKTAG_GUI_FONT_WQY12_SIZE 4830u
#define INKTAG_GUI_FONT_HELVB14_SIZE 287u
#define INKTAG_GUI_FONT_HELVB18_SIZE 389u
#define INKTAG_GUI_DEVICE_QR_SIZE 79u
#define INKTAG_GUI_LUNAR_SOLAR_SIZE 220u
#define INKTAG_GUI_LUNAR_MONTH_SIZE 220u
#define INKTAG_GUI_SOLAR_TERM_SIZE 160u
#define INKTAG_GUI_FONT_ASSET_PAYLOAD_SIZE \
    (INKTAG_GUI_FONT_WQY9_SIZE + INKTAG_GUI_FONT_WQY12_SIZE + \
     INKTAG_GUI_FONT_HELVB14_SIZE + INKTAG_GUI_FONT_HELVB18_SIZE + \
     INKTAG_GUI_DEVICE_QR_SIZE + INKTAG_GUI_LUNAR_SOLAR_SIZE + \
     INKTAG_GUI_LUNAR_MONTH_SIZE + INKTAG_GUI_SOLAR_TERM_SIZE)

#if defined(INKTAG_GUI_ATOMIC_FONT_ASSETS)
#include "inktag_gui_assets.h"
#if defined(INKTAG_GUI_EMBEDDED_FONT_FALLBACK)
extern const uint8_t inktag_gui_fallback_font_wqy9[];
extern const uint8_t inktag_gui_fallback_font_wqy12[];
extern const uint8_t inktag_gui_fallback_font_helvB14[];
extern const uint8_t inktag_gui_fallback_font_helvB18[];
#endif
#define u8g2_font_wqy9_t_lunar inktag_gui_asset_font(0u)
#define u8g2_font_wqy12_t_lunar inktag_gui_asset_font(1u)
#define u8g2_font_helvB14_tn inktag_gui_asset_font(2u)
#define u8g2_font_helvB18_tn inktag_gui_asset_font(3u)
#elif defined(INKTAG_GUI_EXTERNAL_FONT_ASSETS)
#define INKTAG_GUI_FONT_ASSET_VIRTUAL_BASE 0x60000000u
#define INKTAG_GUI_FONT_WQY9_OFFSET INKTAG_GUI_FONT_ASSET_HEADER_SIZE
#define INKTAG_GUI_FONT_WQY12_OFFSET \
    (INKTAG_GUI_FONT_WQY9_OFFSET + INKTAG_GUI_FONT_WQY9_SIZE)
#define INKTAG_GUI_FONT_HELVB14_OFFSET \
    (INKTAG_GUI_FONT_WQY12_OFFSET + INKTAG_GUI_FONT_WQY12_SIZE)
#define INKTAG_GUI_FONT_HELVB18_OFFSET \
    (INKTAG_GUI_FONT_HELVB14_OFFSET + INKTAG_GUI_FONT_HELVB14_SIZE)
#define INKTAG_GUI_DEVICE_QR_OFFSET \
    (INKTAG_GUI_FONT_HELVB18_OFFSET + INKTAG_GUI_FONT_HELVB18_SIZE)
#define INKTAG_GUI_LUNAR_SOLAR_OFFSET \
    (INKTAG_GUI_DEVICE_QR_OFFSET + INKTAG_GUI_DEVICE_QR_SIZE)
#define INKTAG_GUI_LUNAR_MONTH_OFFSET \
    (INKTAG_GUI_LUNAR_SOLAR_OFFSET + INKTAG_GUI_LUNAR_SOLAR_SIZE)
#define INKTAG_GUI_SOLAR_TERM_OFFSET \
    (INKTAG_GUI_LUNAR_MONTH_OFFSET + INKTAG_GUI_LUNAR_MONTH_SIZE)
#define INKTAG_GUI_EXTERNAL_FONT(offset) \
    ((const uint8_t *)(uintptr_t)(INKTAG_GUI_FONT_ASSET_VIRTUAL_BASE + (offset)))
#define u8g2_font_wqy9_t_lunar \
    INKTAG_GUI_EXTERNAL_FONT(INKTAG_GUI_FONT_WQY9_OFFSET)
#define u8g2_font_wqy12_t_lunar \
    INKTAG_GUI_EXTERNAL_FONT(INKTAG_GUI_FONT_WQY12_OFFSET)
#define u8g2_font_helvB14_tn \
    INKTAG_GUI_EXTERNAL_FONT(INKTAG_GUI_FONT_HELVB14_OFFSET)
#define u8g2_font_helvB18_tn \
    INKTAG_GUI_EXTERNAL_FONT(INKTAG_GUI_FONT_HELVB18_OFFSET)
#else
extern const uint8_t u8g2_font_wqy9_t_lunar[] U8G2_FONT_SECTION("u8g2_font_wqy9_t_lunar");
extern const uint8_t u8g2_font_wqy12_t_lunar[] U8G2_FONT_SECTION("u8g2_font_wqy12_t_lunar");

// 以下字库来自 u8g2，用于显示数字
extern const uint8_t u8g2_font_helvB14_tn[] U8G2_FONT_SECTION("u8g2_font_helvB14_tn");
extern const uint8_t u8g2_font_helvB18_tn[] U8G2_FONT_SECTION("u8g2_font_helvB18_tn");
#endif
#endif
