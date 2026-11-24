#include "inktag_gui_assets.h"

#include <stddef.h>

#include "inktag_gui_fonts.h"
#include "inktag_board.h"
#include "inktag_nrf_external_flash.h"
#include "inktag_protocol_core.h"
#include "inktag_nrf_gui_asset_binding.h"
#if INKTAG_NRF_GUI_RAM_TABLES
#include "inktag_nrf_gui_tables.h"
uint8_t inktag_nrf_gui_tables[INKTAG_NRF_GUI_TABLES_SIZE];
#define ASSET_PAYLOAD_SIZE (INKTAG_GUI_FONT_ASSET_PAYLOAD_SIZE + INKTAG_NRF_GUI_TABLES_SIZE)
#define ASSET_SCHEMA INKTAG_GUI_FONT_ASSET_SCHEMA
#else
#define ASSET_PAYLOAD_SIZE INKTAG_GUI_FONT_ASSET_PAYLOAD_SIZE
#define ASSET_SCHEMA INKTAG_GUI_FONT_ASSET_SCHEMA
#endif

#if !defined(INKTAG_BOARD_EXTERNAL_FLASH_JEDEC_ID) ||  !defined(INKTAG_BOARD_EXTERNAL_FLASH_ASSET_OFFSET)
#error "external font assets require exact-board Flash identity and region"
#endif

#define CACHE_SIZE 256u
/* Glyph metrics/index walks touch short, scattered spans. Smaller lines avoid
 * reading 64 software-SPI bytes for a two-byte lookup. Keep the data budget;
 * the sixteen line tags cost 48 bytes more than the former four tags. */
#define CACHE_WAYS 16u
#define CACHE_LINE_SIZE (CACHE_SIZE / CACHE_WAYS)

static uint8_t s_cache[CACHE_SIZE];
static uint32_t s_cache_virtual_base[CACHE_WAYS];
static uint8_t s_cache_cursor;
static int8_t s_validated = -1;
static bool s_session_open;
static bool s_read_failed;

#if defined(INKTAG_BOARD_LAZY_DISPLAY_BUS) && INKTAG_BOARD_LAZY_DISPLAY_BUS
extern void ble_epd_gui_maintenance(void);
#endif

static uint32_t read_u32_le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool validate_assets(void) {
    uint8_t manifest[INKTAG_GUI_FONT_ASSET_HEADER_SIZE];
    uint32_t crc = 0xFFFFFFFFu;

    if (!inktag_nrf_external_flash_read(
            INKTAG_BOARD_EXTERNAL_FLASH_ASSET_OFFSET, manifest,
            sizeof(manifest))) return false;
    if (read_u32_le(&manifest[0]) != INKTAG_GUI_FONT_ASSET_MAGIC ||
        read_u32_le(&manifest[4]) != ASSET_SCHEMA ||
        read_u32_le(&manifest[8]) != ASSET_PAYLOAD_SIZE ||
        read_u32_le(&manifest[12]) != INKTAG_NRF_GUI_EXPECTED_CRC)
        return false;

    for (uint32_t offset = 0u;
         offset < ASSET_PAYLOAD_SIZE; offset += CACHE_SIZE) {
        uint16_t length = (uint16_t)(ASSET_PAYLOAD_SIZE - offset);
        if (length > CACHE_SIZE) length = CACHE_SIZE;
        if (!inktag_nrf_external_flash_read(
                INKTAG_BOARD_EXTERNAL_FLASH_ASSET_OFFSET +
                    INKTAG_GUI_FONT_ASSET_HEADER_SIZE + offset,
                s_cache, length)) return false;
        crc = inktag_crc32_update(crc, s_cache, length);
    }
    if ((crc ^ 0xFFFFFFFFu) != read_u32_le(&manifest[12])) return false;
#if INKTAG_NRF_GUI_RAM_TABLES
    return inktag_nrf_external_flash_read(
        INKTAG_BOARD_EXTERNAL_FLASH_ASSET_OFFSET + INKTAG_GUI_FONT_ASSET_HEADER_SIZE +
            INKTAG_GUI_FONT_ASSET_PAYLOAD_SIZE,
        inktag_nrf_gui_tables, sizeof(inktag_nrf_gui_tables));
#else
    return true;
#endif
}

bool inktag_gui_assets_begin(void) {
    if (s_session_open) return true;
    if (s_validated == 0) return false;
    if (!inktag_nrf_external_flash_begin()) return false;
    if (s_validated < 0) {
        s_validated = validate_assets() ? 1 : 0;
        if (s_validated == 0) {
            inktag_nrf_external_flash_end();
            return false;
        }
    }
    s_session_open = true;
    s_read_failed = false;
    return true;
}

bool inktag_nrf_gui_assets_ok(void) {
    return s_session_open && s_validated == 1 && !s_read_failed;
}

bool inktag_gui_asset_font_read_failed(void) {
    return s_read_failed;
}

bool inktag_gui_assets_healthy(void) {
    return inktag_nrf_gui_assets_ok();
}

void inktag_gui_assets_end(void) {
    if (!s_session_open) return;
    inktag_nrf_external_flash_end();
    s_session_open = false;
}

uint8_t inktag_gui_asset_read_byte(const uint8_t *address) {
    uint32_t virtual_address = (uint32_t)(uintptr_t)address;
    uint32_t first = INKTAG_GUI_FONT_ASSET_VIRTUAL_BASE +
                     INKTAG_GUI_FONT_ASSET_HEADER_SIZE;
    uint32_t limit = first + INKTAG_GUI_FONT_ASSET_PAYLOAD_SIZE;
    if (virtual_address < first || virtual_address >= limit)
        return *address;
    if (!s_session_open || s_validated != 1) {
        s_read_failed = true;
        return 0xFFu;
    }
    uint32_t offset = virtual_address - first;
    uint32_t aligned = offset & ~(CACHE_LINE_SIZE - 1u);
    uint32_t base = first + aligned;
    for (uint8_t way = 0u; way < CACHE_WAYS; way++) {
        if (s_cache_virtual_base[way] == base)
            return s_cache[way * CACHE_LINE_SIZE + offset - aligned];
    }
    {
        uint8_t way = s_cache_cursor++ & (CACHE_WAYS - 1u);
        uint16_t length = (uint16_t)(INKTAG_GUI_FONT_ASSET_PAYLOAD_SIZE - aligned);
        if (length > CACHE_LINE_SIZE) length = CACHE_LINE_SIZE;
#if defined(INKTAG_BOARD_LAZY_DISPLAY_BUS) && INKTAG_BOARD_LAZY_DISPLAY_BUS
        ble_epd_gui_maintenance();
#endif
        if (!inktag_nrf_external_flash_read(
                INKTAG_BOARD_EXTERNAL_FLASH_ASSET_OFFSET +
                    INKTAG_GUI_FONT_ASSET_HEADER_SIZE + aligned,
                s_cache + way * CACHE_LINE_SIZE, length)) {
            s_cache_virtual_base[way] = 0u;
            s_read_failed = true;
            return 0xFFu;
        }
        s_cache_virtual_base[way] = base;
        return s_cache[way * CACHE_LINE_SIZE + offset - aligned];
    }
}
