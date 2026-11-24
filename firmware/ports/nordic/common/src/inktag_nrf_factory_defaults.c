#include "inktag_nrf_factory_defaults.h"
#include "inktag_nrf_config_store.h"
#include "inktag_board.h"
#include <string.h>

_Static_assert(sizeof(epd_config_t) == 13u, "Legacy Nordic wiring ABI");
_Static_assert(EPD_SCREEN_WIRE_SIZE == 17u, "Shared native screen ABI");

void inktag_nrf_io_project(const inktag_nrf_io_config_t *io, epd_config_t *pins) {
    /* Generated RAM projection only, never another persisted copy. */
    pins->mosi_pin = io->pins[4u + INKTAG_PIN_MOSI];
    pins->sclk_pin = io->pins[4u + INKTAG_PIN_CLOCK];
    pins->cs_pin = io->pins[4u + INKTAG_PIN_CS];
    pins->dc_pin = io->pins[4u + INKTAG_PIN_DC];
    pins->rst_pin = io->pins[4u + INKTAG_PIN_RESET];
    pins->busy_pin = io->pins[4u + INKTAG_PIN_BUSY];
    pins->en_pin = io->pins[4u + INKTAG_PIN_ENABLE];
    pins->led_pin = io->pins[4u + INKTAG_PIN_LED_BLUE];
    pins->bs_pin = pins->wakeup_pin = pins->reserved_screen_legacy = 255u;
}

/* Integrity and routing are independent of screen validity. A bad screen
 * cannot revoke name maintenance. Geometry is exact-decoded by its consumer. */
static inktag_settings_journal_read_result_t factory_read(
    inktag_nrf_io_config_t *io, uint8_t wire[17], bool *has_screen) {
    uint8_t record[INKTAG_NRF_FACTORY_PROFILE_SIZE];
    uint16_t length = 0u;
    inktag_settings_journal_read_result_t result = inktag_nrf_config_store_read_result(
        INKTAG_NRF_CONFIG_BOARD, record, sizeof(record), &length);
    if (result == INKTAG_SETTINGS_JOURNAL_READ_ERROR) return result;
    inktag_nrf_io_defaults(io);
    *has_screen = false;
    if (result == INKTAG_SETTINGS_JOURNAL_READ_MISSING) return result;
    if (length == INKTAG_NRF_FACTORY_PROFILE_SIZE && memcmp(record, "NFP1", 4u) == 0) {
        uint32_t expected;
        memcpy(&expected, record + 49u, sizeof(expected));
        if (epd_record_checksum(record, 49u) != expected) goto invalid;
        memcpy(io->pins, record + INKTAG_RELEASE_NRF_FACTORY_PINS_OFFSET, 16u);
        memcpy(io->aux, record + INKTAG_RELEASE_NRF_FACTORY_AUX_OFFSET, 12u);
        const uint8_t empty[17] = {0};
        *has_screen = memcmp(record + INKTAG_RELEASE_NRF_FACTORY_SCREEN_OFFSET, empty, 17u) != 0;
        if (wire) memcpy(wire, record + INKTAG_RELEASE_NRF_FACTORY_SCREEN_OFFSET, 17u);
    } else goto invalid;
    if (!inktag_nrf_io_valid(io)) goto invalid;
    return result;
invalid:
    return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
}

bool inktag_nrf_factory_io(inktag_nrf_io_config_t *io) {
    bool present;
    return io && factory_read(io, NULL, &present) != INKTAG_SETTINGS_JOURNAL_READ_ERROR;
}

inktag_settings_journal_read_result_t inktag_nrf_factory_screen_read(epd_screen_config_t *screen) {
    inktag_nrf_io_config_t io;
    uint8_t wire[17]; bool present;
    if (!screen || factory_read(&io, wire, &present) == INKTAG_SETTINGS_JOURNAL_READ_ERROR)
        return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    if (present) return wire[0] == INKTAG_RELEASE_SCREEN_VERSION &&
        epd_screen_config_decode_exact(wire, sizeof(wire), screen) == EPD_SCREEN_STATUS_OK ?
        INKTAG_SETTINGS_JOURNAL_READ_FOUND : INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    return INKTAG_SETTINGS_JOURNAL_READ_MISSING; /* Safe unconfigured base: RAM only. */
}

inktag_settings_journal_read_result_t inktag_nrf_screen_read(epd_screen_config_t *screen) {
    epd_screen_record_t record;
    uint16_t length = 0u;
    if (!screen) return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    inktag_settings_journal_read_result_t result = inktag_nrf_config_store_read_result(
        INKTAG_NRF_CONFIG_SCREEN, &record, sizeof(record), &length);
    if (result == INKTAG_SETTINGS_JOURNAL_READ_FOUND &&
        (length != sizeof(record) || epd_screen_record_decode_exact(&record, screen) != EPD_SCREEN_STATUS_OK))
        return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    return result;
}

bool inktag_nrf_boot_screen(epd_screen_config_t *screen) {
    inktag_settings_journal_read_result_t result = inktag_nrf_screen_read(screen);
    return result == INKTAG_SETTINGS_JOURNAL_READ_FOUND ||
        (result == INKTAG_SETTINGS_JOURNAL_READ_MISSING &&
         inktag_nrf_factory_screen_read(screen) == INKTAG_SETTINGS_JOURNAL_READ_FOUND);
}

bool inktag_nrf_factory_screen(epd_screen_config_t *screen) {
    return inktag_nrf_factory_screen_read(screen) == INKTAG_SETTINGS_JOURNAL_READ_FOUND;
}

bool inktag_nrf_board_profile_read(epd_config_t *config) {
    inktag_nrf_io_config_t io;
    if (!config || !inktag_nrf_factory_io(&io)) return false;
    memset(config, 0, sizeof(*config));
    inktag_nrf_io_project(&io, config);
    return true;
}

bool inktag_nrf_board_profile_write(const epd_config_t *config) {
    (void)config;
    return false; /* Factory ID1 is never a runtime user override. */
}
