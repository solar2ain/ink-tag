#ifndef INKTAG_BUILD_PRO
#define INKTAG_BUILD_PRO 1
#endif
#line 1 "../Src/user/inktag_gr_pin_routes.c"
#include "inktag_gr_pin_routes.h"
#include "inktag_gr_board_identity.h"
#include "inktag_gr_factory_seed.h"
#include "goodix_pin_backend_facts.h"
#include "inktag_screen_busy.h"
#include "inktag_protocol_v2.h"

#include <string.h>

static inktag_gr_pin_storage_t s_store;
static inktag_pin_config_t s_effective;
enum {ROUTE_REQUIRED=1u, ROUTE_VALID=2u, ROUTE_APPLIED=4u, ROUTE_INSTALLING=8u,
      ROUTE_BUSY_KNOWN=16u, ROUTE_BUSY_HIGH=32u};
static uint8_t s_flags;

static bool screen_allowed(void *context, const uint8_t wire[17]) {
    (void)context;
    epd_screen_config_t decoded;
    return epd_screen_config_decode_exact(wire, 17u, &decoded) == EPD_SCREEN_STATUS_OK;
}

static bool refresh(void) {
    inktag_gr_pin_factory_t factory;
    inktag_gr_pin_user_t user;
    s_flags &= (uint8_t)~ROUTE_VALID;
    if (!inktag_gr_pin_storage_read(&s_store, &factory, &user)) return false;
    s_effective = user.pins;
    s_flags |= ROUTE_VALID;
    return true;
}

void inktag_gr_pin_routes_init(uint8_t family, uint8_t mcu, uint8_t allowed_boards) {
    static const char contract[] = "INKTAG-GR-RELEASE1";
    /* Full v1 admission checks these bytes inside the authenticated App,
     * not an unsigned package label. Keep it tied to this actual consumer. */
    __asm__ volatile("" : : "r"(contract) : "memory");
    uint8_t raw[76], erased = 255u, profile_erased = 255u;
    s_flags = 0u;
    memset(&s_store, 0, sizeof(s_store));
    inktag_gr_board_identity_invalidate();
    inktag_gr_factory_config_copy(raw);
    for (unsigned i = 0u; i < 12u; ++i) erased &= raw[i];
    for (unsigned i = 12u; i < sizeof(raw); ++i) profile_erased &= raw[i];
    if (erased == 255u && profile_erased == 255u) return;
    s_flags |= ROUTE_REQUIRED;
    inktag_gr_board_identity_invalidate();
    const inktag_gr_pin_facts_t *facts = inktag_gr_pin_backend(family, raw[6], mcu);
    if (!facts || !(allowed_boards & (1u << facts->board)) ||
        !inktag_gr_pin_storage_init(&s_store, facts, raw, sizeof(raw),
                                     screen_allowed, NULL) ||
        inktag_gr_board_identity_configured(&s_store, family, allowed_boards) == 0u ||
        !refresh()) inktag_gr_board_identity_invalidate();
}

bool inktag_gr_pin_routes_required(void) { return (s_flags & ROUTE_REQUIRED) != 0u; }
bool inktag_gr_pin_routes_available(void) {
    return (s_flags & (ROUTE_REQUIRED|ROUTE_VALID)) == (ROUTE_REQUIRED|ROUTE_VALID) && s_store.ready;
}
bool inktag_gr_pin_routes_drive_ready(void) {
    return inktag_gr_pin_routes_available() &&
        (s_flags & (ROUTE_APPLIED|ROUTE_INSTALLING)) == ROUTE_APPLIED;
}
const inktag_pin_config_t *inktag_gr_pin_routes_peek(void) {
    return inktag_gr_pin_routes_available() ? &s_effective : NULL;
}
const inktag_pin_config_t *inktag_gr_pin_routes_applied(void) {
    return (s_flags & ROUTE_APPLIED) ? &s_effective : NULL;
}
void inktag_gr_pin_routes_mark_applied(bool applied) {
    if (applied) { s_flags |= ROUTE_APPLIED; s_flags &= (uint8_t)~ROUTE_INSTALLING; }
    else s_flags &= (uint8_t)~ROUTE_APPLIED;
}
void inktag_gr_pin_routes_invalidate(void) {
    s_flags &= (uint8_t)~ROUTE_VALID;
    s_store.ready = false;
}

bool inktag_gr_pin_routes_screen(epd_screen_config_t *out) {
    if (!out || !inktag_gr_pin_routes_available()) return false;
    return epd_screen_config_decode_exact(s_store.factory + 36u, 17u, out) == EPD_SCREEN_STATUS_OK;
}
bool inktag_gr_pin_routes_prepare(const epd_screen_config_t *screen,
                                  inktag_gr_pin_candidate_t *candidate) {
    inktag_gr_pin_factory_t factory;
    inktag_gr_pin_user_t user;
    inktag_gr_pin_candidate_t next;
    bool high = false;
    if (!candidate || !inktag_gr_pin_routes_available() ||
        !inktag_gr_pin_storage_snapshot(&s_store, &factory, &user)) return false;
    next.pins = user.pins;
    next.busy_known = epd_screen_busy_active_high(screen, &high);
    /* Registered GR backends' existing qualified defaults have no BUSY
     * inverter. SSD1680 BW normalization remains solely in the bus adapter. */
    next.busy_high = high;
    *candidate = next;
    return true;
}
void inktag_gr_pin_routes_publish(const inktag_gr_pin_candidate_t *candidate) {
    s_effective = candidate->pins;
    s_flags = (s_flags & (uint8_t)~(ROUTE_BUSY_KNOWN|ROUTE_BUSY_HIGH)) |
        (candidate->busy_known ? ROUTE_BUSY_KNOWN : 0u) |
        (candidate->busy_high ? ROUTE_BUSY_HIGH : 0u);
}
bool inktag_gr_pin_routes_busy_high(bool *out) {
    if (!out || !inktag_gr_pin_routes_available() || !(s_flags & ROUTE_BUSY_KNOWN)) return false;
    *out = (s_flags & ROUTE_BUSY_HIGH) != 0u;
    return true;
}
bool inktag_gr_pin_routes_config(epd_config_t *inout) {
    const inktag_pin_config_t *pins = inktag_gr_pin_routes_peek();
    if (!pins || !inout) return false;
    inout->mosi_pin = pins->pins[INKTAG_PIN_MOSI];
    inout->sclk_pin = pins->pins[INKTAG_PIN_CLOCK];
    inout->cs_pin = pins->pins[INKTAG_PIN_CS];
    inout->dc_pin = pins->pins[INKTAG_PIN_DC];
    inout->rst_pin = pins->pins[INKTAG_PIN_RESET];
    inout->busy_pin = pins->pins[INKTAG_PIN_BUSY];
    inout->en_pin = pins->pins[INKTAG_PIN_ENABLE];
    inout->bs_pin = inout->wakeup_pin = inout->led_pin = 255u;
    return true;
}
bool inktag_gr_pin_routes_status(uint8_t out[36]) {
    if (!inktag_gr_pin_routes_available() || !inktag_gr_pin_storage_report(&s_store, out)) {
        s_flags &= (uint8_t)~ROUTE_VALID;
        return false;
    }
    /* A fresh valid saved tuple is not proof it was applied to the bus/LEDs.
     * Never report changed storage as the already-applied runtime snapshot. */
    if ((s_flags & ROUTE_APPLIED) &&
        ((uint16_t)(out[22] | ((uint16_t)out[23] << 8u)) != s_effective.active_low ||
         memcmp(out + 24u, s_effective.pins, sizeof(s_effective.pins)))) {
        s_flags &= (uint8_t)~ROUTE_VALID;
        return false;
    }
    out[1] |= INKTAG_PIN_REPORT_RECIPE_BUSY_DERIVED;
    return true;
}
bool inktag_gr_pin_routes_aux(uint8_t out[INKTAG_GR_PIN_AUX_BYTES]) {
    inktag_gr_pin_factory_t factory;
    inktag_gr_pin_user_t user;
    if (!out || !inktag_gr_pin_routes_available() ||
        !inktag_gr_pin_storage_read(&s_store, &factory, &user)) {
        s_flags &= (uint8_t)~ROUTE_VALID;
        return false;
    }
    memcpy(out, user.aux, INKTAG_GR_PIN_AUX_BYTES);
    return true;
}
inktag_gr_pin_storage_result_t inktag_gr_pin_routes_set(
    const uint8_t *wire, size_t length, bool busy) {
    if (!inktag_gr_pin_routes_available()) return INKTAG_GR_PIN_STORAGE_ERROR;
    s_flags |= ROUTE_INSTALLING;
    inktag_gr_pin_storage_result_t result =
        inktag_gr_pin_storage_set_override(&s_store, wire, length, busy);
    if (result == INKTAG_GR_PIN_STORAGE_OK && !refresh()) return INKTAG_GR_PIN_STORAGE_ERROR;
    if (result != INKTAG_GR_PIN_STORAGE_OK && s_store.ready) s_flags &= (uint8_t)~ROUTE_INSTALLING;
    return result;
}
#if INKTAG_BUILD_PRO
#include "inktag_gr_custom_aux_routes_pro.inc"
#else
inktag_gr_pin_storage_result_t inktag_gr_pin_routes_set_aux(const uint8_t a[INKTAG_GR_PIN_AUX_BYTES],bool busy){(void)a;(void)busy;return INKTAG_GR_PIN_STORAGE_INVALID;}
#endif
#line 168 "../Src/user/inktag_gr_pin_routes.c"
