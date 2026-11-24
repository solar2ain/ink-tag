/* DA normal profile: E6 implementations are not included. */
#if INKTAG_EPD_ENABLE_JD79665CA || INKTAG_EPD_ENABLE_OSPREY_E6
#error DA_source_source_requires_E6_disabled
#endif
#include "inktag_epd_screen.h"

#include <string.h>

#if defined(INKTAG_TELINK)
extern volatile uint8_t inktag_boot_stage;
#define EPD_TRACE(value) (inktag_boot_stage = (value))
#else
#define EPD_TRACE(value) ((void)0)
#endif

#define COLOR_BIT(color) ((uint16_t)1u << (color))
#define COLORS_BW_BWR (COLOR_BIT(EPD_SCREEN_COLOR_BW) | COLOR_BIT(EPD_SCREEN_COLOR_BWR))

typedef enum {
    GEOMETRY_DYNAMIC = 0,
    GEOMETRY_UC8151,
    GEOMETRY_ZKC42V,
    GEOMETRY_SES_213,
    GEOMETRY_SES_266,
    GEOMETRY_HEMA_213,
    GEOMETRY_SSD1680,
    GEOMETRY_HUAWEI_398,
    GEOMETRY_LENMORY_H364F067,
    GEOMETRY_ZKC21B,
    GEOMETRY_ZKC42B,
    GEOMETRY_HANSHOW_NEBULAR_420,
    GEOMETRY_HANSHOW_NOWA_213,
} geometry_rule_t;

typedef struct {
    uint8_t driver_id;
    uint8_t panel_recipe;
    uint8_t transfer_format;
    uint8_t width_alignment;
    uint8_t geometry_rule;
    uint16_t color_mask;
    uint16_t max_width;
    uint16_t max_height;
} driver_descriptor_t;

/*
 * This table replaces the old full-screen profile matrix. A row describes a
 * controller/recipe path and its electrical data contract; width, color and
 * presentation remain runtime configuration. Recipe zero is standard, while
 * named recipes own panel-specific reset, scan, MTP and packing behavior.
 */
static const driver_descriptor_t s_drivers[] = {
#if INKTAG_EPD_ENABLE_UC8XXX
    {EPD_SCREEN_DRIVER_UC8XXX, INKTAG_RECIPE_UC8XXX_STANDARD,
     0, 8, GEOMETRY_DYNAMIC, COLORS_BW_BWR, 800, 600},
    {EPD_SCREEN_DRIVER_UC8XXX, INKTAG_RECIPE_UC8XXX_HANSHOW_NOWA_213R_N,
     0, 8, GEOMETRY_HANSHOW_NOWA_213,
     COLOR_BIT(EPD_SCREEN_COLOR_BWR), 128, 250},
#if INKTAG_EPD_ENABLE_UC8151
    {EPD_SCREEN_DRIVER_UC8151, INKTAG_RECIPE_UC8151_STANDARD,
     0, 8, GEOMETRY_UC8151, COLOR_BIT(EPD_SCREEN_COLOR_BWR), 152, 296},
    {EPD_SCREEN_DRIVER_UC8151, INKTAG_RECIPE_UC8151_SES_SE2213FS093,
     0, 8, GEOMETRY_SES_213, COLOR_BIT(EPD_SCREEN_COLOR_BWR), 104, 212},
    {EPD_SCREEN_DRIVER_UC8151, INKTAG_RECIPE_UC8151_SES_SE2266FS093,
     0, 8, GEOMETRY_SES_266,
     COLOR_BIT(EPD_SCREEN_COLOR_BWR), 152, 296},
#endif
#if INKTAG_EPD_ENABLE_UC8159
    {EPD_SCREEN_DRIVER_UC8159, INKTAG_RECIPE_UC8159_STANDARD,
     EPD_SCREEN_TRANSFER_INDEXED_4BPP, 2,
     GEOMETRY_DYNAMIC, COLORS_BW_BWR, 800, 600},
#endif
    {EPD_SCREEN_DRIVER_UC8176, INKTAG_RECIPE_UC8176_STANDARD,
     0, 8, GEOMETRY_DYNAMIC, COLORS_BW_BWR, 800, 600},
    {EPD_SCREEN_DRIVER_UC8176, INKTAG_RECIPE_UC8176_ZKC42V_FACTORY,
     0, 8, GEOMETRY_ZKC42V,
     COLOR_BIT(EPD_SCREEN_COLOR_BWR), 400, 300},
#if INKTAG_EPD_ENABLE_UC8179
    {EPD_SCREEN_DRIVER_UC8179, INKTAG_RECIPE_UC8179_STANDARD,
     0, 8, GEOMETRY_DYNAMIC, COLORS_BW_BWR, 960, 680},
#endif
    {EPD_SCREEN_DRIVER_UC8276, INKTAG_RECIPE_UC8276_STANDARD,
     0, 8, GEOMETRY_DYNAMIC, COLORS_BW_BWR, 800, 600},
    {EPD_SCREEN_DRIVER_UC8276, INKTAG_RECIPE_UC8276_ZKC42V_HE042AD7A2,
     0, 8, GEOMETRY_ZKC42V,
     COLOR_BIT(EPD_SCREEN_COLOR_BWR), 400, 300},
#endif

#if INKTAG_EPD_ENABLE_SSD1XXX
    {EPD_SCREEN_DRIVER_SSD1619, INKTAG_RECIPE_SSD1619_STANDARD,
     0, 8, GEOMETRY_DYNAMIC, COLORS_BW_BWR, 400, 300},
    {EPD_SCREEN_DRIVER_SSD1619, INKTAG_RECIPE_SSD1619_HEMA_213_BWR,
     0, 8, GEOMETRY_HEMA_213,
     COLOR_BIT(EPD_SCREEN_COLOR_BWR), 128, 250},

#if INKTAG_EPD_ENABLE_SSD1677
    {EPD_SCREEN_DRIVER_SSD1677, INKTAG_RECIPE_SSD1677_STANDARD,
     0, 8, GEOMETRY_DYNAMIC,
     COLORS_BW_BWR
#if INKTAG_EPD_ENABLE_SSD1677_GRAY4
         | COLOR_BIT(EPD_SCREEN_COLOR_GRAY4)
#endif
     , 960, 680},
#endif
    {EPD_SCREEN_DRIVER_SSD1680, INKTAG_RECIPE_SSD1680_STANDARD,
     0, 8, GEOMETRY_SSD1680, COLORS_BW_BWR, 128, 250},
    {EPD_SCREEN_DRIVER_SSD1680, INKTAG_RECIPE_SSD1680_ZKC21V_FACTORY,
     0, 8, GEOMETRY_SSD1680,
     COLORS_BW_BWR, 128, 250},
#if INKTAG_EPD_ENABLE_SSD1683
    {EPD_SCREEN_DRIVER_SSD1683, INKTAG_RECIPE_SSD1683_STANDARD,
     0, 8, GEOMETRY_DYNAMIC, COLORS_BW_BWR, 400, 300},
    {EPD_SCREEN_DRIVER_SSD1683, INKTAG_RECIPE_SSD1683_ZKC42VM_FACTORY,
     0, 8, GEOMETRY_DYNAMIC,
     COLORS_BW_BWR, 400, 300},
#endif
#endif

#if INKTAG_EPD_ENABLE_JD79XXX
    {EPD_SCREEN_DRIVER_JD79661, INKTAG_RECIPE_JD79661_STANDARD,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4, GEOMETRY_DYNAMIC,
     COLOR_BIT(EPD_SCREEN_COLOR_BWRY), 400, 300},
    {EPD_SCREEN_DRIVER_JD79661, INKTAG_RECIPE_JD79661_ZKC21B_FACTORY_B2,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4, GEOMETRY_ZKC21B,
     COLOR_BIT(EPD_SCREEN_COLOR_BWRY), 128, 250},
    {EPD_SCREEN_DRIVER_JD79665AA, INKTAG_RECIPE_JD79665AA_STANDARD,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4,
     GEOMETRY_DYNAMIC, COLOR_BIT(EPD_SCREEN_COLOR_BWRY), 800, 600},
    {EPD_SCREEN_DRIVER_JD79665AA, INKTAG_RECIPE_JD79665AA_HUAWEI_398_A0,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4, GEOMETRY_HUAWEI_398,
     COLOR_BIT(EPD_SCREEN_COLOR_BWRY), 768, 552},
    {EPD_SCREEN_DRIVER_JD79665AA, INKTAG_RECIPE_JD79665AA_HUAWEI_398_A1,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4, GEOMETRY_HUAWEI_398,
     COLOR_BIT(EPD_SCREEN_COLOR_BWRY), 768, 552},
    {EPD_SCREEN_DRIVER_JD79668AA, INKTAG_RECIPE_JD79668AA_STANDARD,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4, GEOMETRY_DYNAMIC,
     COLOR_BIT(EPD_SCREEN_COLOR_BWR) | COLOR_BIT(EPD_SCREEN_COLOR_BWRY),
     400, 300},
    {EPD_SCREEN_DRIVER_JD79668AA,
     INKTAG_RECIPE_JD79668AA_HANSHOW_NEBULAR_420R_N,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4, GEOMETRY_HANSHOW_NEBULAR_420,
     COLOR_BIT(EPD_SCREEN_COLOR_BWR) | COLOR_BIT(EPD_SCREEN_COLOR_BWRY),
     400, 300},
    {EPD_SCREEN_DRIVER_JD79668AA, INKTAG_RECIPE_JD79668AA_ZKC42B_FACTORY_B3,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4, GEOMETRY_ZKC42B,
     COLOR_BIT(EPD_SCREEN_COLOR_BWRY), 400, 300},
    {EPD_SCREEN_DRIVER_JD79XXX, INKTAG_RECIPE_JD79XXX_STANDARD,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4, GEOMETRY_DYNAMIC,
     COLOR_BIT(EPD_SCREEN_COLOR_BWR) | COLOR_BIT(EPD_SCREEN_COLOR_BWRY),
     800, 600},
    {EPD_SCREEN_DRIVER_JD79XXX,
     INKTAG_RECIPE_JD79XXX_HANSHOW_NEBULAR_213R_N,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4, GEOMETRY_HANSHOW_NOWA_213,
     COLOR_BIT(EPD_SCREEN_COLOR_BWR) | COLOR_BIT(EPD_SCREEN_COLOR_BWRY),
     128, 250},
#endif
};

static uint16_t read_u16_le(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void write_u16_le(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static const driver_descriptor_t *find_driver(uint8_t driver_id, uint8_t panel_recipe) {
    size_t i;
    for (i = 0; i < sizeof(s_drivers) / sizeof(s_drivers[0]); ++i) {
        if (s_drivers[i].driver_id == driver_id &&
            s_drivers[i].panel_recipe == panel_recipe) return &s_drivers[i];
    }
    return NULL;
}

static bool driver_is_known(uint8_t driver_id) {
    size_t i;
    for (i = 0; i < sizeof(s_drivers) / sizeof(s_drivers[0]); ++i) {
        if (s_drivers[i].driver_id == driver_id) return true;
    }
    return false;
}

static bool color_id_is_known(uint8_t color_id) {
    return color_id >= EPD_SCREEN_COLOR_BW && color_id <= EPD_SCREEN_COLOR_BWRYGBO;
}

static bool geometry_rule_matches(
    const driver_descriptor_t *driver, const epd_screen_config_t *config) {
    uint8_t rule = driver->geometry_rule;

    /* Keep this as comparisons instead of a switch. The TLSR8359 TC32
     * compiler emits an invalid indirect jump table for this contiguous enum,
     * which faults only when a fixed panel recipe reaches the table. */
#if INKTAG_EPD_ENABLE_UC8XXX && INKTAG_EPD_ENABLE_UC8151
    if (rule == GEOMETRY_UC8151) {
        return (config->width == 104u && config->height == 212u) ||
               (config->width == 152u && config->height == 296u);
    }
#endif
    if (rule == GEOMETRY_ZKC42V || rule == GEOMETRY_ZKC42B ||
        rule == GEOMETRY_HANSHOW_NEBULAR_420) {
        return config->width == 400u && config->height == 300u;
    }
#if INKTAG_EPD_ENABLE_UC8XXX && INKTAG_EPD_ENABLE_UC8151
    if (rule == GEOMETRY_SES_213) {
        return config->width == 104u && config->height == 212u;
    }
    if (rule == GEOMETRY_SES_266) {
        return config->width == 152u && config->height == 296u;
    }
#endif
    if (rule == GEOMETRY_HEMA_213) {
        return config->width == 128u && config->height == 250u;
    }
    if (rule == GEOMETRY_HANSHOW_NOWA_213) {
        return config->width == 128u && config->height == 250u &&
               config->visible_x == 0u && config->visible_y == 0u &&
               config->visible_width == 122u && config->visible_height == 250u;
    }
    if (rule == GEOMETRY_SSD1680 || rule == GEOMETRY_ZKC21B) {
        return config->width == 128u && config->height == 250u;
    }
    if (rule == GEOMETRY_HUAWEI_398) {
        return config->width == 768u && config->height == 552u;
    }
    return true;
}

static bool geometry_is_valid(
    const driver_descriptor_t *driver, const epd_screen_config_t *config) {
    uint32_t visible_right;
    uint32_t visible_bottom;
    uint8_t alignment = driver->width_alignment;

    if (config->width == 0u || config->height == 0u ||
        config->visible_width == 0u || config->visible_height == 0u ||
        config->width > driver->max_width || config->height > driver->max_height ||
        alignment == 0u || (alignment & (uint8_t)(alignment - 1u)) != 0u ||
        (config->width & (uint16_t)(alignment - 1u)) != 0u) {
        return false;
    }
    visible_right = (uint32_t)config->visible_x + config->visible_width;
    visible_bottom = (uint32_t)config->visible_y + config->visible_height;
    if (visible_right > config->width || visible_bottom > config->height) return false;
    if (!geometry_rule_matches(driver, config)) return false;
#if INKTAG_EPD_ENABLE_SSD1677_GRAY4
    if (config->driver_id == EPD_SCREEN_DRIVER_SSD1677 &&
        config->color_id == EPD_SCREEN_COLOR_GRAY4) {
        return config->width == 800u && config->height == 480u;
    }
#endif
    return true;
}

static uint8_t transfer_format_for(
    const driver_descriptor_t *driver, uint8_t color_id) {
    if (driver->transfer_format != 0u) return driver->transfer_format;
#if INKTAG_EPD_ENABLE_SSD1677_GRAY4
    if (color_id == EPD_SCREEN_COLOR_GRAY4) {
        return EPD_SCREEN_TRANSFER_PLANAR_2X1BPP;
    }
#endif
    return color_id == EPD_SCREEN_COLOR_BW ? EPD_SCREEN_TRANSFER_MONO_1BPP
                                            : EPD_SCREEN_TRANSFER_DUAL_1BPP;
}

epd_screen_status_t epd_screen_resolve(
    const epd_screen_config_t *candidate, epd_screen_t *screen) {
    const driver_descriptor_t *driver;
    uint8_t transfer_format;
    uint32_t row_bytes;
    uint32_t frame_bytes;

    EPD_TRACE(0xD0u);
    if (candidate == NULL || screen == NULL) return EPD_SCREEN_STATUS_UNSUPPORTED_COMBINATION;
    if (candidate->schema_version != EPD_SCREEN_SCHEMA_VERSION) {
        return EPD_SCREEN_STATUS_UNSUPPORTED_SCHEMA;
    }
    driver = find_driver(candidate->driver_id, candidate->panel_recipe);
    EPD_TRACE(0xD1u);
    if (driver == NULL) {
        if (!driver_is_known(candidate->driver_id)) {
            return EPD_SCREEN_STATUS_UNSUPPORTED_DRIVER;
        }
        /* Recipes are controller-local.  An unknown value uses recipe zero,
         * the controller's STANDARD path, and is normalized before it is
         * persisted or returned to a client. */
        driver = find_driver(candidate->driver_id, 0u);
        if (driver == NULL) return EPD_SCREEN_STATUS_UNSUPPORTED_RECIPE;
    }
    EPD_TRACE(0xD2u);
    if (!color_id_is_known(candidate->color_id)) return EPD_SCREEN_STATUS_UNSUPPORTED_COLOR;
    if ((driver->color_mask & COLOR_BIT(candidate->color_id)) == 0u) {
        return EPD_SCREEN_STATUS_UNSUPPORTED_COMBINATION;
    }
    if ((candidate->transform & EPD_SCREEN_TRANSFORM_RESERVED_MASK) != 0u) {
        return EPD_SCREEN_STATUS_INVALID_TRANSFORM;
    }
    if (candidate->native_ui_scale > INKTAG_NATIVE_UI_SCALE_X1_75) {
        return EPD_SCREEN_STATUS_UNSUPPORTED_COMBINATION;
    }
    EPD_TRACE(0xD3u);
    if (!geometry_is_valid(driver, candidate)) return EPD_SCREEN_STATUS_INVALID_GEOMETRY;
    EPD_TRACE(0xD4u);

    transfer_format = transfer_format_for(driver, candidate->color_id);
    if (transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2BPP ||
        transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2X2BPP) {
        row_bytes = ((uint32_t)candidate->width + 3u) / 4u;
    } else if (transfer_format == EPD_SCREEN_TRANSFER_INDEXED_4BPP) {
        row_bytes = ((uint32_t)candidate->width + 1u) / 2u;
    } else {
        row_bytes = ((uint32_t)candidate->width + 7u) / 8u;
    }
    frame_bytes = row_bytes * candidate->height;
    if (transfer_format == EPD_SCREEN_TRANSFER_DUAL_1BPP ||
        transfer_format == EPD_SCREEN_TRANSFER_PLANAR_2X1BPP ||
        transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2X2BPP) {
        frame_bytes *= 2u;
    }
    if (row_bytes > UINT16_MAX) return EPD_SCREEN_STATUS_INVALID_GEOMETRY;

    EPD_TRACE(0xD5u);
    /* Keep config padding deterministic as well as fields. Several ports
     * persist or compare this value as a complete record, so assigning the
     * caller's struct directly is not sufficient. Validation is complete
     * before the output is cleared, keeping the hot path small on constrained
     * compilers and preventing an invalid candidate from mutating output. */
    memset(screen, 0, sizeof(*screen));
    EPD_TRACE(0xD6u);
    screen->config.schema_version = candidate->schema_version;
    screen->config.driver_id = candidate->driver_id;
    screen->config.panel_recipe = driver->panel_recipe;
    screen->config.color_id = candidate->color_id;
    screen->config.transform = candidate->transform;
    screen->config.width = candidate->width;
    screen->config.height = candidate->height;
    screen->config.visible_x = candidate->visible_x;
    screen->config.visible_y = candidate->visible_y;
    screen->config.visible_width = candidate->visible_width;
    screen->config.visible_height = candidate->visible_height;
    screen->config.native_ui_scale = candidate->native_ui_scale;
    screen->row_bytes = (uint16_t)row_bytes;
    screen->frame_bytes = frame_bytes;
    screen->transfer_format = transfer_format;
    EPD_TRACE(0xD7u);
    return EPD_SCREEN_STATUS_OK;
}

static epd_screen_status_t screen_config_decode(
    const uint8_t *payload, size_t length, epd_screen_config_t *config,
    bool exact_recipe) {
    epd_screen_config_t candidate;
    epd_screen_t resolved;
    epd_screen_status_t status;

    if (payload == NULL || config == NULL || length != EPD_SCREEN_WIRE_SIZE) {
        return EPD_SCREEN_STATUS_UNSUPPORTED_COMBINATION;
    }
    if ((payload[4] & EPD_SCREEN_WIRE_RESERVED_MASK) != 0u) {
        return EPD_SCREEN_STATUS_INVALID_TRANSFORM;
    }
    memset(&candidate, 0, sizeof(candidate));
    candidate.schema_version = payload[0];
    candidate.driver_id = payload[1];
    candidate.panel_recipe = payload[2];
    candidate.color_id = payload[3];
    candidate.transform = payload[4] & 0x0fu;
    candidate.native_ui_scale =
        (payload[4] & EPD_SCREEN_NATIVE_UI_SCALE_MASK) >> EPD_SCREEN_NATIVE_UI_SCALE_SHIFT;
    candidate.width = read_u16_le(payload + 5);
    candidate.height = read_u16_le(payload + 7);
    candidate.visible_x = read_u16_le(payload + 9);
    candidate.visible_y = read_u16_le(payload + 11);
    candidate.visible_width = read_u16_le(payload + 13);
    candidate.visible_height = read_u16_le(payload + 15);
    status = epd_screen_resolve(&candidate, &resolved);
    if (status == EPD_SCREEN_STATUS_OK) {
        if (exact_recipe && resolved.config.panel_recipe != candidate.panel_recipe)
            return EPD_SCREEN_STATUS_UNSUPPORTED_RECIPE;
        *config = resolved.config;
    }
    return status;
}

epd_screen_status_t epd_screen_config_decode(
    const uint8_t *payload, size_t length, epd_screen_config_t *config) {
    return screen_config_decode(payload, length, config, false);
}

epd_screen_status_t epd_screen_config_decode_exact(
    const uint8_t *payload, size_t length, epd_screen_config_t *config) {
    return screen_config_decode(payload, length, config, true);
}

epd_screen_status_t epd_screen_config_encode(
    const epd_screen_config_t *config, uint8_t payload[EPD_SCREEN_WIRE_SIZE]) {
    epd_screen_t resolved;
    epd_screen_status_t status;

    if (payload == NULL) return EPD_SCREEN_STATUS_UNSUPPORTED_COMBINATION;
    EPD_TRACE(0xE0u);
    status = epd_screen_resolve(config, &resolved);
    EPD_TRACE(0xE1u);
    if (status != EPD_SCREEN_STATUS_OK) return status;
    payload[0] = resolved.config.schema_version;
    payload[1] = resolved.config.driver_id;
    payload[2] = resolved.config.panel_recipe;
    payload[3] = resolved.config.color_id;
    payload[4] = resolved.config.transform |
        (uint8_t)(resolved.config.native_ui_scale << EPD_SCREEN_NATIVE_UI_SCALE_SHIFT);
    write_u16_le(payload + 5, resolved.config.width);
    write_u16_le(payload + 7, resolved.config.height);
    write_u16_le(payload + 9, resolved.config.visible_x);
    write_u16_le(payload + 11, resolved.config.visible_y);
    write_u16_le(payload + 13, resolved.config.visible_width);
    write_u16_le(payload + 15, resolved.config.visible_height);
    EPD_TRACE(0xE2u);
    return EPD_SCREEN_STATUS_OK;
}

bool epd_screen_wire_image_compatible(
    const uint8_t left[EPD_SCREEN_WIRE_SIZE],
    const uint8_t right[EPD_SCREEN_WIRE_SIZE]) {
    if (left == NULL || right == NULL) return false;
    if (memcmp(left, right, 4u) != 0) return false;
    if ((left[4] & 0x0fu) != (right[4] & 0x0fu)) return false;
    return memcmp(left + 5u, right + 5u, EPD_SCREEN_WIRE_SIZE - 5u) == 0;
}

uint32_t epd_record_checksum(const uint8_t *data, size_t length) {
    uint32_t checksum = 2166136261u;
    size_t i;
    for (i = 0; i < length; ++i) {
        checksum ^= data[i];
        checksum *= 16777619u;
    }
    return checksum;
}

static uint32_t read_u32_le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void write_u32_le(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

epd_screen_status_t epd_screen_record_encode(
    const epd_screen_config_t *config, epd_screen_record_t *record) {
    epd_screen_status_t status;
    if (record == NULL) return EPD_SCREEN_STATUS_PERSISTENCE_FAILURE;
    memset(record, 0, sizeof(*record));
    record->bytes[0] = 'S';
    record->bytes[1] = 'P';
    record->bytes[2] = EPD_SCREEN_RECORD_VERSION;
    record->bytes[3] = EPD_SCREEN_WIRE_SIZE;
    status = epd_screen_config_encode(config, &record->bytes[4]);
    if (status != EPD_SCREEN_STATUS_OK) return status;
    write_u32_le(&record->bytes[24], epd_record_checksum(record->bytes, 24u));
    return EPD_SCREEN_STATUS_OK;
}

epd_screen_status_t epd_screen_record_decode(
    const epd_screen_record_t *record, epd_screen_config_t *config) {
    if (record == NULL || config == NULL || record->bytes[0] != (uint8_t)'S' ||
        record->bytes[1] != (uint8_t)'P' ||
        record->bytes[2] != EPD_SCREEN_RECORD_VERSION ||
        record->bytes[3] != EPD_SCREEN_WIRE_SIZE ||
        record->bytes[21] != 0u || record->bytes[22] != 0u ||
        record->bytes[23] != 0u ||
        read_u32_le(&record->bytes[24]) != epd_record_checksum(record->bytes, 24u)) {
        return EPD_SCREEN_STATUS_PERSISTENCE_FAILURE;
    }
    return epd_screen_config_decode_exact(&record->bytes[4], EPD_SCREEN_WIRE_SIZE, config);
}
