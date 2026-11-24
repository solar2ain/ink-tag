#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "inktag_epd_screen.h"

static epd_screen_status_t resolve(uint8_t driver, uint8_t recipe,
                                   uint8_t color, uint16_t width,
                                   uint16_t height) {
    epd_screen_config_t config;
    epd_screen_t screen;
    memset(&config, 0, sizeof(config));
    config.schema_version = EPD_SCREEN_SCHEMA_VERSION;
    config.driver_id = driver;
    config.panel_recipe = recipe;
    config.color_id = color;
    config.width = width;
    config.height = height;
    config.visible_width = width;
    config.visible_height = height;
    return epd_screen_resolve(&config, &screen);
}

int main(void) {
    /* SES's two named profiles and its standard geometry alias are retired
     * together; Nowa-213 uses UC8XXX and remains a separate retained path. */
    for (uint8_t recipe = 0u; recipe <= 2u; ++recipe) {
        assert(resolve(EPD_SCREEN_DRIVER_UC8151, recipe,
                       EPD_SCREEN_COLOR_BWR, 104u, 212u) ==
               EPD_SCREEN_STATUS_UNSUPPORTED_DRIVER);
        assert(resolve(EPD_SCREEN_DRIVER_UC8151, recipe,
                       EPD_SCREEN_COLOR_BWR, 152u, 296u) ==
               EPD_SCREEN_STATUS_UNSUPPORTED_DRIVER);
    }
    assert(resolve(EPD_SCREEN_DRIVER_SSD1677, INKTAG_RECIPE_SSD1677_STANDARD,
                   EPD_SCREEN_COLOR_GRAY4, 800u, 480u) ==
           EPD_SCREEN_STATUS_UNSUPPORTED_COMBINATION);
    assert(resolve(EPD_SCREEN_DRIVER_SSD1677, INKTAG_RECIPE_SSD1677_STANDARD,
                   EPD_SCREEN_COLOR_BW, 800u, 480u) == EPD_SCREEN_STATUS_OK);
    assert(resolve(EPD_SCREEN_DRIVER_SSD1677, INKTAG_RECIPE_SSD1677_STANDARD,
                   EPD_SCREEN_COLOR_BWR, 800u, 480u) == EPD_SCREEN_STATUS_OK);
    assert(resolve(EPD_SCREEN_DRIVER_UC8159,
                   INKTAG_RECIPE_UC8159_STANDARD,
                   EPD_SCREEN_COLOR_BWR, 400u, 300u) ==
           EPD_SCREEN_STATUS_UNSUPPORTED_DRIVER);
    /* Actual/candidate AESL paths and the retained swap catalog. */
    assert(resolve(EPD_SCREEN_DRIVER_SSD1619,
                   INKTAG_RECIPE_SSD1619_STANDARD,
                   EPD_SCREEN_COLOR_BWR, 128u, 296u) ==
           EPD_SCREEN_STATUS_OK);
    assert(resolve(EPD_SCREEN_DRIVER_SSD1619,
                   INKTAG_RECIPE_SSD1619_HEMA_213_BWR,
                   EPD_SCREEN_COLOR_BWR, 128u, 250u) ==
           EPD_SCREEN_STATUS_OK);
    assert(resolve(EPD_SCREEN_DRIVER_SSD1619,
                   INKTAG_RECIPE_SSD1619_HEMA_213_BWR,
                   EPD_SCREEN_COLOR_BW, 128u, 250u) ==
           EPD_SCREEN_STATUS_UNSUPPORTED_COMBINATION);
    assert(resolve(EPD_SCREEN_DRIVER_UC8176,
                   INKTAG_RECIPE_UC8176_STANDARD,
                   EPD_SCREEN_COLOR_BWR, 400u, 300u) ==
           EPD_SCREEN_STATUS_OK);
    assert(resolve(EPD_SCREEN_DRIVER_JD79668AA,
                   INKTAG_RECIPE_JD79668AA_HANSHOW_NEBULAR_420R_N,
                   EPD_SCREEN_COLOR_BWRY, 400u, 300u) ==
           EPD_SCREEN_STATUS_OK);
    assert(resolve(EPD_SCREEN_DRIVER_JD79665AA,
                   INKTAG_RECIPE_JD79665AA_HUAWEI_398_A0,
                   EPD_SCREEN_COLOR_BWRY, 768u, 552u) ==
           EPD_SCREEN_STATUS_OK);
    assert(resolve(EPD_SCREEN_DRIVER_JD79665AA,
                   INKTAG_RECIPE_JD79665AA_HUAWEI_398_A1,
                   EPD_SCREEN_COLOR_BWRY, 768u, 552u) ==
           EPD_SCREEN_STATUS_OK);
    assert(resolve(EPD_SCREEN_DRIVER_JD79665CA,
                   INKTAG_RECIPE_JD79665CA_LENMORY_H364F067_E6,
                   EPD_SCREEN_COLOR_BWRYGB, 760u, 568u) ==
           EPD_SCREEN_STATUS_UNSUPPORTED_DRIVER);

    /* Preserve ordinary InkTag geometry while the dedicated SSD1675 path
     * stays excluded by this constrained target's established build. */
    assert(resolve(EPD_SCREEN_DRIVER_SSD1675,
                   INKTAG_RECIPE_SSD1675_STANDARD,
                   EPD_SCREEN_COLOR_BW, 152u, 152u) ==
           EPD_SCREEN_STATUS_UNSUPPORTED_DRIVER);
    assert(resolve(EPD_SCREEN_DRIVER_SSD1675,
                   INKTAG_RECIPE_SSD1675_HANSHOW_NOWA_154R_N,
                   EPD_SCREEN_COLOR_BWR, 152u, 152u) ==
           EPD_SCREEN_STATUS_UNSUPPORTED_DRIVER);
    assert(resolve(EPD_SCREEN_DRIVER_UC8XXX,
                   INKTAG_RECIPE_UC8XXX_STANDARD,
                   EPD_SCREEN_COLOR_BW, 200u, 200u) ==
           EPD_SCREEN_STATUS_OK);
    epd_screen_config_t nowa = {
        .schema_version = EPD_SCREEN_SCHEMA_VERSION,
        .driver_id = EPD_SCREEN_DRIVER_UC8XXX,
        .panel_recipe = INKTAG_RECIPE_UC8XXX_HANSHOW_NOWA_213R_N,
        .color_id = EPD_SCREEN_COLOR_BWR, .width = 128u, .height = 250u,
        .visible_width = 122u, .visible_height = 250u,
    };
    epd_screen_t screen;
    assert(epd_screen_resolve(&nowa, &screen) == EPD_SCREEN_STATUS_OK);

    puts("nRF52811 retained screen catalog and explicit omission tests passed");
    return 0;
}
