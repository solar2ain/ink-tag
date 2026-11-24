#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define INKTAG_DIALOG 1
#include "inktag_epd_driver.h"

const epd_driver_t epd_drv_uc8xxx = {0};
const epd_driver_t epd_drv_ssd1xxx = {0};
const epd_driver_t epd_drv_jd79xxx = {0};

static epd_screen_config_t screen_config(
    uint8_t driver, uint8_t recipe, uint8_t color, uint16_t width, uint16_t height) {
    epd_screen_config_t config;
    memset(&config, 0, sizeof(config));
    config.schema_version = EPD_SCREEN_SCHEMA_VERSION;
    config.driver_id = driver;
    config.panel_recipe = recipe;
    config.color_id = color;
    config.width = width;
    config.height = height;
    config.visible_width = width;
    config.visible_height = height;
    return config;
}

static void test_composed_driver_resolution_color(void) {
    epd_screen_config_t config = screen_config(
        EPD_SCREEN_DRIVER_UC8XXX, INKTAG_RECIPE_UC8XXX_STANDARD, EPD_SCREEN_COLOR_BWR, 400, 300);
    epd_screen_t screen;

    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_OK);
    assert(screen.row_bytes == 50u);
    assert(screen.frame_bytes == 30000u);
    assert(screen.transfer_format == EPD_SCREEN_TRANSFER_DUAL_1BPP);

    config.driver_id = EPD_SCREEN_DRIVER_UC8176;
    config.color_id = EPD_SCREEN_COLOR_BW;
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_OK);
    assert(screen.frame_bytes == 15000u);

    config.color_id = EPD_SCREEN_COLOR_BWY;
    assert(epd_screen_resolve(&config, &screen) ==
           EPD_SCREEN_STATUS_UNSUPPORTED_COMBINATION);
    config.color_id = 0xffu;
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_UNSUPPORTED_COLOR);
}

static void test_hanshow_nowa213_byte_pitch(void) {
    epd_screen_config_t config = screen_config(
        EPD_SCREEN_DRIVER_UC8XXX,
        INKTAG_RECIPE_UC8XXX_HANSHOW_NOWA_213R_N,
        EPD_SCREEN_COLOR_BWR, 128, 250);
    epd_screen_t screen;

    config.visible_width = 122u;
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_OK);
    assert(screen.row_bytes == 16u);
    assert(screen.frame_bytes == 8000u);
    assert(screen.transfer_format == EPD_SCREEN_TRANSFER_DUAL_1BPP);
    config.visible_width = 128u;
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_INVALID_GEOMETRY);
    config.visible_width = 122u;
    config.color_id = EPD_SCREEN_COLOR_BW;
    assert(epd_screen_resolve(&config, &screen) ==
           EPD_SCREEN_STATUS_UNSUPPORTED_COMBINATION);
}

static void test_special_command_paths(void) {
    epd_screen_config_t config = screen_config(
        EPD_SCREEN_DRIVER_JD79661, INKTAG_RECIPE_JD79661_ZKC21B_FACTORY_B2, EPD_SCREEN_COLOR_BWRY, 128, 250);
    epd_screen_t screen;

    config.visible_width = 122;
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_OK);
    assert(screen.row_bytes == 32u);
    assert(screen.frame_bytes == 8000u);
    assert(screen.transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2BPP);

    config = screen_config(
        EPD_SCREEN_DRIVER_JD79XXX,
        INKTAG_RECIPE_JD79XXX_HANSHOW_NEBULAR_213R_N,
        EPD_SCREEN_COLOR_BWR, 128, 250);
    config.visible_width = 122u;
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_OK);
    assert(screen.row_bytes == 32u);
    assert(screen.frame_bytes == 8000u);
    assert(screen.transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2BPP);

    config = screen_config(
        EPD_SCREEN_DRIVER_JD79668AA, INKTAG_RECIPE_JD79668AA_HANSHOW_NEBULAR_420R_N, EPD_SCREEN_COLOR_BWRY, 400, 300);
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_OK);
    assert(screen.row_bytes == 100);
    assert(screen.frame_bytes == 30000);
    assert(screen.transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2BPP);
    config.color_id = EPD_SCREEN_COLOR_BWR;
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_OK);

    config = screen_config(
        EPD_SCREEN_DRIVER_JD79668AA, INKTAG_RECIPE_JD79668AA_ZKC42B_FACTORY_B3, EPD_SCREEN_COLOR_BWRY, 400, 300);
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_OK);
    assert(screen.frame_bytes == 30000u);

    config = screen_config(
        EPD_SCREEN_DRIVER_JD79665AA, INKTAG_RECIPE_JD79665AA_HUAWEI_398_A0, EPD_SCREEN_COLOR_BWRY, 768, 552);
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_OK);
    config.width = 400;
    config.height = 300;
    config.visible_width = 400;
    config.visible_height = 300;
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_INVALID_GEOMETRY);

    config = screen_config(
        EPD_SCREEN_DRIVER_JD79668AA, INKTAG_RECIPE_JD79668AA_STANDARD, EPD_SCREEN_COLOR_BWRY, 768, 552);
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_INVALID_GEOMETRY);
    config = screen_config(
        EPD_SCREEN_DRIVER_JD79665AA, INKTAG_RECIPE_JD79665AA_STANDARD, EPD_SCREEN_COLOR_BWRY, 768, 552);
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_OK);
}

static void test_nebular213_four_colour_pilot(void) {
    epd_screen_config_t config = screen_config(
        EPD_SCREEN_DRIVER_JD79XXX,
        INKTAG_RECIPE_JD79XXX_HANSHOW_NEBULAR_213R_N,
        EPD_SCREEN_COLOR_BWRY, 128, 250);
    epd_screen_t screen;
    epd_screen_record_t record;
    epd_screen_config_t decoded;

    config.visible_width = 122u;
    config.transform = 5u;
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_OK);
    assert(screen.row_bytes == 32u && screen.frame_bytes == 8000u);
    assert(screen.transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2BPP);
    assert(epd_screen_record_encode(&config, &record) == EPD_SCREEN_STATUS_OK);
    assert(epd_screen_record_decode(&record, &decoded) == EPD_SCREEN_STATUS_OK);
    assert(memcmp(&decoded, &config, sizeof(config)) == 0);
    config.color_id = EPD_SCREEN_COLOR_BWR;
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_OK);
    config.color_id = EPD_SCREEN_COLOR_BWY;
    assert(epd_screen_resolve(&config, &screen) ==
           EPD_SCREEN_STATUS_UNSUPPORTED_COMBINATION);
    config.color_id = EPD_SCREEN_COLOR_BWRY;
    config.visible_width = 128u;
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_INVALID_GEOMETRY);
    config.visible_width = 122u;
    config.width = 400u;
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_INVALID_GEOMETRY);
    config.width = 128u;
    config.driver_id = EPD_SCREEN_DRIVER_SSD1680;
    assert(epd_screen_resolve(&config, &screen) ==
           EPD_SCREEN_STATUS_UNSUPPORTED_COMBINATION);
}

static void test_transfer_formats(void) {
    epd_screen_config_t config = screen_config(
        EPD_SCREEN_DRIVER_UC8159, INKTAG_RECIPE_UC8159_STANDARD, EPD_SCREEN_COLOR_BWR, 640, 384);
    epd_screen_t screen;

    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_UNSUPPORTED_DRIVER);

    config = screen_config(EPD_SCREEN_DRIVER_SSD1677,
        INKTAG_RECIPE_SSD1677_STANDARD, EPD_SCREEN_COLOR_GRAY4, 800, 480);
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_UNSUPPORTED_COMBINATION);
    config.color_id = EPD_SCREEN_COLOR_BWR;
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_OK);
    config = screen_config(EPD_SCREEN_DRIVER_JD79665CA,
        INKTAG_RECIPE_JD79665CA_LENMORY_H364F067_E6,
        EPD_SCREEN_COLOR_BWRYGB, 760, 568);
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_UNSUPPORTED_DRIVER);
}

static void test_geometry_rules(void) {
    epd_screen_config_t config = screen_config(
        EPD_SCREEN_DRIVER_SSD1680, INKTAG_RECIPE_SSD1680_STANDARD, EPD_SCREEN_COLOR_BWR, 128, 250);
    epd_screen_t screen;

    config.visible_x = 6;
    config.visible_width = 122;
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_OK);
    config.visible_width = 123;
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_INVALID_GEOMETRY);

    const uint8_t excluded[] = {EPD_SCREEN_DRIVER_UC8151, EPD_SCREEN_DRIVER_UC8159, EPD_SCREEN_DRIVER_SSD1675};
    for (size_t i = 0; i < sizeof(excluded); ++i) {
        config = screen_config(excluded[i], 0, EPD_SCREEN_COLOR_BWR, 152, 152);
        assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_UNSUPPORTED_DRIVER);
    }

    /* Unknown controller-local recipe values normalize to recipe zero. */
    config = screen_config(
        EPD_SCREEN_DRIVER_SSD1683, 0x02u,
        EPD_SCREEN_COLOR_BWR, 400, 300);
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_OK);
    assert(screen.config.panel_recipe == INKTAG_RECIPE_SSD1683_STANDARD);
    config = screen_config(
        EPD_SCREEN_DRIVER_UC8176, INKTAG_RECIPE_UC8176_ZKC42V_FACTORY,
        EPD_SCREEN_COLOR_BWR, 400, 300);
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_OK);
    config.color_id = EPD_SCREEN_COLOR_BW;
    assert(epd_screen_resolve(&config, &screen) ==
           EPD_SCREEN_STATUS_UNSUPPORTED_COMBINATION);
    config.color_id = EPD_SCREEN_COLOR_BWR;
    config.width = config.visible_width = 392u;
    assert(epd_screen_resolve(&config, &screen) ==
           EPD_SCREEN_STATUS_INVALID_GEOMETRY);
    config = screen_config(
        EPD_SCREEN_DRIVER_UC8176, 0x02u,
        EPD_SCREEN_COLOR_BWR, 400, 300);
    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_OK);
    assert(screen.config.panel_recipe == INKTAG_RECIPE_UC8176_STANDARD);
}

static void test_wire_codec_preserves_transform(void) {
    epd_screen_config_t config = screen_config(
        EPD_SCREEN_DRIVER_JD79661, INKTAG_RECIPE_JD79661_ZKC21B_FACTORY_B2, EPD_SCREEN_COLOR_BWRY, 128, 250);
    epd_screen_config_t decoded;
    uint8_t wire[EPD_SCREEN_WIRE_SIZE];

    config.transform = 0x0du; /* 90 degrees, mirror X and mirror Y. */
    config.visible_width = 122;
    assert(epd_screen_config_encode(&config, wire) == EPD_SCREEN_STATUS_OK);
    assert(sizeof(wire) == 17u);
    assert(wire[0] == EPD_SCREEN_SCHEMA_VERSION);
    assert(wire[1] == EPD_SCREEN_DRIVER_JD79661);
    assert(wire[2] == INKTAG_RECIPE_JD79661_ZKC21B_FACTORY_B2);
    assert(wire[3] == EPD_SCREEN_COLOR_BWRY);
    assert(wire[4] == 0x0du);
    assert(wire[5] == 0x80u && wire[6] == 0x00u);
    assert(wire[7] == 0xfau && wire[8] == 0x00u);
    assert(wire[13] == 0x7au && wire[14] == 0x00u);
    assert(epd_screen_config_decode(wire, sizeof(wire), &decoded) == EPD_SCREEN_STATUS_OK);
    assert(memcmp(&decoded, &config, sizeof(config)) == 0);

    config.native_ui_scale = INKTAG_NATIVE_UI_SCALE_X2;
    assert(epd_screen_config_encode(&config, wire) == EPD_SCREEN_STATUS_OK);
    assert(wire[4] == 0x3du);
    assert(epd_screen_config_decode(wire, sizeof(wire), &decoded) == EPD_SCREEN_STATUS_OK);
    assert(decoded.native_ui_scale == INKTAG_NATIVE_UI_SCALE_X2);
    assert(decoded.transform == 0x0du);
    config.native_ui_scale = INKTAG_NATIVE_UI_SCALE_X1_75;
    assert(epd_screen_config_encode(&config, wire) == EPD_SCREEN_STATUS_OK);
    assert(wire[4] == 0x5du);
    assert(epd_screen_config_decode(wire, sizeof(wire), &decoded) == EPD_SCREEN_STATUS_OK);
    assert(decoded.native_ui_scale == INKTAG_NATIVE_UI_SCALE_X1_75);
    assert(decoded.transform == 0x0du);
    {
        uint8_t auto_scale[EPD_SCREEN_WIRE_SIZE];
        config.native_ui_scale = INKTAG_NATIVE_UI_SCALE_AUTO;
        assert(epd_screen_config_encode(&config, auto_scale) == EPD_SCREEN_STATUS_OK);
        assert(epd_screen_wire_image_compatible(wire, auto_scale));
    }

    config.transform = 0x10u;
    assert(epd_screen_resolve(&config, &(epd_screen_t){0}) ==
           EPD_SCREEN_STATUS_INVALID_TRANSFORM);
    assert(epd_screen_config_decode(wire, sizeof(wire) - 1u, &decoded) ==
           EPD_SCREEN_STATUS_UNSUPPORTED_COMBINATION);
}

static void test_unknown_recipe_falls_back_to_standard(void) {
    epd_screen_config_t config = screen_config(
        EPD_SCREEN_DRIVER_JD79668AA, 0x7fu, EPD_SCREEN_COLOR_BWRY, 400, 300);
    epd_screen_config_t decoded;
    epd_screen_t screen;
    uint8_t wire[EPD_SCREEN_WIRE_SIZE];

    assert(epd_screen_resolve(&config, &screen) == EPD_SCREEN_STATUS_OK);
    assert(screen.config.panel_recipe == INKTAG_RECIPE_JD79668AA_STANDARD);
    assert(screen.transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2BPP);
    assert(screen.frame_bytes == 30000u);
    assert(epd_screen_config_encode(&config, wire) == EPD_SCREEN_STATUS_OK);
    assert(wire[2] == INKTAG_RECIPE_JD79668AA_STANDARD);
    wire[2] = 0x7fu;
    assert(epd_screen_config_decode(wire, sizeof(wire), &decoded) == EPD_SCREEN_STATUS_OK);
    assert(decoded.panel_recipe == INKTAG_RECIPE_JD79668AA_STANDARD);
}


static void test_flash_record_rejects_corruption(void) {
    epd_screen_config_t config = screen_config(
        EPD_SCREEN_DRIVER_SSD1683, INKTAG_RECIPE_SSD1683_STANDARD, EPD_SCREEN_COLOR_BWR, 400, 300);
    epd_screen_config_t decoded;
    epd_screen_record_t record;

    config.transform = EPD_SCREEN_TRANSFORM_MIRROR_X | 2u;
    assert(sizeof(record.bytes) == EPD_SCREEN_RECORD_SIZE);
    assert(epd_screen_record_encode(&config, &record) == EPD_SCREEN_STATUS_OK);
    assert(record.bytes[0] == 'S' && record.bytes[1] == 'P');
    assert(record.bytes[2] == EPD_SCREEN_RECORD_VERSION);
    assert(record.bytes[3] == EPD_SCREEN_WIRE_SIZE);
    assert(epd_screen_record_decode(&record, &decoded) == EPD_SCREEN_STATUS_OK);
    assert(memcmp(&decoded, &config, sizeof(config)) == 0);

    record.bytes[8] ^= 0x01u;
    assert(epd_screen_record_decode(&record, &decoded) ==
           EPD_SCREEN_STATUS_PERSISTENCE_FAILURE);
}

int main(void) {
    test_composed_driver_resolution_color();
    test_hanshow_nowa213_byte_pitch();
    test_special_command_paths();
    test_nebular213_four_colour_pilot();
    test_transfer_formats();
    test_geometry_rules();
    test_wire_codec_preserves_transform();
    test_unknown_recipe_falls_back_to_standard();
    test_flash_record_rejects_corruption();
    puts("epd screen tests passed");
    return 0;
}
