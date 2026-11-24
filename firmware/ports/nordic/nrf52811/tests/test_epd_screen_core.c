#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "inktag_epd_screen.h"

static epd_screen_status_t resolve(uint8_t driver, uint8_t recipe, uint8_t color,
                                   uint16_t width, uint16_t height) {
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
    assert(resolve(EPD_SCREEN_DRIVER_UC8XXX, 0, EPD_SCREEN_COLOR_BWR, 400, 300) == EPD_SCREEN_STATUS_OK);
    assert(resolve(EPD_SCREEN_DRIVER_UC8151, 0, EPD_SCREEN_COLOR_BWR, 104, 212) == EPD_SCREEN_STATUS_UNSUPPORTED_DRIVER);
    assert(resolve(EPD_SCREEN_DRIVER_UC8176, 0, EPD_SCREEN_COLOR_BWR, 400, 300) == EPD_SCREEN_STATUS_OK);
    assert(resolve(EPD_SCREEN_DRIVER_SSD1619, 0, EPD_SCREEN_COLOR_BWR, 128, 296) == EPD_SCREEN_STATUS_OK);
    assert(resolve(EPD_SCREEN_DRIVER_SSD1680, 0, EPD_SCREEN_COLOR_BWR, 128, 250) == EPD_SCREEN_STATUS_OK);

    assert(resolve(EPD_SCREEN_DRIVER_UC8159, 0, EPD_SCREEN_COLOR_BWR, 640, 384) == EPD_SCREEN_STATUS_UNSUPPORTED_DRIVER);
    assert(resolve(EPD_SCREEN_DRIVER_UC8179, 0, EPD_SCREEN_COLOR_BWR, 648, 480) == EPD_SCREEN_STATUS_UNSUPPORTED_DRIVER);
    assert(resolve(EPD_SCREEN_DRIVER_SSD1675, 0, EPD_SCREEN_COLOR_BW, 152, 152) == EPD_SCREEN_STATUS_UNSUPPORTED_DRIVER);
    assert(resolve(EPD_SCREEN_DRIVER_SSD1677, 0, EPD_SCREEN_COLOR_GRAY4, 800, 480) == EPD_SCREEN_STATUS_UNSUPPORTED_DRIVER);
    assert(resolve(EPD_SCREEN_DRIVER_SSD1683, 0, EPD_SCREEN_COLOR_BWR, 400, 300) == EPD_SCREEN_STATUS_UNSUPPORTED_DRIVER);
    assert(resolve(EPD_SCREEN_DRIVER_JD79665AA, 0, EPD_SCREEN_COLOR_BWRY, 768, 552) == EPD_SCREEN_STATUS_UNSUPPORTED_DRIVER);
    puts("epd constrained-controller tests passed");
    return 0;
}
