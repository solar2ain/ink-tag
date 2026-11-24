/* DA normal source projection v1 */
#include "inktag_gui_assets.h"

#include <stddef.h>
#include <string.h>

#include "inktag_gui_fonts.h"
#include "inktag_da_default_qr.h"
#include "inktag_board.h"
#include "inktag_protocol_core.h"
#include "spi_flash.h"

extern void app_service_ble(void);

#if !defined(INKTAG_BOARD_EXTERNAL_FLASH_ASSET_A_START) || \
    !defined(INKTAG_BOARD_EXTERNAL_FLASH_ASSET_A_SIZE) || \
    !defined(INKTAG_BOARD_EXTERNAL_FLASH_ASSET_B_START) || \
    !defined(INKTAG_BOARD_EXTERNAL_FLASH_ASSET_B_SIZE) || \
    !defined(INKTAG_BOARD_EXTERNAL_FLASH_ASSET_IMAGE_SIZE)
#error "DA external GUI assets require exact-board split Flash regions"
#endif

#define CACHE_LINE_SIZE 256u
#ifndef INKTAG_BOARD_GUI_ASSET_CACHE_LINES
#define INKTAG_BOARD_GUI_ASSET_CACHE_LINES 1u
#endif

/* u8g2 repeatedly moves between the font header, Unicode index and glyph
 * bodies. A single line made a paged 768x552 calendar issue several thousand
 * serial-Flash reads. Keep a small fully-associative cache inside the same
 * fixed SRAM budget as the DA GUI page buffer; immutable assets let entries
 * survive power-down between render sessions. */
static uint8_t s_cache[INKTAG_BOARD_GUI_ASSET_CACHE_LINES][CACHE_LINE_SIZE]
    ;
/* The legacy payload has 42 lines (48 with the candidate tail). Line+1 is an unambiguous valid tag;
 * zero-initialized BSS means empty without retaining full virtual addresses. */
static uint8_t s_cache_line[INKTAG_BOARD_GUI_ASSET_CACHE_LINES];
static uint8_t s_cache_next;
static uint8_t s_read_service_count;
static int8_t s_validated = -1;
static bool s_session_open;
static bool s_session_failed;

_Static_assert(INKTAG_BOARD_EXTERNAL_FLASH_ASSET_A_SIZE +
                   INKTAG_BOARD_EXTERNAL_FLASH_ASSET_B_SIZE ==
               INKTAG_BOARD_EXTERNAL_FLASH_ASSET_IMAGE_SIZE,
               "DA GUI asset spans must exactly cover the packaged image");
_Static_assert(INKTAG_GUI_FONT_ASSET_HEADER_SIZE +
                   INKTAG_GUI_FONT_ASSET_PAYLOAD_SIZE <=
               INKTAG_BOARD_EXTERNAL_FLASH_ASSET_IMAGE_SIZE,
               "DA GUI asset image cannot hold the common payload");
_Static_assert(INKTAG_BOARD_GUI_ASSET_CACHE_LINES > 0u &&
                   INKTAG_BOARD_GUI_ASSET_CACHE_LINES <= UINT8_MAX,
               "DA GUI asset cache count exceeds bounded index");
_Static_assert((INKTAG_GUI_FONT_ASSET_PAYLOAD_SIZE + CACHE_LINE_SIZE - 1u) /
                   CACHE_LINE_SIZE < UINT8_MAX,
               "DA GUI asset cache line tag exceeds one byte");
_Static_assert(INKTAG_DA_DEFAULT_QR_OFFSET >=
                   INKTAG_GUI_FONT_ASSET_HEADER_SIZE + INKTAG_GUI_FONT_ASSET_PAYLOAD_SIZE &&
               INKTAG_DA_DEFAULT_QR_OFFSET + INKTAG_DA_DEFAULT_QR_SIZE <=
                   INKTAG_BOARD_EXTERNAL_FLASH_ASSET_IMAGE_SIZE,
               "DA default QR must remain within the existing erased asset tail");

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Keep one bounded adapter for validation and cached glyph reads. Sharing its
 * code avoids duplicating the two-span Flash checks in the SRAM-loaded App;
 * the static stack bound is checked by the DA build after linking. */
static bool flash_read(
    uint32_t image_offset, uint8_t *data, uint16_t length)
{
    uint32_t actual = 0u;
    uint32_t physical;
    if (image_offset > INKTAG_BOARD_EXTERNAL_FLASH_ASSET_IMAGE_SIZE ||
        length > INKTAG_BOARD_EXTERNAL_FLASH_ASSET_IMAGE_SIZE - image_offset)
        return false;
    if (length == 0u) return true;
    /* There are exactly two spans: at most one crossing, never a loop or
     * recursive read. This keeps less live state on the renderer's stack. */
    if (image_offset < INKTAG_BOARD_EXTERNAL_FLASH_ASSET_A_SIZE) {
        uint32_t available = INKTAG_BOARD_EXTERNAL_FLASH_ASSET_A_SIZE - image_offset;
        physical = INKTAG_BOARD_EXTERNAL_FLASH_ASSET_A_START + image_offset;
        if (length > available) {
            if (spi_flash_read_data(data, physical,
                    available, &actual) != SPI_FLASH_ERR_OK || actual != available)
                return false;
            data += available;
            length = (uint16_t)(length - available);
            physical = INKTAG_BOARD_EXTERNAL_FLASH_ASSET_B_START;
        }
    } else {
        physical = INKTAG_BOARD_EXTERNAL_FLASH_ASSET_B_START + image_offset -
                   INKTAG_BOARD_EXTERNAL_FLASH_ASSET_A_SIZE;
    }
    actual = 0u;
    return spi_flash_read_data(data, physical, length, &actual) ==
               SPI_FLASH_ERR_OK && actual == length;
}

static bool validate_assets(void)
{
    uint8_t manifest[INKTAG_GUI_FONT_ASSET_HEADER_SIZE];
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t offset;

    if (!flash_read(0u, manifest, sizeof(manifest))) return false;
    if (read_u32_le(&manifest[0]) != INKTAG_GUI_FONT_ASSET_MAGIC ||
        read_u32_le(&manifest[4]) != INKTAG_GUI_FONT_ASSET_SCHEMA ||
        read_u32_le(&manifest[8]) != INKTAG_GUI_FONT_ASSET_PAYLOAD_SIZE)
        return false;

    for (offset = 0u; offset < INKTAG_GUI_FONT_ASSET_PAYLOAD_SIZE;
         offset += CACHE_LINE_SIZE) {
        uint16_t length =
            (uint16_t)(INKTAG_GUI_FONT_ASSET_PAYLOAD_SIZE - offset);
        if (length > CACHE_LINE_SIZE) length = CACHE_LINE_SIZE;
        if (!flash_read(INKTAG_GUI_FONT_ASSET_HEADER_SIZE + offset,
                        s_cache[0], length)) return false;
        crc = inktag_crc32_update(crc, s_cache[0], length);
    }
    return (crc ^ 0xFFFFFFFFu) == read_u32_le(&manifest[12]);
}

bool inktag_gui_assets_begin(void)
{
    if (s_session_open) return !s_session_failed;
    if (s_validated == 0) return false;
    if (spi_flash_release_from_power_down() != SPI_FLASH_ERR_OK) return false;
    if (s_validated < 0) {
        s_validated = validate_assets() ? 1 : 0;
        if (s_validated == 0) {
            (void)spi_flash_power_down();
            return false;
        }
    }
    s_session_open = true;
    s_session_failed = false;
    return true;
}

bool inktag_gui_assets_healthy(void)
{
    return s_session_open && !s_session_failed;
}

void inktag_gui_assets_end(void)
{
    if (!s_session_open) return;
    (void)spi_flash_power_down();
    s_session_open = false;
}


uint8_t inktag_gui_asset_read_byte(const uint8_t *address)
{
    uint32_t virtual_address = (uint32_t)(uintptr_t)address;
    uint32_t first = INKTAG_GUI_FONT_ASSET_VIRTUAL_BASE +
                     INKTAG_GUI_FONT_ASSET_HEADER_SIZE;
    uint32_t payload_size = INKTAG_GUI_FONT_ASSET_PAYLOAD_SIZE;
    uint32_t limit = first + payload_size;
    uint32_t offset;
    uint32_t aligned;
    uint8_t tag;
    uint8_t victim;
    uint8_t index;
    if (virtual_address < first || virtual_address >= limit) return *address;
    if (!inktag_gui_assets_healthy() || s_validated != 1) return 0xFFu;
    /* A large GUI page can spend longer than the connection supervision
     * window inside u8g2 before its output callback runs. Keep the radio and
     * watchdog live independently of cache hits so a successful refresh can
     * always deliver its terminal V2 event to the initiating client. */
    if (++s_read_service_count == 0u) {
        app_service_ble();
        /* A serviced cancellation may close the owning render session. */
        if (!s_session_open) return 0xFFu;
    }
    offset = virtual_address - first;
    aligned = offset & ~(CACHE_LINE_SIZE - 1u);
    tag = (uint8_t)(aligned / CACHE_LINE_SIZE + 1u);
    offset -= aligned;
    for (index = 0u; index < INKTAG_BOARD_GUI_ASSET_CACHE_LINES; ++index) {
        if (s_cache_line[index] == tag)
            return s_cache[index][offset];
    }
    victim = s_cache_next;
    {
        uint16_t length =
            (uint16_t)(payload_size - aligned);
        if (length > CACHE_LINE_SIZE) length = CACHE_LINE_SIZE;
        /* A failed/short refill may already have overwritten victim bytes.
         * Never keep its old tag eligible after a partial transport failure. */
        s_cache_line[victim] = 0u;
        if (!flash_read(INKTAG_GUI_FONT_ASSET_HEADER_SIZE + aligned,
                        s_cache[victim], length)) {
            s_session_failed = true;
            return 0xFFu;
        }
        s_cache_line[victim] = tag;
        s_cache_next = (uint8_t)(victim + 1u);
        if (s_cache_next >= INKTAG_BOARD_GUI_ASSET_CACHE_LINES)
            s_cache_next = 0u;
    }
    return s_cache[victim][offset];
}
