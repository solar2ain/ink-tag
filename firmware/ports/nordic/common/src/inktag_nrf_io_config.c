#include "inktag_nrf_build_profile.h"
#include "inktag_nrf_io_config.h"
#include "inktag_board.h"
#include "inktag_screen_busy.h"
#include <string.h>

_Static_assert(sizeof(inktag_nrf_io_config_t) == 28u, "Nordic IO uses existing codecs");
static inktag_nrf_io_config_t current;
static bool available, busy_known, legacy_busy_low;

void inktag_nrf_io_defaults(inktag_nrf_io_config_t *out) {
    const inktag_nrf_io_config_t defaults = {
        INKTAG_BOARD_DEFAULT_PIN_WIRE, INKTAG_BOARD_DEFAULT_AUX_WIRE};
    *out = defaults;
}

static bool pin_allowed(void *context, inktag_pin_role_t role, uint8_t pin) {
    (void)context;
    return role < INKTAG_PIN_ENABLE && pin < 32u &&
        (INKTAG_BOARD_DISPLAY_CONFIG_MASK & (UINT32_C(1) << pin)) != 0u;
}

bool inktag_nrf_io_valid(const inktag_nrf_io_config_t *config) {
    inktag_pin_config_t pins;
    if (!config ||
        !inktag_pin_config_decode(config->pins, sizeof(config->pins), &pins) ||
        !inktag_pin_config_validate(&pins, pin_allowed, NULL)) return false;
#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 11
#include "inktag_nrf_input_custom_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
    /* CS/RESET and data/clock/DC meanings are not user settings. No connected
     * EN/LED/UART or PWM is qualified on this exact board. */
    if ((pins.active_low & ~INKTAG_RELEASE_EDITABLE_ACTIVE_LOW_MASK) != 0u)
        return false;
    return true;
}

bool inktag_nrf_io_derive(const epd_screen_config_t *screen,
                         inktag_nrf_io_config_t *config) {
    bool high;
    return config && epd_screen_busy_active_high(screen, &high);
}

void inktag_nrf_io_publish(const inktag_nrf_io_config_t *config,
                          const epd_screen_config_t *screen) {
    available = false;
    current = *config;
#if !INKTAG_BUILD_PRO
    /* Retained Pro fields remain opaque in storage, never effective GPIO. */
    static const uint8_t stock_aux[] = INKTAG_BOARD_DEFAULT_AUX_WIRE;
    memcpy(current.aux, stock_aux, sizeof(current.aux));
#endif
    busy_known = inktag_nrf_io_derive(screen, &current);
    /* Preserve the legacy wait(false) convention, not a physical inversion.
     * No RESET/SPI/waveform sequence changes. Other qualified paths stay raw. */
    legacy_busy_low = busy_known && screen->driver_id == EPD_SCREEN_DRIVER_SSD1680 &&
        screen->color_id == EPD_SCREEN_COLOR_BW;
    available = true;
}

void inktag_nrf_io_invalidate(void) { available = busy_known = legacy_busy_low = false; }
const inktag_nrf_io_config_t *inktag_nrf_io_current(void) {
    return available ? &current : NULL;
}
bool inktag_nrf_io_busy_known(void) { return available && busy_known; }
bool inktag_nrf_io_read_busy(bool raw) {
    return raw ^ (INKTAG_BOARD_BUSY_INVERTED != 0) ^ legacy_busy_low;
}
bool inktag_nrf_io_enable_low(void) {
    return available && (current.pins[2] & (1u << INKTAG_PIN_ENABLE)) != 0u;
}
