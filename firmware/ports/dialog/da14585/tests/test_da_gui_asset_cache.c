#include <assert.h>
#include <stdio.h>
#include <string.h>
#define INKTAG_GUI_ASSET_READ_FAILURE_AWARE 1
#ifndef INKTAG_DA_TEST_CACHE_LINES_EXPECTED
#define INKTAG_DA_TEST_CACHE_LINES_EXPECTED 12u
#endif

/* Include the production backend, not a copied cache algorithm. The real
 * AESL header supplies its cache budget and split Flash boundaries. */
#include "../compat/dialog/src/inktag_da_external_assets.c"

static uint8_t s_image[INKTAG_BOARD_EXTERNAL_FLASH_ASSET_IMAGE_SIZE];
static unsigned s_reads, s_wakes, s_parks, s_services;
static bool s_powered, s_short_read, s_wake_fail, s_reenter, s_close_on_service;

static const uint8_t *asset_address(uint32_t offset) {
    return (const uint8_t *)(uintptr_t)(INKTAG_GUI_FONT_ASSET_VIRTUAL_BASE +
        INKTAG_GUI_FONT_ASSET_HEADER_SIZE + offset);
}

static uint8_t expected(uint32_t offset) {
    return (uint8_t)(offset * 37u + (offset / CACHE_LINE_SIZE) * 13u);
}

static void write_u32(uint32_t offset, uint32_t value) {
    unsigned i;
    for (i = 0u; i < 4u; ++i) s_image[offset + i] = (uint8_t)(value >> (8u * i));
}

static void boot_fixture(void) {
    uint32_t i, crc;
    memset(s_image, 0, sizeof(s_image));
    for (i = 0u; i < INKTAG_GUI_FONT_ASSET_PAYLOAD_SIZE; ++i)
        s_image[INKTAG_GUI_FONT_ASSET_HEADER_SIZE + i] = expected(i);
    write_u32(0u, INKTAG_GUI_FONT_ASSET_MAGIC);
    write_u32(4u, INKTAG_GUI_FONT_ASSET_SCHEMA);
    write_u32(8u, INKTAG_GUI_FONT_ASSET_PAYLOAD_SIZE);
    crc = inktag_crc32_update(0xFFFFFFFFu,
        &s_image[INKTAG_GUI_FONT_ASSET_HEADER_SIZE],
        INKTAG_GUI_FONT_ASSET_PAYLOAD_SIZE) ^ 0xFFFFFFFFu;
    write_u32(12u, crc);
    memset(s_cache, 0, sizeof(s_cache));
    memset(s_cache_line, 0, sizeof(s_cache_line));
    s_cache_next = s_read_service_count = 0u;
    s_validated = -1;
    s_session_open = false;
    s_session_failed = false;
    s_reads = s_wakes = s_parks = s_services = 0u;
    s_powered = s_short_read = s_wake_fail = s_reenter = s_close_on_service = false;
}

int spi_flash_release_from_power_down(void) {
    ++s_wakes;
    s_powered = !s_wake_fail;
    return s_wake_fail ? SPI_FLASH_ERR_FAILED : SPI_FLASH_ERR_OK;
}

int spi_flash_power_down(void) {
    ++s_parks;
    s_powered = false;
    return SPI_FLASH_ERR_OK;
}

int spi_flash_read_data(uint8_t *data, uint32_t address, uint32_t length,
                        uint32_t *actual) {
    uint32_t offset;
    assert(s_powered);
    if (address >= INKTAG_BOARD_EXTERNAL_FLASH_ASSET_A_START &&
        address < INKTAG_BOARD_EXTERNAL_FLASH_ASSET_A_START +
                      INKTAG_BOARD_EXTERNAL_FLASH_ASSET_A_SIZE) {
        offset = address - INKTAG_BOARD_EXTERNAL_FLASH_ASSET_A_START;
        assert(length <= INKTAG_BOARD_EXTERNAL_FLASH_ASSET_A_SIZE - offset);
    } else {
        assert(address >= INKTAG_BOARD_EXTERNAL_FLASH_ASSET_B_START);
        offset = address - INKTAG_BOARD_EXTERNAL_FLASH_ASSET_B_START;
        assert(offset < INKTAG_BOARD_EXTERNAL_FLASH_ASSET_B_SIZE);
        assert(length <= INKTAG_BOARD_EXTERNAL_FLASH_ASSET_B_SIZE - offset);
        offset += INKTAG_BOARD_EXTERNAL_FLASH_ASSET_A_SIZE;
    }
    ++s_reads;
    *actual = s_short_read ? length / 2u : length;
    memcpy(data, &s_image[offset], *actual);
    return SPI_FLASH_ERR_OK;
}

void app_service_ble(void) {
    ++s_services;
    if (s_reenter) {
        s_reenter = false;
        assert(inktag_gui_asset_read_byte(asset_address(3u * 256u + 17u)) ==
               expected(3u * 256u + 17u));
    }
    if (s_close_on_service) inktag_gui_assets_end();
}

static void test_bounds_hits_and_full_payload(void) {
    uint32_t i;
    unsigned reads;
    uint8_t ordinary = 0x71u, scratch[4];
    boot_fixture();
    assert(INKTAG_BOARD_GUI_ASSET_CACHE_LINES == INKTAG_DA_TEST_CACHE_LINES_EXPECTED);
    assert(inktag_gui_asset_read_byte(asset_address(0u)) == 0xFFu);
    assert(inktag_gui_asset_read_byte(&ordinary) == ordinary);
    assert(inktag_gui_assets_begin());
    assert(inktag_gui_assets_begin());
    assert(inktag_gui_assets_healthy());
    assert(s_wakes == 1u);
    reads = s_reads;
    assert(inktag_gui_asset_read_byte(asset_address(0u)) == expected(0u));
    assert(s_reads == reads + 1u); /* validation scratch is not a cache hit */
    assert(inktag_gui_asset_read_byte(asset_address(255u)) == expected(255u));
    assert(s_reads == reads + 1u);
    reads = s_reads;
    assert(inktag_gui_asset_read_byte(asset_address(4079u)) == expected(4079u));
    assert(s_reads == reads + 2u); /* a line straddles split A/B */
    assert(inktag_gui_asset_read_byte(asset_address(4080u)) == expected(4080u));
    assert(s_reads == reads + 2u);
    assert(inktag_gui_asset_read_byte(asset_address(10499u)) == expected(10499u));
    reads = s_reads;
    assert(flash_read(INKTAG_BOARD_EXTERNAL_FLASH_ASSET_IMAGE_SIZE, scratch, 0u));
    assert(!flash_read(INKTAG_BOARD_EXTERNAL_FLASH_ASSET_IMAGE_SIZE, scratch, 1u));
    assert(!flash_read(INKTAG_BOARD_EXTERNAL_FLASH_ASSET_IMAGE_SIZE + 1u, scratch, 0u));
    assert(!flash_read(UINT32_MAX, scratch, 4u));
    assert(!flash_read(INKTAG_BOARD_EXTERNAL_FLASH_ASSET_IMAGE_SIZE - 1u, scratch, 2u));
    assert(s_reads == reads); /* reject the entire invalid span before I/O */
    assert(flash_read(INKTAG_BOARD_EXTERNAL_FLASH_ASSET_A_SIZE - 1u, scratch, 4u));
    for (i = 0u; i < 4u; ++i)
        assert(scratch[i] == expected(INKTAG_BOARD_EXTERNAL_FLASH_ASSET_A_SIZE -
                                    INKTAG_GUI_FONT_ASSET_HEADER_SIZE - 1u + i));
    for (i = 0u; i < INKTAG_GUI_FONT_ASSET_PAYLOAD_SIZE; ++i)
        assert(inktag_gui_asset_read_byte(asset_address(i)) == expected(i));
    assert(s_services != 0u); /* hits still service radio/watchdog */
    inktag_gui_assets_end();
    inktag_gui_assets_end();
    assert(s_parks == 1u);
    assert(inktag_gui_asset_read_byte(asset_address(0u)) == 0xFFu);
}

static void test_eviction_short_fill_cannot_poison_old_line(void) {
    uint32_t line;
    unsigned reads;
    boot_fixture();
    assert(inktag_gui_assets_begin());
    for (line = 0u; line < INKTAG_BOARD_GUI_ASSET_CACHE_LINES; ++line)
        assert(inktag_gui_asset_read_byte(asset_address(line * 256u)) ==
               expected(line * 256u));
    s_short_read = true;
    assert(inktag_gui_asset_read_byte(asset_address(
               INKTAG_BOARD_GUI_ASSET_CACHE_LINES * 256u)) == 0xFFu);
    assert(s_cache_line[0] == 0u);
    assert(!inktag_gui_assets_healthy());
    s_short_read = false;
    reads = s_reads;
    assert(inktag_gui_asset_read_byte(asset_address(0u)) == 0xFFu);
    assert(s_reads == reads); /* Failure is sticky for this render session. */
    assert(!inktag_gui_assets_begin());
    inktag_gui_assets_end();
    assert(inktag_gui_assets_begin() && inktag_gui_assets_healthy());
    assert(inktag_gui_asset_read_byte(asset_address(0u)) == expected(0u));
    assert(s_reads == reads + 1u); /* old tag may not return overwritten bytes */
    assert(inktag_gui_asset_read_byte(asset_address(
               INKTAG_BOARD_GUI_ASSET_CACHE_LINES * 256u)) ==
           expected(INKTAG_BOARD_GUI_ASSET_CACHE_LINES * 256u));
    inktag_gui_assets_end();
    reads = s_reads;
    assert(inktag_gui_assets_begin());
    assert(inktag_gui_asset_read_byte(asset_address(
               INKTAG_BOARD_GUI_ASSET_CACHE_LINES * 256u)) ==
           expected(INKTAG_BOARD_GUI_ASSET_CACHE_LINES * 256u));
    assert(s_reads == reads && s_wakes == 3u); /* immutable hits survive parking */
    inktag_gui_assets_end();
}

static void test_yield_reentry_and_session_close(void) {
    unsigned reads;
    boot_fixture();
    assert(inktag_gui_assets_begin());
    s_read_service_count = 255u;
    s_reenter = true;
    assert(inktag_gui_asset_read_byte(asset_address(19u * 256u + 3u)) ==
           expected(19u * 256u + 3u));
    assert(s_services == 1u);
    s_read_service_count = 255u;
    s_close_on_service = true;
    reads = s_reads;
    assert(inktag_gui_asset_read_byte(asset_address(20u * 256u)) == 0xFFu);
    assert(s_reads == reads && !s_powered && !s_session_open);
}

static void test_invalid_manifest_crc_and_wake_fail_closed(void) {
    unsigned reads;
    boot_fixture();
    s_wake_fail = true;
    assert(!inktag_gui_assets_begin() && s_reads == 0u);
    s_wake_fail = false;
    s_image[0] ^= 1u;
    assert(!inktag_gui_assets_begin() && !s_powered);
    reads = s_reads;
    assert(!inktag_gui_assets_begin() && s_reads == reads);
    boot_fixture();
    s_image[INKTAG_GUI_FONT_ASSET_HEADER_SIZE + 600u] ^= 1u;
    assert(!inktag_gui_assets_begin() && !s_powered);
    assert(inktag_gui_asset_read_byte(asset_address(0u)) == 0xFFu);
}

int main(void) {
    test_bounds_hits_and_full_payload();
    test_eviction_short_fill_cannot_poison_old_line();
    test_yield_reentry_and_session_close();
    test_invalid_manifest_crc_and_wake_fail_closed();
    puts("DA shipping GUI cache boundary/lifecycle tests passed");
    return 0;
}
