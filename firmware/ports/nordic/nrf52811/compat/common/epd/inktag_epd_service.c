/* Copyright (c) 2012 Nordic Semiconductor. All Rights Reserved.
 *
 * The information contained herein is property of Nordic Semiconductor ASA.
 * Terms and conditions of usage are described in detail in NORDIC
 * SEMICONDUCTOR STANDARD SOFTWARE LICENSE AGREEMENT.
 *
 * Licensees are granted free, non-transferable use of the information. NO
 * WARRANTY of ANY KIND is provided. This heading must NOT be removed from
 * the file.
 *
 */

/* Service transport and scheduler admission were implemented from the
 * observable contract in docs/research/2026-11-04-nrf32-epd-service-spec.md.
 * Nordic's retained notice above applies to the vendor-derived portions.
 * Inherited non-V2 execution is separately replaced from the companion
 * 2026-11-04-nrf32-compat-command-spec.md; retained product providers and
 * Nordic notices keep their existing licensing. No project license is changed. */

#include "inktag_epd_service.h"

#include <string.h>

#include "app_scheduler.h"
#include "ble_srv_common.h"
#include "main.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_pwr_mgmt.h"
#include "sdk_macros.h"
#include "inktag_board.h"
#include "inktag_monotonic_clock.h"
#include "inktag_page_cycle.h"
#ifndef INKTAG_NRF_INDICATOR_ENABLED
#define INKTAG_NRF_INDICATOR_ENABLED 0
#endif
#ifndef INKTAG_NRF_TEST_ENABLED
#define INKTAG_NRF_TEST_ENABLED 1
#endif
#include "inktag_gui_device_qr.h"
#include "app_timer.h"
#include "ble_dfu.h"
#include "inktag_lifecycle.h"
#include "inktag_protocol_core.h"
#if defined(INKTAG_ENABLE_SOAK_TELEMETRY) && INKTAG_ENABLE_SOAK_TELEMETRY
#include "inktag_soak_telemetry.h"
#endif
#include "inktag_brand_overlay.h"
#if INKTAG_NRF_INDICATOR_ENABLED
#include "inktag_nrf_config_store.h"
#include "inktag_status.h"
#endif
#include "inktag_nrf_health.h"
#include "inktag_nrf_record_store.h"
#if INKTAG_NRF_IMAGE_STORE_ENABLED
#include "inktag_nrf_image_store.h"
#endif

#if !defined(INKTAG_BOARD_DEFAULT_SCREEN_CONFIG)
#if defined(INKTAG_NRF_PROFILE_EPD_420_BWR_SSD1683)
#define INKTAG_BOARD_DEFAULT_SCREEN_CONFIG \
    {EPD_SCREEN_SCHEMA_VERSION, EPD_SCREEN_DRIVER_SSD1683, INKTAG_RECIPE_SSD1683_STANDARD, EPD_SCREEN_COLOR_BWR, 0u, 400u, 300u, 0u, 0u, 400u, 300u}
#elif defined(INKTAG_NRF_PROFILE_SMALL_213_PROBE)
#define INKTAG_BOARD_DEFAULT_SCREEN_CONFIG \
    {EPD_SCREEN_SCHEMA_VERSION, EPD_SCREEN_DRIVER_SSD1619, INKTAG_RECIPE_SSD1619_STANDARD, EPD_SCREEN_COLOR_BW, 1u, 128u, 250u, 0u, 0u, 128u, 250u}
#elif defined(INKTAG_NRF_PROFILE_HEMA_213_BWR)
#define INKTAG_BOARD_DEFAULT_SCREEN_CONFIG \
    {EPD_SCREEN_SCHEMA_VERSION, EPD_SCREEN_DRIVER_SSD1619, INKTAG_RECIPE_SSD1619_HEMA_213_BWR, EPD_SCREEN_COLOR_BWR, 3u, 128u, 250u, 0u, 0u, 122u, 250u}
#else
#define INKTAG_BOARD_DEFAULT_SCREEN_CONFIG \
    {EPD_SCREEN_SCHEMA_VERSION, EPD_SCREEN_DRIVER_UC8176, INKTAG_RECIPE_UC8176_STANDARD, EPD_SCREEN_COLOR_BWR, 0u, 400u, 300u, 0u, 0u, 400u, 300u}
#endif
#endif

#if !defined(INKTAG_BOARD_DEFAULT_EPD_CONFIG)
#define INKTAG_BOARD_DEFAULT_EPD_CONFIG \
    {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0xFF, 0x09, 0x03, 0xFF}
#endif

static uint32_t rle_decompress_from(const uint8_t *src, uint32_t src_len,
                                  uint32_t *src_pos, uint8_t *dst, uint32_t dst_len) {
    uint32_t produced = 0u;
    while (produced < dst_len && *src_pos < src_len) {
        const uint8_t *run = src + *src_pos;
        bool repeat = (run[0] & 128u) != 0u;
        uint32_t count = repeat ? (run[0] & 127u) + 3u : run[0] + 1u;
        uint32_t encoded = repeat ? 2u : count + 1u;
        if (count > dst_len - produced || encoded > src_len - *src_pos) break;
        if (repeat) memset(dst + produced, run[1], count);
        else memcpy(dst + produced, run + 1, count);
        produced += count;
        *src_pos += encoded;
    }
    return produced;
}

static void epd_parse_write_image_cfg(uint8_t cfg, bool *black, bool *begin, bool *rle) {
    uint8_t group = cfg >> 4;
    bool historical = group == 0u || group == 15u;
    *rle = !historical && (cfg & 4u) != 0u;
    *begin = historical ? group == 0u : (cfg & 2u) != 0u;
    *black = historical ? (cfg & 15u) == 15u : (cfg & 1u) == 0u;
}

static void epd_send_diag(ble_epd_t* p_epd, const char* tag);
static size_t epd_format_diag(ble_epd_t* p_epd, char* buf, size_t size, const char* tag);
static void epd_send_text(ble_epd_t* p_epd, const char* text);
static void epd_v2_send_log(ble_epd_t* p_epd, const char* text);
static void epd_v2_link_closed(ble_epd_t* p_epd);
static bool epd_device_info_activated(void);
static inktag_brand_edition_t epd_brand_edition(void);
static const gui_countdown_t* epd_v2_countdown(void);
static bool epd_brand_overlay_chunk(ble_epd_t* p_epd,
                                    uint8_t transfer_format,
                                    uint8_t segment_index,
                                    uint32_t segment_offset,
                                    uint8_t* data,
                                    uint16_t length, uint8_t reading_rotation);
#if INKTAG_NRF_IMAGE_STORE_ENABLED
static bool epd_v2_image_record_visible(ble_epd_t* p_epd, uint8_t id);
#endif
static inktag_status_v2_t epd_v2_input_action_access(uint8_t source,
                                                     uint8_t action);
static volatile bool m_epd_io_busy = false;
static volatile bool m_epd_cancel_requested = false;
static volatile bool m_epd_operation_active = false;
static volatile bool m_epd_operation_pending = false;
static volatile bool m_epd_dispatch_scheduled = false;
static volatile bool m_native_timer_update_pending = false;
static volatile uint32_t m_native_timer_update_timestamp = 0u;
static bool m_picture_io_active = false;
static epd_display_state_t m_display_state;
static bool m_display_state_loaded = false;
static epd_device_info_qr_t m_device_info_qr[EPD_DEVICE_INFO_QR_SLOT_COUNT];
#if INKTAG_NRF_IMAGE_STORE_ENABLED
static bool m_image_store_write_failed = false;
static uint8_t m_pending_picture_store_id = UINT8_MAX;
static bool m_slideshow_active = false;
static uint32_t m_slideshow_interval = 3600u;
static uint32_t m_slideshow_next_at = 0u;
static uint8_t m_slideshow_current_id = UINT8_MAX;
static epd_display_state_t m_button_slideshow_return_state;
static bool m_button_slideshow_return_valid = false;
#endif
#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED
#define INKTAG_NRF_BUTTON_ACTION_MASK 0x02FFu
static epd_device_settings_t m_device_settings;
static epd_display_state_t m_button_clear_return_state;
static bool m_button_clear_return_valid = false;
#endif
#if defined(INKTAG_GUI_STATIC_BUFFER_ONLY)
#if !defined(INKTAG_GUI_RENDER_BUFFER_SIZE) || INKTAG_GUI_RENDER_BUFFER_SIZE < 1
#error "INKTAG_GUI_STATIC_BUFFER_ONLY requires INKTAG_GUI_RENDER_BUFFER_SIZE"
#endif
/* Nordic previously allocated this same page buffer from its 8 KiB heap.
 * Keeping it explicit removes the general-purpose allocator without changing
 * the GUI paging model or the maximum 7 KiB render budget. */
static uint8_t m_gui_render_buffer[INKTAG_GUI_RENDER_BUFFER_SIZE] __attribute__((aligned(4)));
#define EPD_GUI_RENDER_BUFFER_FIELDS                   \
    .render_buffer = m_gui_render_buffer,              \
    .render_buffer_size = sizeof(m_gui_render_buffer),
#endif

typedef enum {
    NRF_EPD_OP_NONE = 0,
    NRF_EPD_OP_CLEAR,
    NRF_EPD_OP_REFRESH,
    NRF_EPD_OP_GUI,
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    NRF_EPD_OP_IMAGE_SHOW,
#endif
} nrf_epd_operation_type_t;

typedef struct {
    ble_epd_t* p_epd;
    uint32_t timestamp;
    uint8_t type;
    uint8_t arg;
    uint8_t background;
    uint8_t origin;
    uint8_t v2_opcode;
    uint8_t v2_transaction;
} nrf_epd_operation_t;

static nrf_epd_operation_t m_epd_pending_operation;
static nrf_epd_operation_t m_epd_active_operation;
static void epd_v2_operation_complete(const nrf_epd_operation_t* operation,
                                      inktag_status_v2_t status);
#if INKTAG_NRF_INDICATOR_ENABLED
static void epd_v2_indicator_tick(void);
static void epd_v2_indicator_set_active(inktag_status_scene_t scene,
                                        bool active);
static void epd_v2_indicator_trigger(inktag_status_scene_t scene);
#endif
static void epd_v2_display_read_yield(void);

static bool epd_abort_requested(void) {
    epd_v2_display_read_yield();
    return m_epd_cancel_requested;
}

#if defined(INKTAG_BOARD_LAZY_DISPLAY_BUS) && INKTAG_BOARD_LAZY_DISPLAY_BUS
/* Native rendering can spend seconds in paged font layout without returning
 * to main. Asset reads run in main context, so service the existing status
 * runtime here as well as during the controller BUSY wait. */
void ble_epd_gui_maintenance(void) {
    app_feed_wdt();
    epd_v2_display_read_yield();
#if defined(INKTAG_NRF_PROTOCOL_V2) && INKTAG_NRF_INDICATOR_ENABLED
    if (m_epd_operation_active) epd_v2_indicator_tick();
#endif
}
#endif

static bool epd_requires_busy_edge(const epd_model_t* epd) {
    if (epd == NULL) return false;
    return
           epd->ic == DRV_IC_SSD1619 ||
#if !defined(INKTAG_EXCLUDE_SSD1675_154)
           epd->ic == DRV_IC_SSD1675 ||
#endif
           epd->ic == DRV_IC_UC8159 ||
           epd->ic == DRV_IC_UC8176 || epd->ic == DRV_IC_UC8179;
}

static void epd_send_text(ble_epd_t* p_epd, const char* text) {
    epd_v2_send_log(p_epd, text);
}

#if INKTAG_NRF_IMAGE_STORE_ENABLED
static uint32_t epd_read_u32_le(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool epd_screen_config_matches(const ble_epd_t* p_epd,
                                      const uint8_t* encoded) {
    uint8_t current[EPD_SCREEN_WIRE_SIZE];
    return p_epd && encoded &&
           epd_screen_config_encode(&p_epd->screen_config, current) == EPD_SCREEN_STATUS_OK &&
           memcmp(current, encoded, sizeof(current)) == 0;
}

static uint32_t epd_image_store_expected_payload(const ble_epd_t* p_epd) {
    epd_screen_t screen;
    return p_epd && epd_screen_resolve(&p_epd->screen_config, &screen) == EPD_SCREEN_STATUS_OK
        ? screen.frame_bytes : 0u;
}

static const char* epd_image_store_result_name(inktag_image_store_result_t result) {
    switch (result) {
        case INKTAG_IMAGE_STORE_OK: return "ok";
        case INKTAG_IMAGE_STORE_UNSUPPORTED: return "unsupported";
        case INKTAG_IMAGE_STORE_BAD_ARGUMENT: return "bad-argument";
        case INKTAG_IMAGE_STORE_BUSY: return "busy";
        case INKTAG_IMAGE_STORE_NO_SPACE: return "no-space";
        case INKTAG_IMAGE_STORE_IO_ERROR: return "io-error";
        case INKTAG_IMAGE_STORE_SIZE_MISMATCH: return "size-mismatch";
        case INKTAG_IMAGE_STORE_CRC_MISMATCH: return "crc-mismatch";
        case INKTAG_IMAGE_STORE_NOT_FOUND: return "not-found";
        default: return "unknown";
    }
}

static void epd_send_image_store_result(ble_epd_t* p_epd, const char* operation,
                                        inktag_image_store_result_t result,
                                        uint8_t id) {
    char buf[80];
    if (result == INKTAG_IMAGE_STORE_OK)
        GFX_format(buf, sizeof(buf), "img:%s:done id=%u", operation, (unsigned)id);
    else
        GFX_format(buf, sizeof(buf), "img:%s:error=%s", operation,
                   epd_image_store_result_name(result));
    epd_send_text(p_epd, buf);
}

static void epd_slideshow_stop(ble_epd_t* p_epd, bool notify) {
    bool was_active = m_slideshow_active;
    m_slideshow_active = false;
    m_slideshow_next_at = 0u;
    m_slideshow_current_id = UINT8_MAX;
    m_button_slideshow_return_valid = false;
    if (notify && was_active) epd_send_text(p_epd, "img:slideshow:stopped");
}

static bool epd_slideshow_select_next(ble_epd_t* p_epd, uint8_t* id) {
    uint8_t cursor = m_slideshow_current_id;
    uint8_t first = UINT8_MAX;
    uint16_t attempts;
    /* The independent fixed-slot backend's sector_count returns slots. */
    for (attempts = 0u; attempts < inktag_image_store_sector_count(); attempts++) {
        uint8_t candidate;
        if (inktag_image_store_next_compatible(
                cursor, &p_epd->screen_config, &candidate) != INKTAG_IMAGE_STORE_OK)
            return false;
        if (first == UINT8_MAX) first = candidate;
        else if (candidate == first) return false;
        if (epd_v2_image_record_visible(p_epd, candidate)) {
            *id = candidate;
            return true;
        }
        cursor = candidate;
    }
    return false;
}

static bool epd_page_cycle_resolve(ble_epd_t* p_epd,
                                   nrf_epd_operation_type_t* operation,
                                   uint8_t* argument,
                                   bool* slideshow_step) {
    epd_display_state_t slideshow_state;
    const epd_display_state_t* current =
        m_display_state_loaded ? &m_display_state : NULL;
    inktag_page_cycle_target_t target;
    uint8_t first_id = UINT8_MAX;
    bool first_available = false;

    if (p_epd == NULL || operation == NULL || argument == NULL ||
        slideshow_step == NULL) return false;
    *slideshow_step = false;
    if (m_slideshow_active) {
        slideshow_state = m_display_state;
        slideshow_state.mode = EPD_DISPLAY_STATE_SLIDESHOW;
        slideshow_state.argument = m_slideshow_current_id;
        current = &slideshow_state;
    } else if (current != NULL &&
               current->mode == EPD_DISPLAY_STATE_DEVICE_INFO) {
        first_available = epd_slideshow_select_next(p_epd, &first_id);
    }

    target = inktag_page_cycle_next(current, first_available, false);
    switch (target) {
        case INKTAG_PAGE_CYCLE_CALENDAR:
            *operation = NRF_EPD_OP_GUI;
            *argument = MODE_CALENDAR;
            return true;
        case INKTAG_PAGE_CYCLE_CLOCK:
            *operation = NRF_EPD_OP_GUI;
            *argument = MODE_CLOCK;
            return true;
        case INKTAG_PAGE_CYCLE_DEVICE_INFO:
            *operation = NRF_EPD_OP_GUI;
            *argument = MODE_DEVICE_INFO;
            return true;
        case INKTAG_PAGE_CYCLE_SLIDESHOW:
            if (!first_available) return false;
            *operation = NRF_EPD_OP_IMAGE_SHOW;
            *argument = first_id;
            *slideshow_step = true;
            return true;
        default:
            return false;
    }
}

static void epd_page_cycle_scheduled(ble_epd_t* p_epd, bool slideshow_step) {
    epd_slideshow_stop(p_epd, true);
    if (!slideshow_step) return;
    if (m_slideshow_interval < 30u) m_slideshow_interval = 3600u;
    m_slideshow_active = true;
    m_slideshow_next_at = timestamp() + m_slideshow_interval;
}

static void epd_send_image_store_info(ble_epd_t* p_epd) {
    inktag_image_store_status_t status;
    uint32_t payload = epd_image_store_expected_payload(p_epd);
    inktag_image_store_result_t result = inktag_image_store_status(
        &p_epd->screen_config, payload, &status);
    char buf[220];
    uint16_t fit, contiguous;
    if (result != INKTAG_IMAGE_STORE_OK || payload == 0u) {
        epd_send_image_store_result(p_epd, "info", result, 0u);
        return;
    }
    fit = status.required_sectors == 0u ? 0u :
          (uint16_t)(status.free_sectors / status.required_sectors);
    contiguous = status.required_sectors == 0u ? 0u :
                 (uint16_t)(status.largest_free_run / status.required_sectors);
    GFX_format(buf, sizeof(buf),
               "img:info driver=%02x size=%ux%u bytes=%u need=%u total=%u "
               "free=%u largest=%u fit=%u contiguous=%u saved=%u "
               "compatible=%u playable=%u incompatible=%u dirty=%u "
               "slideshow=%u interval=%u",
               (unsigned)p_epd->screen_config.driver_id,
               (unsigned)p_epd->screen_config.width,
               (unsigned)p_epd->screen_config.height,
               (unsigned)payload, (unsigned)status.required_sectors,
               (unsigned)status.total_sectors, (unsigned)status.free_sectors,
               (unsigned)status.largest_free_run, (unsigned)fit,
               (unsigned)contiguous, (unsigned)status.saved_count,
               (unsigned)status.compatible_count, (unsigned)status.playable_count,
               (unsigned)status.incompatible_count, (unsigned)status.dirty_sectors,
               m_slideshow_active ? 1u : 0u, (unsigned)m_slideshow_interval);
    epd_send_text(p_epd, buf);
}

static void epd_send_image_store_list(ble_epd_t* p_epd) {
    uint16_t sector, count = 0u;
    for (sector = 0u; sector < inktag_image_store_sector_count(); ++sector) {
        inktag_image_store_header_t header;
        epd_screen_config_t stored;
        bool decoded, is_compatible;
        char buf[184];
        if (inktag_image_store_get((uint8_t)sector, &header) != INKTAG_IMAGE_STORE_OK)
            continue;
        decoded = epd_screen_config_decode(header.screen_config, EPD_SCREEN_WIRE_SIZE,
                                           &stored) == EPD_SCREEN_STATUS_OK;
        is_compatible = decoded && epd_screen_config_matches(p_epd, header.screen_config);
        GFX_format(buf, sizeof(buf),
                   "img:item id=%u driver=%02x size=%ux%u bytes=%u fmt=%u "
                   "seg=%u type=%u created=%u compatible=%u name=%s",
                   (unsigned)sector,
                   (unsigned)(decoded ? stored.driver_id : 0xFFu),
                   (unsigned)(decoded ? stored.width : 0u),
                   (unsigned)(decoded ? stored.height : 0u),
                   (unsigned)header.payload_size, (unsigned)header.format,
                   (unsigned)header.segment_count, (unsigned)header.content_type,
                   (unsigned)header.created_at, is_compatible ? 1u : 0u,
                   header.name[0] != '\0' ? header.name : "image");
        epd_send_text(p_epd, buf);
        count++;
        sector = (uint16_t)(sector + header.record_sectors - 1u);
        app_feed_wdt();
    }
    {
        char done[32];
        GFX_format(done, sizeof(done), "img:list:done count=%u", (unsigned)count);
        epd_send_text(p_epd, done);
    }
    epd_send_image_store_info(p_epd);
}
#endif

typedef struct {
    epd_model_t* epd;
    bool black_plane;
    bool begin;
} epd_gui_ram_plane_t;

static void epd_gui_write_ram_plane(void* user_data, uint8_t* black, uint8_t* color, uint16_t x, uint16_t y,
                                    uint16_t w, uint16_t h) {
    epd_gui_ram_plane_t* plane = (epd_gui_ram_plane_t*)user_data;
    (void)color;
    if (plane == NULL || plane->epd == NULL || black == NULL) return;
    uint16_t ram_width = (plane->epd->width + 7u) & ~7u;
    if (x != 0 || w != ram_width || y >= plane->epd->height || h == 0) return;

    uint32_t len = ((uint32_t)(w + 7) / 8) * h;
    while (len > 0) {
        if (EPD_AbortRequested()) return;
        app_feed_wdt();
        uint8_t chunk = len > UINT8_MAX ? UINT8_MAX : (uint8_t)len;
        (void)EPD_DriverWriteRam(plane->epd, plane->begin,
                                 plane->black_plane, black, chunk);
        plane->begin = false;
        black += chunk;
        len -= chunk;
    }
}

__attribute__((noinline, noclone))
static bool epd_gui_update_run(ble_epd_t* p_epd, display_mode_t mode, uint32_t update_timestamp) {
    epd_model_t* epd = epd_init(&p_epd->screen_config);
    if (epd == NULL) {
        return false;
    }
    p_epd->epd = epd;
    char screen_info[32] = {0};
    int8_t display_temperature = GUI_TEMPERATURE_UNAVAILABLE;
    const char* color_name = epd->color == COLOR_BW ? "BW" :
                             epd->color == COLOR_BWR ? "BWR" :
                             epd->color == COLOR_BWRY ? "BWRY" :
                             epd->color == COLOR_BWRYGB ? "BWRYGB" : "COLOR";
    GFX_format(screen_info, sizeof(screen_info), "%ux%u / %s",
               epd->visible_width, epd->visible_height, color_name);
    /* The platform-neutral hook supplies only a validated product sample.
     * The legacy panel callback cannot report controller-read failure. */
    (void)EPD_ReadEffectiveTemperature(&display_temperature);
    gui_data_t data = {
        .mode = mode,
        .color = epd->color,
        .width = epd->width,
        .height = epd->height,
#if defined(INKTAG_GUI_RAW_VIEWPORT)
        .visible_x = epd->visible_x,
        .visible_y = epd->visible_y,
#endif
        .visible_width = epd->visible_width,
        .visible_height = epd->visible_height,
        .force_rotation = true,
        .rotation = (GFX_Rotate)(epd->transform & EPD_SCREEN_TRANSFORM_ROTATION_MASK),
        .mirror_x = (epd->transform & EPD_SCREEN_TRANSFORM_MIRROR_X) != 0u,
        .mirror_y = (epd->transform & EPD_SCREEN_TRANSFORM_MIRROR_Y) != 0u,
        .native_ui_scale = p_epd->screen_config.native_ui_scale,
        .timestamp = update_timestamp,
        .week_start = p_epd->config.week_start,
        .countdown = epd_v2_countdown(),
        .temperature = display_temperature,
        .voltage = EPD_ReadVoltage(),
        .activated = epd_device_info_activated(),
        .brand_hidden = epd_brand_edition() == INKTAG_BRAND_EDITION_HIDDEN,
        .screen_info = screen_info,
    };
    char bluetooth_name[EPD_DEVICE_NAME_MAX_BYTES + 1u] = {0};
    uint16_t dev_name_len = sizeof(bluetooth_name) - 1u;
    uint32_t err_code = sd_ble_gap_device_name_get((uint8_t*)bluetooth_name, &dev_name_len);
    if (err_code == NRF_SUCCESS && dev_name_len > 0u && dev_name_len < sizeof(bluetooth_name)) {
        bluetooth_name[dev_name_len] = '\0';
        memcpy(data.ssid, bluetooth_name, dev_name_len + 1u);
    } else {
        memcpy(bluetooth_name, "--", 3u);
    }
    data.bluetooth_name = bluetooth_name;
    data.mcu_name = "nRF52811";
    if (mode == MODE_DEVICE_INFO) {
        (void)GUI_SetDeviceName(&data, m_device_settings.label,
                                m_device_settings.label_length,
                                (uint16_t)NRF_FICR->DEVICEADDR[0]);
        data.device_info_qr_data[0] = inktag_default_device_qr;
        data.device_info_qr_modules[0] = INKTAG_DEFAULT_DEVICE_QR_MODULES;
        for (uint8_t i = 0; i < EPD_DEVICE_INFO_QR_SLOT_COUNT; i++) {
            if (epd_device_info_qr_read(i, &m_device_info_qr[i])) {
                data.device_info_qr_data[i] = m_device_info_qr[i].data;
                data.device_info_qr_modules[i] = m_device_info_qr[i].modules;
#if defined(INKTAG_NRF_COMPACT_STORE)
            } else if (inktag_nrf_config_store_last_error() != NRF_ERROR_NOT_FOUND) {
                return false;
#endif
            }
        }
    }

    epd_send_diag(p_epd, "gui:start");
    if (EPD_GuiNeedsContiguousColorPlanes(epd)) {
        gui_options_t black_options = {
            .color_plane = GFX_COLOR_PLANE_BLACK,
            .force_buffer_color = true,
            .buffer_color = COLOR_BW,
#if defined(INKTAG_GUI_STATIC_BUFFER_ONLY)
            EPD_GUI_RENDER_BUFFER_FIELDS
#endif
        };
        epd_gui_ram_plane_t black_plane = {.epd = epd, .black_plane = true, .begin = true};
        epd_send_diag(p_epd, "gui:black");
        if (!DrawGUIEx(&data, epd_gui_write_ram_plane, &black_plane, &black_options))
            return false;
        app_feed_wdt();
        if (EPD_AbortRequested()) return false;

        gui_options_t red_options = {
            .color_plane = GFX_COLOR_PLANE_RED,
            .force_buffer_color = true,
            .buffer_color = COLOR_BW,
#if defined(INKTAG_GUI_STATIC_BUFFER_ONLY)
            EPD_GUI_RENDER_BUFFER_FIELDS
#endif
        };
        epd_gui_ram_plane_t red_plane = {.epd = epd, .black_plane = false, .begin = true};
        epd_send_diag(p_epd, "gui:red");
        if (!DrawGUIEx(&data, epd_gui_write_ram_plane, &red_plane, &red_options))
            return false;
        app_feed_wdt();
    } else {
        epd_send_diag(p_epd, "gui:draw");
#if defined(INKTAG_GUI_STATIC_BUFFER_ONLY)
        gui_options_t page_options = {
            EPD_GUI_RENDER_BUFFER_FIELDS
            .prefer_full_frame = true,
        };
        if (!DrawGUIEx(&data, (buffer_callback)epd->drv->write_image, epd, &page_options))
            return false;
#else
        DrawGUI(&data, (buffer_callback)epd->drv->write_image, epd);
#endif
        app_feed_wdt();
    }
    if (EPD_AbortRequested()) return false;
    epd_send_diag(p_epd, "gui:refresh");
    epd->drv->refresh(epd);
    app_feed_wdt();
    return !EPD_AbortRequested();
}

static void epd_operation_dispatch(void* p_event_data, uint16_t event_size);

static void epd_record_display_state(ble_epd_t* p_epd, uint8_t mode, uint8_t argument,
                                     uint32_t interval_seconds) {
    epd_display_state_t next = {
        .mode = mode,
        .argument = argument,
        .reserved = m_display_state_loaded ? m_display_state.reserved : 0u,
        .interval_seconds = interval_seconds,
    };
    p_epd->config.display_mode = epd_display_state_driver_mode(mode);
    if (!m_display_state_loaded || memcmp(&next, &m_display_state, sizeof(next)) != 0) {
        if (epd_display_state_write(&next)) {
            m_display_state = next;
            m_display_state_loaded = true;
        }
    }
}

static bool epd_display_state_flag(uint16_t flag) {
    return m_display_state_loaded && (m_display_state.reserved & flag) != 0u;
}

static void epd_schedule_dispatch(void) {
    if (!m_epd_operation_pending || m_epd_operation_active || m_epd_dispatch_scheduled) return;
    /* Own the token before posting: failure leaves the operation available
     * to the ordinary process-loop retry, without a second queue entry. */
    m_epd_dispatch_scheduled = true;
    uint32_t posted = app_sched_event_put(NULL, 0, epd_operation_dispatch);
    if (posted == NRF_SUCCESS) return;
    m_epd_dispatch_scheduled = false;
    epd_send_text(m_epd_pending_operation.p_epd, "epd:sched-fail");
}

/* Policy providers may be reduced for a InkTag export without changing the
 * mailbox or transport. Admission never publishes work or acquires the panel. */
static bool epd_operation_admit(ble_epd_t *p_epd) {
    (void)p_epd;

    if (!inktag_nrf_health_operation_allowed(INKTAG_POWER_OPERATION_DISPLAY, true)) {
        epd_send_text(p_epd, "epd:low-power");
        return false;
    }
    return true;
}

static bool epd_schedule_operation(ble_epd_t *p_epd,
                                   nrf_epd_operation_type_t type, uint8_t arg,
                                   uint32_t operation_timestamp) {
    if (!epd_operation_admit(p_epd)) return false;
    bool occupied = m_epd_operation_active || m_epd_operation_pending;
    if (type != NRF_EPD_OP_REFRESH) occupied = occupied || m_picture_io_active;
    if (occupied) {
        epd_send_text(p_epd, "epd:busy");
        return false;
    }

    /* Capability policy ends above. Publish a complete local request before
     * making it visible to the dispatcher; BLE ownership is added by V2. */
    m_epd_pending_operation = (nrf_epd_operation_t){
        .p_epd = p_epd, .timestamp = operation_timestamp, .type = (uint8_t)type, .arg = arg,
    };

    m_epd_operation_pending = true;
    epd_schedule_dispatch();
    return true;
}

static bool epd_schedule_background_operation(ble_epd_t* p_epd,
                                              nrf_epd_operation_type_t type,
                                              uint8_t arg,
                                              uint32_t operation_timestamp) {
    if (!epd_schedule_operation(p_epd, type, arg, operation_timestamp))
        return false;
    m_epd_pending_operation.background = 1u;
    return true;
}

#if INKTAG_NRF_IMAGE_STORE_ENABLED
__attribute__((noinline, noclone))
static bool epd_image_store_show_run(ble_epd_t* p_epd, uint8_t id) {
    inktag_image_store_header_t header;
    uint8_t buffer[220];
    uint32_t offset = 0u;
    uint8_t segment;
    inktag_image_store_result_t result = inktag_image_store_get(id, &header);
    if (result != INKTAG_IMAGE_STORE_OK) {
        epd_send_image_store_result(p_epd, "show", result, id);
        return false;
    }
    if (!epd_screen_config_matches(p_epd, header.screen_config)) {
        epd_send_text(p_epd, "img:show:error=incompatible");
        return false;
    }
    p_epd->epd->drv->init(p_epd->epd);
    for (segment = 0u; segment < header.segment_count; ++segment) {
        uint32_t remaining = header.segment_size[segment];
        bool begin = true;
        bool black = (header.black_mask & (1u << segment)) != 0u;
        while (remaining > 0u) {
            uint32_t segment_offset = header.segment_size[segment] - remaining;
            uint16_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) :
                                                              (uint16_t)remaining;
            if (EPD_AbortRequested()) return false;
            result = inktag_image_store_read(id, offset, buffer, chunk);
            if (result != INKTAG_IMAGE_STORE_OK) {
                epd_send_image_store_result(p_epd, "show", result, id);
                return false;
            }
            if (!epd_brand_overlay_chunk(p_epd, header.format, segment,
                                         segment_offset, buffer, chunk,
                                         inktag_image_reading_rotation(header.black_mask))) {
                epd_send_text(p_epd, "img:show:error=brand");
                return false;
            }
            if (!EPD_DriverWriteRam(p_epd->epd, begin, black, buffer,
                                    (uint8_t)chunk)) return false;
            begin = false;
            offset += chunk;
            remaining -= chunk;
            app_feed_wdt();
        }
    }
    p_epd->epd->drv->refresh(p_epd->epd);
    return true;
}
#endif

static void epd_operation_dispatch(void* p_event_data, uint16_t event_size) {
    (void)p_event_data;
    (void)event_size;
    m_epd_dispatch_scheduled = false;
    if (!m_epd_operation_pending) return;

    nrf_epd_operation_t operation = m_epd_pending_operation;
    m_epd_active_operation = operation;
    m_epd_operation_pending = false;
    m_epd_operation_active = true;
    m_epd_cancel_requested = false;
    m_epd_io_busy = true;
    inktag_nrf_health_display_begin();
    EPD_SetAbortCallback(epd_abort_requested);
#if defined(INKTAG_NRF_PROTOCOL_V2) && INKTAG_NRF_INDICATOR_ENABLED
    epd_v2_indicator_set_active(INKTAG_STATUS_SCENE_REFRESH, true);
    EPD_SetMaintenanceCallback(epd_v2_indicator_tick);
#endif
#if defined(INKTAG_BOARD_LAZY_DISPLAY_BUS) && INKTAG_BOARD_LAZY_DISPLAY_BUS
    if (!m_picture_io_active) EPD_GPIO_Init();
#else
    EPD_GPIO_Init();
#endif
    EPD_BoardPrepare();

    ble_epd_t* p_epd = operation.p_epd;
    if (p_epd->epd == NULL) p_epd->epd = epd_init(&p_epd->screen_config);
    bool started = p_epd->epd != NULL && EPD_GPIO_IsConfigured();
    bool io_failed = false;
    EPD_BusyDiagReset();
    if (!started) {
        epd_send_text(p_epd, "epd:no-profile");
    } else if (operation.type == NRF_EPD_OP_CLEAR) {
        epd_send_diag(p_epd, "clear:start");
        p_epd->epd->drv->init(p_epd->epd);
        epd_send_diag(p_epd, "clear:init");
        if (!EPD_AbortRequested()) p_epd->epd->drv->clear(p_epd->epd, operation.arg != 0);
    } else if (operation.type == NRF_EPD_OP_REFRESH) {
        epd_send_diag(p_epd, "picture:refresh");
        p_epd->epd->drv->refresh(p_epd->epd);
    } else if (operation.type == NRF_EPD_OP_GUI) {
        started = epd_gui_update_run(p_epd, (display_mode_t)operation.arg, operation.timestamp);
        if (!started && p_epd->epd != NULL && !EPD_AbortRequested()) io_failed = true;
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    } else if (operation.type == NRF_EPD_OP_IMAGE_SHOW) {
        started = epd_image_store_show_run(p_epd, operation.arg);
#endif
    }


    bool cancelled = m_epd_cancel_requested || EPD_AbortRequested();
    bool refresh_expected = started &&
                            (operation.type == NRF_EPD_OP_REFRESH || operation.type == NRF_EPD_OP_GUI ||
#if INKTAG_NRF_IMAGE_STORE_ENABLED
                            operation.type == NRF_EPD_OP_IMAGE_SHOW ||
#endif
                            (operation.type == NRF_EPD_OP_CLEAR && operation.arg != 0));
    /* Compact diagnostics may omit log text, but controller refreshes using
     * the edge waiter must prove that activation actually asserted BUSY.
     * An idle pin after an unacknowledged SPI transfer is not completion. */
    bool missing_busy_edge = refresh_expected && !cancelled &&
                             epd_requires_busy_edge(p_epd->epd) &&
                             !EPD_BusyDiagAsserted();
    bool stuck_busy = started &&
                      !cancelled && p_epd->epd->drv->read_busy(p_epd->epd);
    inktag_status_v2_t completion_status = io_failed ? INKTAG_STATUS_IO_ERROR :
        !started ? INKTAG_STATUS_INCOMPATIBLE :
        cancelled ? INKTAG_STATUS_CANCELLED :
        (missing_busy_edge || stuck_busy) ? INKTAG_STATUS_TIMEOUT :
                                            INKTAG_STATUS_OK;
#if !defined(INKTAG_COMPACT_BUSY_DIAG)
    if (missing_busy_edge) epd_send_diag(p_epd, "epd:no-busy-edge");
#endif
    if (cancelled || missing_busy_edge || stuck_busy) {
        /* Hardware RESET is the only controller-independent way to stop a
         * refresh waveform.  Keep EN/GPIO alive until RESET has completed. */
        EPD_Reset(true, 10);
        epd_send_text(p_epd, cancelled ? "epd:stopped" : "epd:timeout");
    } else if (started) {
        if (operation.type == NRF_EPD_OP_CLEAR) {
            epd_record_display_state(p_epd, EPD_DISPLAY_STATE_CLEAR, operation.arg, 0);
        } else if (operation.type == NRF_EPD_OP_REFRESH) {
            epd_record_display_state(p_epd, EPD_DISPLAY_STATE_PICTURE,
#if INKTAG_NRF_IMAGE_STORE_ENABLED
                                     m_pending_picture_store_id, 0);
            m_pending_picture_store_id = UINT8_MAX;
#else
                                     EPD_DISPLAY_STATE_NO_SOURCE, 0);
#endif
#if INKTAG_NRF_IMAGE_STORE_ENABLED
        } else if (operation.type == NRF_EPD_OP_IMAGE_SHOW) {
            epd_record_display_state(p_epd,
                                     m_slideshow_active ? EPD_DISPLAY_STATE_SLIDESHOW :
                                                          EPD_DISPLAY_STATE_PICTURE,
                                     operation.arg,
                                     m_slideshow_active ? m_slideshow_interval : 0u);
            if (m_slideshow_active) {
                m_slideshow_current_id = operation.arg;
            }
#endif
        } else if (operation.type == NRF_EPD_OP_GUI) {
            epd_record_display_state(p_epd,
                                     operation.arg == MODE_DEVICE_INFO ? EPD_DISPLAY_STATE_DEVICE_INFO : operation.arg,
                                     EPD_DISPLAY_STATE_NO_SOURCE, 0);
        }
#if defined(INKTAG_NRF_BOARD_NRF52811_AESL)
        inktag_nrf_health_panel_temperature_sample(&p_epd->screen_config);
#endif
        p_epd->epd->drv->sleep(p_epd->epd);
        nrf_delay_ms(100);
        if (operation.type == NRF_EPD_OP_CLEAR) epd_send_diag(p_epd, "clear:done");
        else if (operation.type == NRF_EPD_OP_REFRESH) epd_send_diag(p_epd, "picture:done");
#if INKTAG_NRF_IMAGE_STORE_ENABLED
        else if (operation.type == NRF_EPD_OP_IMAGE_SHOW) {
            char buf[32];
            GFX_format(buf, sizeof(buf), "img:show:done id=%u", (unsigned)operation.arg);
            epd_send_text(p_epd, buf);
        }
#endif
        else if (operation.arg == MODE_CALENDAR) epd_send_diag(p_epd, "calendar:done");
        else if (operation.arg == MODE_CLOCK) epd_send_diag(p_epd, "clock:done");
        else if (operation.arg == MODE_DEVICE_INFO) epd_send_diag(p_epd, "device-info:done");
        else epd_send_diag(p_epd, "gui:done");
    }

#if INKTAG_NRF_IMAGE_STORE_ENABLED
    /* Failure ends this attempt; an explicitly requested slideshow continues
     * after its normal interval. STOP during the operation leaves it inactive. */
    if (operation.type == NRF_EPD_OP_IMAGE_SHOW && m_slideshow_active)
        m_slideshow_next_at = timestamp() + m_slideshow_interval;
#endif

    EPD_BoardRelease();
    if (operation.type == NRF_EPD_OP_REFRESH) m_picture_io_active = false;
    EPD_GPIO_Uninit();
    inktag_nrf_health_display_end();
    EPD_SetAbortCallback(NULL);
#if defined(INKTAG_NRF_PROTOCOL_V2) && INKTAG_NRF_INDICATOR_ENABLED
    EPD_SetMaintenanceCallback(NULL);
    epd_v2_indicator_set_active(INKTAG_STATUS_SCENE_REFRESH, false);
    epd_v2_indicator_trigger(completion_status == INKTAG_STATUS_OK ?
        INKTAG_STATUS_SCENE_SUCCESS : INKTAG_STATUS_SCENE_ERROR);
#endif
    epd_v2_operation_complete(&operation, completion_status);
    m_epd_io_busy = false;
    m_epd_operation_active = false;
    memset(&m_epd_active_operation, 0, sizeof(m_epd_active_operation));
    m_epd_cancel_requested = false;
    app_feed_wdt();
    epd_schedule_dispatch();
}

static void epd_notifications_set(ble_epd_t *p_epd, bool enabled) {
    p_epd->is_notification_enabled = enabled;
    if (enabled) return;
    epd_v2_link_closed(p_epd);
    if (m_epd_operation_pending && m_epd_pending_operation.origin == 1u)
        m_epd_operation_pending = false;
    if (m_epd_operation_active && m_epd_active_operation.origin == 1u)
        m_epd_cancel_requested = true;
}

static void on_connect(ble_epd_t *p_epd, ble_evt_t *p_ble_evt) {
    p_epd->conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
#if !defined(INKTAG_BOARD_LAZY_DISPLAY_BUS) || !INKTAG_BOARD_LAZY_DISPLAY_BUS
    EPD_GPIO_Init();
#else
#endif
#if defined(INKTAG_NRF_PROTOCOL_V2) && INKTAG_NRF_INDICATOR_ENABLED
    epd_v2_indicator_trigger(INKTAG_STATUS_SCENE_CONNECTED);
#endif
}

static void on_disconnect(ble_epd_t *p_epd, ble_evt_t *p_ble_evt) {
    (void)p_ble_evt;
    p_epd->conn_handle = BLE_CONN_HANDLE_INVALID;
    p_epd->is_notification_enabled = false;

    epd_notifications_set(p_epd, false);
    /* Link loss ends BLE ownership. Local and background waveforms keep
     * their normal completion/cleanup owner even after the peer leaves. */
    if (m_epd_operation_active && m_epd_active_operation.origin != 1u) return;
    if (!m_epd_operation_active && !m_epd_io_busy) {
        if (EPD_BoardIsPrepared() && p_epd->epd) {
            p_epd->epd->drv->sleep(p_epd->epd);
            nrf_delay_ms(200u);
        }
        EPD_BoardRelease();
        m_picture_io_active = false;
        EPD_GPIO_Uninit();
    } else {
        m_epd_cancel_requested = true;
    }
}

static void epd_send_time(ble_epd_t* p_epd) {
    char response[20];
    size_t count = GFX_format(response, sizeof(response), "t=%" PRIu32, timestamp());
    (void)ble_epd_string_send(p_epd, (uint8_t*)response, (uint16_t)count);
}

static void epd_send_mtu(ble_epd_t* p_epd) {
    char response[48];
    const char *format = "mtu=%d rle=1";
    size_t count = GFX_format(response, sizeof(response), format, p_epd->max_data_len);
    (void)ble_epd_string_send(p_epd, (uint8_t*)response, (uint16_t)count);
}

static size_t epd_format_diag(ble_epd_t* p_epd, char* buf, size_t size, const char* tag) {
    uint8_t raw_busy = EPD_ReadBusy();
    uint8_t busy = p_epd->epd ? p_epd->epd->drv->read_busy(p_epd->epd) : raw_busy;
    return GFX_format(buf, size,
                      "%s drv=%02x active=%02x raw=%u busy=%u v=%u edge=%u assert_ms=%" PRIu32 " busy_ms=%" PRIu32 " pins=%02x,%02x,%02x,%02x,%02x,%02x,%02x wk=%02x en=%02x led=%02x",
                      tag,
                      p_epd->screen_config.driver_id,
                      p_epd->epd ? p_epd->epd->driver_id : 0xff,
                      raw_busy,
                      busy,
                      EPD_ReadVoltage(),
                      EPD_BusyDiagAsserted() ? 1 : 0,
                      EPD_BusyDiagAssertDelayMs(),
                      EPD_BusyDiagActiveMs(),
                      p_epd->config.mosi_pin,
                      p_epd->config.sclk_pin,
                      p_epd->config.cs_pin,
                      p_epd->config.dc_pin,
                      p_epd->config.rst_pin,
                      p_epd->config.busy_pin,
                      p_epd->config.bs_pin,
                      p_epd->config.wakeup_pin,
                      p_epd->config.en_pin,
                      p_epd->config.led_pin);
}

static void epd_send_diag(ble_epd_t* p_epd, const char* tag) {
    char buf[192];
    size_t length = epd_format_diag(p_epd, buf, sizeof(buf), tag);
    (void)length;
    epd_send_text(p_epd, buf);
}
static void epd_send_board(ble_epd_t* p_epd) {
    char buf[32] = {0};
    GFX_format(buf, sizeof(buf), "board=%s", INKTAG_BOARD_ID);
    ble_epd_string_send(p_epd, (uint8_t*)buf, strlen(buf));
}

static void epd_send_screen_config(ble_epd_t* p_epd) {
    uint8_t payload[EPD_SCREEN_WIRE_SIZE];
    if (epd_screen_config_encode(&p_epd->screen_config, payload) == EPD_SCREEN_STATUS_OK) {
        ble_epd_string_send(p_epd, payload, sizeof(payload));
    }
}
static volatile bool m_ghosting_stop = false;
#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED
static bool epd_button_action_supported(uint8_t action) {
    return action <= INKTAG_INPUT_ACTION_TOGGLE_CLEAR_PREVIOUS &&
           (INKTAG_NRF_BUTTON_ACTION_MASK & (1u << action)) != 0u;
}

static void epd_encode_settings_text(const uint8_t* value, uint8_t length,
                                     char* output, uint16_t capacity) {
    static const char hex[] = "0123456789ABCDEF";
    uint16_t out = 0u;
    uint8_t i;
    for (i = 0u; value != NULL && i < length && out + 1u < capacity; ++i) {
        uint8_t byte = value[i];
        bool plain = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                     (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
                     byte == '.' || byte == '~';
        if (plain) output[out++] = (char)byte;
        else if (out + 3u < capacity) {
            output[out++] = '%';
            output[out++] = hex[byte >> 4];
            output[out++] = hex[byte & 0x0Fu];
        } else break;
    }
    output[out] = '\0';
}

static void epd_send_device_settings(ble_epd_t* p_epd) {
    char name[EPD_DEVICE_NAME_MAX_BYTES + 1u] = {0};
    char encoded_name[EPD_DEVICE_NAME_MAX_BYTES * 3u + 1u];
    char encoded_label[EPD_DEVICE_LABEL_MAX_BYTES * 3u + 1u];
    char buf[176];
    ble_gap_addr_t addr;
    uint16_t name_length = EPD_DEVICE_NAME_MAX_BYTES;
    if (sd_ble_gap_device_name_get((uint8_t*)name, &name_length) != NRF_SUCCESS)
        name_length = 0u;
    name[name_length] = '\0';
    epd_encode_settings_text((const uint8_t*)name, (uint8_t)name_length,
                             encoded_name, sizeof(encoded_name));
    epd_encode_settings_text(m_device_settings.label, m_device_settings.label_length,
                             encoded_label, sizeof(encoded_label));
    if (sd_ble_gap_addr_get(&addr) == NRF_SUCCESS) {
        GFX_format(buf, sizeof(buf),
                   "settings:identity name=%s addr=%02X:%02X:%02X:%02X:%02X:%02X",
                   encoded_name, addr.addr[5], addr.addr[4], addr.addr[3],
                   addr.addr[2], addr.addr[1], addr.addr[0]);
    } else {
        GFX_format(buf, sizeof(buf), "settings:identity name=%s addr=none", encoded_name);
    }
    epd_send_text(p_epd, buf);
    GFX_format(buf, sizeof(buf), "settings:device name=%s", encoded_label);
    epd_send_text(p_epd, buf);
}

static void epd_stop_current_operation(ble_epd_t* p_epd) {
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    epd_slideshow_stop(p_epd, true);
#endif
    m_epd_operation_pending = false;
    m_native_timer_update_pending = false;
    m_epd_cancel_requested = true;
    m_ghosting_stop = true;
    if (!m_epd_operation_active && m_picture_io_active) {
        EPD_Reset(true, 10);
        EPD_BoardRelease();
        m_picture_io_active = false;
    }
    epd_send_text(p_epd, (m_epd_operation_active || m_epd_io_busy) ?
                  "epd:stopping" : "epd:stopped");
}

#if INKTAG_NRF_IMAGE_STORE_ENABLED
static bool epd_stored_image_replayable(ble_epd_t* p_epd, uint8_t id,
                                        bool slideshow_only) {
    inktag_image_store_header_t header;
    return inktag_image_store_get(id, &header) == INKTAG_IMAGE_STORE_OK &&
           epd_v2_image_record_visible(p_epd, id) &&
           epd_screen_config_matches(p_epd, header.screen_config) &&
           (!slideshow_only || header.content_type == INKTAG_IMAGE_CONTENT_IMAGE);
}

static bool epd_restore_input_display(ble_epd_t* p_epd,
                                      const epd_display_state_t* state,
                                      bool background) {
    if (state == NULL) return false;
    if (state->mode == EPD_DISPLAY_STATE_DEVICE_INFO) {
        (background ? epd_schedule_background_operation : epd_schedule_operation)(
            p_epd, NRF_EPD_OP_GUI, MODE_DEVICE_INFO, timestamp());
        return true;
    }
    if (state->mode == EPD_DISPLAY_STATE_CALENDAR ||
        state->mode == EPD_DISPLAY_STATE_CLOCK) {
        (background ? epd_schedule_background_operation : epd_schedule_operation)(
            p_epd, NRF_EPD_OP_GUI, state->mode, timestamp());
        return true;
    }
    if (state->mode == EPD_DISPLAY_STATE_CLEAR) {
        (background ? epd_schedule_background_operation : epd_schedule_operation)(
            p_epd, NRF_EPD_OP_CLEAR, 1u, timestamp());
        return true;
    }
    if (state->mode == EPD_DISPLAY_STATE_PICTURE &&
        epd_stored_image_replayable(p_epd, state->argument, false)) {
        (background ? epd_schedule_background_operation : epd_schedule_operation)(
            p_epd, NRF_EPD_OP_IMAGE_SHOW, state->argument, timestamp());
        return true;
    }
    if (state->mode == EPD_DISPLAY_STATE_SLIDESHOW) {
        uint8_t id = state->argument;
        if (!epd_stored_image_replayable(p_epd, id, true)) {
            m_slideshow_current_id = UINT8_MAX;
            if (!epd_slideshow_select_next(p_epd, &id)) return false;
        }
        m_slideshow_interval = state->interval_seconds >= 30u ?
                                   state->interval_seconds : 3600u;
        m_slideshow_active = true;
        m_slideshow_current_id = id;
        m_slideshow_next_at = timestamp() + m_slideshow_interval;
        (background ? epd_schedule_background_operation : epd_schedule_operation)(
            p_epd, NRF_EPD_OP_IMAGE_SHOW, id, timestamp());
        return true;
    }
    return false;
}
#endif

static void epd_process_button_action(ble_epd_t* p_epd,
                                      const inktag_input_binding_t* binding) {
    uint8_t action;
    if (binding == NULL) return;
    action = binding->action;
    if (action == INKTAG_INPUT_ACTION_TOGGLE_CLEAR_PREVIOUS) {
        if (!m_display_state_loaded || m_display_state.mode != EPD_DISPLAY_STATE_CLEAR) {
            if (m_display_state_loaded) {
                m_button_clear_return_state = m_display_state;
                m_button_clear_return_valid = true;
            }
#if INKTAG_NRF_IMAGE_STORE_ENABLED
            epd_slideshow_stop(p_epd, true);
#endif
            epd_schedule_operation(p_epd, NRF_EPD_OP_CLEAR, 1u, timestamp());
#if INKTAG_NRF_IMAGE_STORE_ENABLED
        } else if (m_button_clear_return_valid &&
                   epd_restore_input_display(p_epd, &m_button_clear_return_state,
                                             false)) {
            return;
#endif
        } else if (m_button_clear_return_valid &&
                   m_button_clear_return_state.mode == EPD_DISPLAY_STATE_DEVICE_INFO) {
            epd_schedule_operation(p_epd, NRF_EPD_OP_GUI, MODE_DEVICE_INFO, timestamp());
        } else if (m_button_clear_return_valid &&
                   (m_button_clear_return_state.mode == EPD_DISPLAY_STATE_CALENDAR ||
                    m_button_clear_return_state.mode == EPD_DISPLAY_STATE_CLOCK)) {
            epd_schedule_operation(p_epd, NRF_EPD_OP_GUI,
                                m_button_clear_return_state.mode, timestamp());
        } else {
            epd_send_text(p_epd, "button:return-missing");
        }
    } else if (action == INKTAG_INPUT_ACTION_NEXT_PAGE) {
#if INKTAG_NRF_IMAGE_STORE_ENABLED
        nrf_epd_operation_type_t operation;
        uint8_t argument;
        bool slideshow_step;
        if (epd_page_cycle_resolve(p_epd, &operation, &argument,
                                   &slideshow_step) &&
            epd_schedule_operation(p_epd, operation, argument, timestamp())) {
            epd_page_cycle_scheduled(p_epd, slideshow_step);
        }
#else
        epd_schedule_operation(p_epd, NRF_EPD_OP_GUI, MODE_CALENDAR,
                               timestamp());
#endif
    } else if (action == INKTAG_INPUT_ACTION_TOGGLE_SLIDESHOW) {
#if INKTAG_NRF_IMAGE_STORE_ENABLED
        if (m_slideshow_active) {
            epd_display_state_t return_state = m_button_slideshow_return_state;
            bool return_valid = m_button_slideshow_return_valid;
            epd_slideshow_stop(p_epd, false);
            m_button_slideshow_return_valid = false;
            epd_send_text(p_epd, "img:slideshow:stopped");
            if (return_valid &&
                epd_restore_input_display(p_epd, &return_state, false))
                return;
            epd_schedule_operation(p_epd, NRF_EPD_OP_GUI, MODE_DEVICE_INFO, timestamp());
            return;
        }
        {
            uint8_t first_id = UINT8_MAX;
            m_slideshow_current_id = UINT8_MAX;
            if (!epd_slideshow_select_next(p_epd, &first_id)) return;
            m_button_slideshow_return_valid =
                m_display_state_loaded &&
                m_display_state.mode != EPD_DISPLAY_STATE_SLIDESHOW;
            if (m_button_slideshow_return_valid)
                m_button_slideshow_return_state = m_display_state;
            if (m_slideshow_interval < 30u) m_slideshow_interval = 3600u;
            m_slideshow_active = true;
            m_slideshow_current_id = first_id;
            m_slideshow_next_at = timestamp() + m_slideshow_interval;
            epd_schedule_operation(p_epd, NRF_EPD_OP_IMAGE_SHOW, first_id,
                                timestamp());
        }
#endif
        return;
    } else {
        epd_send_text(p_epd, "button:unsupported");
    }
}

void ble_epd_handle_button_gesture(ble_epd_t* p_epd,
                                   inktag_button_gesture_t gesture) {
    /* Legacy custom fields remain stored, but InkTag executes stock defaults. */
    inktag_button_bindings_t stock;
    inktag_button_bindings_default(&stock);
    const inktag_input_binding_t* binding = inktag_button_binding(&stock, gesture);
    char buf[48];
    if (p_epd == NULL || binding == NULL) return;
    GFX_format(buf, sizeof(buf), "button:event gesture=%u action=%u",
               (unsigned)gesture, (unsigned)binding->action);
    epd_send_text(p_epd, buf);
    if (epd_v2_input_action_access(
            (uint8_t)(INKTAG_INPUT_SOURCE_BUTTON_SINGLE + gesture -
                      INKTAG_BUTTON_GESTURE_SINGLE),
            binding->action) != INKTAG_STATUS_OK) {
        epd_send_text(p_epd, "button:denied");
        return;
    }
    epd_process_button_action(p_epd, binding);
}
#endif

static bool epd_ghosting_abort_requested(void) {
    return m_ghosting_stop;
}

static void epd_ghosting_clear(void* p_event_data, uint16_t event_size) {
    epd_gui_update_event_t* event = (epd_gui_update_event_t*)p_event_data;
    ble_epd_t* p_epd = event->p_epd;
    uint16_t cycles = (uint16_t)event->timestamp;

    if (p_epd == NULL) return;
    if (p_epd->epd == NULL) p_epd->epd = epd_init(&p_epd->screen_config);
    if (p_epd->epd == NULL) return;

    m_epd_io_busy = true;
    EPD_SetAbortCallback(epd_ghosting_abort_requested);
    EPD_GPIO_Init();
    EPD_BoardPrepare();
    for (uint16_t i = 0; i < cycles && !m_ghosting_stop; i++) {
        app_feed_wdt();
        p_epd->epd->drv->init(p_epd->epd);
        p_epd->epd->drv->clear(p_epd->epd, true);
        p_epd->epd->drv->sleep(p_epd->epd);
        nrf_delay_ms(10000);
    }
    m_ghosting_stop = false;
    EPD_BoardRelease();
    EPD_GPIO_Uninit();
    EPD_SetAbortCallback(NULL);
    m_epd_io_busy = false;
}

static void epd_set_holidays(ble_epd_t* p_epd, const uint8_t* p_data, uint16_t length) {
    if (length < 4) return;
    uint16_t year = ((uint16_t)p_data[1] << 8) | p_data[2];
    uint8_t count = p_data[3];
    if (count > 128 || length < (uint16_t)(4 + count * 2)) return;

    uint16_t schedule[128];
    for (uint8_t i = 0; i < count; i++) {
        schedule[i] = ((uint16_t)p_data[4 + i * 2] << 8) | p_data[5 + i * 2];
    }
    GUI_SetHolidaySchedule(year, schedule, count);

    char buf[32] = {0};
    GFX_format(buf, sizeof(buf), "holidays=%u/%u", year, count);
    ble_epd_string_send(p_epd, (uint8_t*)buf, strlen(buf));
}

#if INKTAG_NRF_IMAGE_STORE_ENABLED
static void epd_capture_image_bytes(ble_epd_t* p_epd, const uint8_t* data,
                                    uint16_t length) {
    inktag_image_store_result_t result;
    if (!inktag_image_store_capture_active() || m_image_store_write_failed) return;
    result = inktag_image_store_append(data, length);
    if (result != INKTAG_IMAGE_STORE_OK) {
        m_image_store_write_failed = true;
        epd_send_image_store_result(p_epd, "write", result, 0u);
    }
}

static void epd_process_image_store_begin(ble_epd_t* p_epd,
                                          const uint8_t* data,
                                          uint16_t length) {
    epd_screen_config_t packet_screen;
    epd_screen_t resolved;
    uint32_t payload_size, segment_size[2];
    uint8_t expected_segments, content_type, id = 0u;
    uint16_t name_offset, name_length, i;
    char name[INKTAG_IMAGE_STORE_NAME_SIZE];
    inktag_image_store_result_t result;
    m_pending_picture_store_id = UINT8_MAX;
    if (length < EPD_IMAGE_BEGIN_V2_MIN_LENGTH ||
        (data[1] != 2u && data[1] != 3u) ||
        (data[1] == 3u && length < EPD_IMAGE_BEGIN_V3_MIN_LENGTH)) {
        epd_send_text(p_epd, "img:begin:error=bad-packet");
        return;
    }
    if (epd_screen_config_decode(&data[EPD_IMAGE_BEGIN_SCREEN_OFFSET],
                                 EPD_SCREEN_WIRE_SIZE, &packet_screen) !=
            EPD_SCREEN_STATUS_OK ||
        !epd_screen_config_matches(p_epd, &data[EPD_IMAGE_BEGIN_SCREEN_OFFSET]) ||
        epd_screen_resolve(&packet_screen, &resolved) != EPD_SCREEN_STATUS_OK) {
        epd_send_text(p_epd, "img:begin:error=screen-mismatch");
        return;
    }
    payload_size = epd_read_u32_le(&data[EPD_IMAGE_BEGIN_PAYLOAD_SIZE_OFFSET]);
    segment_size[0] = epd_read_u32_le(&data[EPD_IMAGE_BEGIN_SEGMENT0_SIZE_OFFSET]);
    segment_size[1] = epd_read_u32_le(&data[EPD_IMAGE_BEGIN_SEGMENT1_SIZE_OFFSET]);
    expected_segments = (resolved.transfer_format == EPD_SCREEN_TRANSFER_DUAL_1BPP ||
                         resolved.transfer_format == EPD_SCREEN_TRANSFER_PLANAR_2X1BPP ||
                         resolved.transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2X2BPP)
        ? 2u : 1u;
    if (data[2] != resolved.transfer_format || data[3] != expected_segments ||
        payload_size != resolved.frame_bytes) {
        epd_send_text(p_epd, "img:begin:error=size-mismatch");
        return;
    }
    memset(name, 0, sizeof(name));
    content_type = data[1] == 3u ? data[EPD_IMAGE_BEGIN_CONTENT_TYPE_OFFSET]
                                  : INKTAG_IMAGE_CONTENT_IMAGE;
    name_offset = data[1] == 3u ? EPD_IMAGE_BEGIN_V3_NAME_OFFSET
                                 : EPD_IMAGE_BEGIN_V2_NAME_OFFSET;
    name_length = length > name_offset ? (uint16_t)(length - name_offset) : 0u;
    if (name_length >= sizeof(name)) name_length = sizeof(name) - 1u;
    for (i = 0u; i < name_length; ++i) {
        uint8_t c = data[name_offset + i];
        name[i] = ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                   (c >= 'a' && c <= 'z') || c == '-' || c == '_') ? (char)c : '_';
    }
    result = inktag_image_store_begin(
        &packet_screen, data[2], data[3], data[4], content_type, segment_size,
        payload_size, epd_read_u32_le(&data[EPD_IMAGE_BEGIN_CRC32_OFFSET]),
        timestamp(), name, &id);
    if (result != INKTAG_IMAGE_STORE_OK) {
        epd_send_image_store_result(p_epd, "begin", result, id);
        return;
    }
    m_image_store_write_failed = false;
}
#endif

#include "inktag_nrf_protocol_v2.inc"

static bool epd_device_info_activated(void) {
    return epd_brand_edition() != INKTAG_BRAND_EDITION_DEFAULT;
}

static inktag_brand_edition_t epd_brand_edition(void) {
    return INKTAG_BRAND_EDITION_DEFAULT;
}

static bool epd_brand_overlay_chunk(ble_epd_t* p_epd,
                                    uint8_t transfer_format,
                                    uint8_t segment_index,
                                    uint32_t segment_offset,
                                    uint8_t* data,
                                    uint16_t length, uint8_t reading_rotation) {
    if (p_epd == NULL) return false;
    return inktag_brand_overlay_apply_oriented(
        &p_epd->screen_config, transfer_format, segment_index, segment_offset,
        data, length, epd_brand_edition(), reading_rotation);
}


static void epd_service_on_write(ble_epd_t *p_epd, uint8_t *p_data, uint16_t length) {
    if (!p_data || !length) return;
    /* The queue copies the span before the SoftDevice event is released.
     * Bearer proofs and QR contents must never reach a diagnostic dump. */
    epd_v2_queue_packet(p_epd, p_data, length);
}

/* Non-V2 command adapters: implementation. */
/* End non-V2 command adapters. */

static void on_write(ble_epd_t *p_epd, ble_evt_t *p_ble_evt) {
    ble_gatts_evt_write_t *write = &p_ble_evt->evt.gatts_evt.params.write;
    if (write->handle == p_epd->char_handles.cccd_handle && write->len == 2u) {
        epd_notifications_set(p_epd, ble_srv_is_notification_enabled(write->data));
    } else if (write->handle == p_epd->char_handles.value_handle) {
        epd_service_on_write(p_epd, write->data, write->len);
    }
}

#if defined(INKTAG_NRF_SDK17)
void ble_epd_evt_handler(ble_evt_t const* p_ble_evt, void* p_context) {
    if (p_context == NULL || p_ble_evt == NULL) return;

    ble_epd_t* p_epd = (ble_epd_t*)p_context;
    ble_epd_on_ble_evt(p_epd, (ble_evt_t*)p_ble_evt);
}
#endif

void ble_epd_on_ble_evt(ble_epd_t *p_epd, ble_evt_t *p_ble_evt) {
    if (!p_epd || !p_ble_evt) return;
    switch (p_ble_evt->header.evt_id) {
        case BLE_GATTS_EVT_WRITE:
            on_write(p_epd, p_ble_evt);
            return;
        case BLE_GAP_EVT_CONNECTED:
            on_connect(p_epd, p_ble_evt);
            return;
        case BLE_GAP_EVT_DISCONNECTED:
            on_disconnect(p_epd, p_ble_evt);
            return;

        default:
            return;
    }
}

static uint32_t epd_service_init(ble_epd_t *p_epd) {
    static const ble_uuid128_t base = BLE_UUID_EPD_SVC_BASE;
    ble_uuid_t identity = {.uuid = BLE_UUID_EPD_SVC};
    uint32_t status = sd_ble_uuid_vs_add(&base, &identity.type);
    if (status != NRF_SUCCESS) return status;
    status = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &identity,
                                      &p_epd->service_handle);
    if (status != NRF_SUCCESS) return status;

    ble_add_char_params_t spec = {
        .uuid = BLE_UUID_EPD_CHAR, .uuid_type = identity.type,
        .max_len = BLE_EPD_MAX_DATA_LEN, .init_len = 1u, .is_var_len = true,
        .char_props = {.write = 1u, .write_wo_resp = 1u, .notify = 1u},
        .read_access = SEC_OPEN, .write_access = SEC_OPEN, .cccd_write_access = SEC_OPEN,
    };
    status = characteristic_add(p_epd->service_handle, &spec, &p_epd->char_handles);
    if (status != NRF_SUCCESS) return status;

    static uint8_t version[] = INKTAG_VERSION_GATT_BYTES;
    spec = (ble_add_char_params_t){
        .uuid = BLE_UUID_APP_VER, .uuid_type = identity.type,
        .max_len = sizeof(version), .init_len = sizeof(version), .p_init_value = version,
        .char_props = {.read = 1u}, .read_access = SEC_OPEN,
    };
    return characteristic_add(p_epd->service_handle, &spec, &p_epd->app_ver_handles);
}

void ble_epd_sleep_prepare(ble_epd_t* p_epd) {
    EPD_LED_OFF();
    // Prepare wakeup pin
    /* N1P1's native slot 8 is RGB green, never a wakeup input. */
    (void)p_epd;
}

uint32_t ble_epd_init(ble_epd_t* p_epd) {
    if (p_epd == NULL) return NRF_ERROR_NULL;
    {
        uint32_t err_code = epd_v2_system_action_timer_init();
        if (err_code != NRF_SUCCESS) return err_code;
    }

    // Initialize the service structure.
    p_epd->max_data_len = BLE_EPD_MAX_DATA_LEN;
    p_epd->conn_handle = BLE_CONN_HANDLE_INVALID;
    p_epd->is_notification_enabled = false;

    epd_config_init(&p_epd->config);
    epd_config_read(&p_epd->config);
#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED
    bool settings_valid = epd_device_settings_read(&m_device_settings);
    if (!settings_valid) {
        epd_device_settings_default(&m_device_settings);
    }
#else
    bool settings_valid = true;
#endif

    /* Board wiring and screen identity are deliberately separate records. */
#if !defined(INKTAG_NRF_COMPACT_STORE)
    bool use_default_config = epd_config_empty(&p_epd->config);
    if (use_default_config) {
        uint8_t cfg[] = INKTAG_BOARD_DEFAULT_EPD_CONFIG;
        memset(&p_epd->config, 0xFF, sizeof(p_epd->config));
        memcpy(&p_epd->config, cfg, sizeof(cfg));
        if (p_epd->config.display_mode == 0xFF) {
#if defined(INKTAG_BOARD_SAFE_BRINGUP)
            p_epd->config.display_mode = MODE_PICTURE;
#else
            p_epd->config.display_mode = MODE_CALENDAR;
#endif
        }
        if (p_epd->config.week_start == 0xFF)
            p_epd->config.week_start = INKTAG_DISPLAY_SETTINGS_DEFAULT_WEEK_START;
    }
#endif

    if (!epd_display_state_read(&m_display_state)) {
        inktag_display_state_default(&m_display_state);
#if defined(INKTAG_NRF_COMPACT_STORE)
        if (p_epd->config.display_mode <= EPD_DISPLAY_STATE_CLOCK)
            m_display_state.mode = p_epd->config.display_mode;
#endif
    }
    m_display_state_loaded = true;
    epd_v2_display_settings_init(p_epd);
    p_epd->config.display_mode = epd_display_state_driver_mode(m_display_state.mode);

    /* A configured Full provides the factory/user records. A bare base has
     * no guessed panel and performs no provisioning writes or redraw. */
    memset(&p_epd->screen_config, 0, sizeof(p_epd->screen_config));
    /* A disabled bus does not erase the independently stored panel identity:
     * restoring wiring must work without a second reboot or screen write. */
    p_epd->epd = settings_valid && !epd_config_empty(&p_epd->config) &&
        epd_screen_config_read(&p_epd->screen_config) &&
        p_epd->config.mosi_pin!=255u ?
        epd_find(&p_epd->screen_config) : NULL;

#if INKTAG_NRF_IMAGE_STORE_ENABLED
    (void)inktag_image_store_init();
#endif
    {
        uint32_t saved_timestamp = 0u;
        /* main() restores the local timestamp after service initialization.
         * Load only the paired timezone here: calling set_timestamp() would
         * start the clock early and rewrite the same FDS record at every boot. */
        (void)epd_time_state_read(&saved_timestamp, &m_v2_timezone_minutes);
    }

    // load config
    EPD_GPIO_Load(&p_epd->config);
#if defined(INKTAG_NRF_PROTOCOL_V2) && INKTAG_NRF_INDICATOR_ENABLED
    epd_v2_indicator_init();
#endif

    // Add the service.
    return epd_service_init(p_epd);
}

uint32_t ble_epd_string_send(ble_epd_t *p_epd, uint8_t *p_string, uint16_t length) {
    if (!p_epd->is_notification_enabled || p_epd->conn_handle == BLE_CONN_HANDLE_INVALID)
        return NRF_ERROR_INVALID_STATE;
    if (length > p_epd->max_data_len) return NRF_ERROR_INVALID_PARAM;

    ble_gatts_hvx_params_t packet = {
        .handle = p_epd->char_handles.value_handle, .type = BLE_GATT_HVX_NOTIFICATION,
        .p_len = &length, .p_data = p_string,
    };
    return sd_ble_gatts_hvx(p_epd->conn_handle, &packet);

}

static void epd_native_timer_update_try(ble_epd_t* p_epd) {
    if (!m_native_timer_update_pending || p_epd == NULL) return;
    bool native_mode = p_epd->config.display_mode == MODE_CALENDAR ||
                       p_epd->config.display_mode == MODE_CLOCK;
    bool blocked = m_picture_io_active || m_epd_operation_active ||
                   m_epd_operation_pending;
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    blocked = blocked || inktag_image_store_busy();
#endif
    if (!native_mode) {
        m_native_timer_update_pending = false;
    } else if (!blocked) {
        uint32_t operation_timestamp = m_native_timer_update_timestamp;
        m_native_timer_update_pending = false;
        /* Refusal consumes this boundary; do not re-sample ADC on every
         * main-loop turn. Busy foreground work retains/coalesces pending. */
        (void)epd_schedule_background_operation(p_epd, NRF_EPD_OP_GUI,
                                               p_epd->config.display_mode,
                                               operation_timestamp);
    }
}

void ble_epd_process(ble_epd_t* p_epd) {
    if (p_epd == NULL) return;
    epd_v2_display_read_yield();
    epd_v2_process_system_action();
    epd_v2_tick(p_epd);
#if INKTAG_NRF_INDICATOR_ENABLED
    epd_v2_indicator_tick();
#endif
    epd_native_timer_update_try(p_epd);
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    inktag_image_store_event_t event;
    inktag_image_store_process();
    while (inktag_image_store_take_event(&event)) {
        (void)epd_v2_handle_store_event(p_epd, &event);
    }
#else
#endif
}

bool ble_epd_startup_complete(void) {
#if defined(INKTAG_NRF_PROTOCOL_V2) && INKTAG_NRF_INDICATOR_ENABLED
    epd_v2_indicator_tick();
    return m_v2_indicator_runtime.config.policy == INKTAG_STATUS_POLICY_OFF ||
           m_v2_indicator_runtime.transient_scene !=
           INKTAG_STATUS_SCENE_STARTUP;
#else
    return true;
#endif
}

void ble_epd_application_ready(void) {
#if defined(INKTAG_NRF_PROTOCOL_V2) && INKTAG_NRF_INDICATOR_ENABLED
    /* This is deliberately later than Recovery's image validation and local
     * activation: the application has initialized services, started ordinary
     * advertising and checkpointed any installed-image identity. */
    inktag_status_role_ready(&m_v2_indicator_runtime,
                             INKTAG_STATUS_READY_APPLICATION,
                             app_monotonic_now_ms());
#endif
}

void ble_epd_on_timer(ble_epd_t* p_epd, uint32_t previous_timestamp,
                      uint32_t timestamp, bool force_update) {
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    if (m_slideshow_active &&
        m_slideshow_next_at != 0u &&
        (int32_t)(timestamp - m_slideshow_next_at) >= 0 &&
        !m_picture_io_active && !m_epd_operation_active &&
        !m_epd_operation_pending && !inktag_image_store_busy()) {
        uint8_t next_id = UINT8_MAX;
        m_slideshow_next_at = timestamp + m_slideshow_interval;
        if (epd_slideshow_select_next(p_epd, &next_id) &&
            next_id != m_slideshow_current_id) {
            /* Current ID changes only after the dispatcher succeeds. */
            (void)epd_schedule_background_operation(p_epd, NRF_EPD_OP_IMAGE_SHOW,
                                                    next_id, timestamp);
        }
    }
#endif
    // Update calendar on 00:00:00, clock on every minute
    bool native_mode = p_epd->config.display_mode == MODE_CALENDAR ||
                       p_epd->config.display_mode == MODE_CLOCK;
    bool update_due = (force_update && native_mode) ||
                      (p_epd->config.display_mode == MODE_CALENDAR &&
                       inktag_time_boundary_crossed(previous_timestamp,
                                                    timestamp, 86400u)) ||
                      (p_epd->config.display_mode == MODE_CLOCK &&
                       inktag_time_boundary_crossed(previous_timestamp,
                                                    timestamp, 60u));
    if (update_due) {
        // Collapse any number of crossed boundaries into one refresh after
        // display, Flash or radio-driven work releases the main loop.
        m_native_timer_update_timestamp = timestamp;
        m_native_timer_update_pending = true;
        epd_native_timer_update_try(p_epd);
    }
}

bool ble_epd_restore_display(ble_epd_t* p_epd, uint32_t timestamp,
                             bool first_display_requested,
                             bool ota_install_boot,
                             bool startup_complete) {
    inktag_boot_display_plan_t plan;
    if (p_epd == NULL || !m_display_state_loaded) return true;
    plan = inktag_boot_display_plan(first_display_requested, ota_install_boot,
                                    &m_display_state);
    /* The journal consumes the one-shot marker first. Defer for startup or
     * foreground ownership, then attempt once; a refusal must not spin main. */
    if (plan.target == INKTAG_BOOT_DISPLAY_NONE ||
        (plan.defer_until_startup_complete && !startup_complete))
        return plan.target == INKTAG_BOOT_DISPLAY_NONE;
    if (m_picture_io_active || m_epd_operation_active || m_epd_operation_pending
#if INKTAG_NRF_IMAGE_STORE_ENABLED
        || inktag_image_store_busy()
#endif
    ) return false;
    nrf_epd_operation_type_t operation = NRF_EPD_OP_GUI;
    uint8_t argument = plan.target == INKTAG_BOOT_DISPLAY_DEVICE_INFO ?
                           EPD_DISPLAY_STATE_DEVICE_INFO : m_display_state.mode;
    if (argument == EPD_DISPLAY_STATE_CLEAR) {
        operation = NRF_EPD_OP_CLEAR;
        argument = m_display_state.argument <= 1u ? m_display_state.argument : 1u;
    }
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    else if (argument == EPD_DISPLAY_STATE_PICTURE ||
             argument == EPD_DISPLAY_STATE_SLIDESHOW) {
        bool slideshow = argument == EPD_DISPLAY_STATE_SLIDESHOW;
        argument = m_display_state.argument;
        if (slideshow) {
            m_slideshow_interval = m_display_state.interval_seconds >= 30u ?
                                       m_display_state.interval_seconds : 3600u;
            m_slideshow_active = true;
            m_slideshow_current_id = argument; /* Retained last shown image. */
            m_slideshow_next_at = timestamp + m_slideshow_interval;
        }
        if (!epd_stored_image_replayable(p_epd, argument, slideshow) &&
            (!slideshow || !epd_slideshow_select_next(p_epd, &argument)))
            return true;
        operation = NRF_EPD_OP_IMAGE_SHOW;
    }
#endif
    /* Keep persisted modes until clear/resource dispatch is resolved:
     * GUI DEVICE_INFO=3 shares its value with persisted CLEAR=3. */
    else if (argument == EPD_DISPLAY_STATE_DEVICE_INFO) argument = MODE_DEVICE_INFO;
    else if (argument != MODE_CALENDAR && argument != MODE_CLOCK) return true;
    (void)epd_schedule_background_operation(p_epd, operation, argument, timestamp);
    return true;
}
