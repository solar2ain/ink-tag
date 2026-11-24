#include "inktag_panel_gpio_snapshot.h"
#if INKTAG_PANEL_GPIO_SNAPSHOT_ENABLED
#include <stdbool.h>
#include <string.h>

static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4u; ++i) p[i] = (uint8_t)(v >> (i * 8u));
}
static bool field_ok(uint8_t known, unsigned mask, uint8_t value, uint8_t max) {
    return (known & mask) ? value <= max : value == INKTAG_PANEL_GPIO_UNKNOWN;
}
static bool valid(const inktag_panel_gpio_snapshot_t *s) {
    if ((uint32_t)(s->end_ms - s->begin_ms) > INKTAG_PANEL_GPIO_SNAPSHOT_MAX_MS ||
        (s->state_known & ~0x3fu) || (s->state_value & ~s->state_known) ||
        !(s->state_known & s->state_value & INKTAG_PANEL_STATE_CONNECT_PROBE_DISABLED) ||
        ((s->state_value & INKTAG_PANEL_STATE_CACHE_VALID) && s->cache_age_ms == UINT32_MAX))
        return false;
    for (unsigned i = 0; i < INKTAG_PANEL_GPIO_SNAPSHOT_ROLES; ++i) {
        const inktag_panel_gpio_role_t *r = &s->roles[i];
        if ((r->known & ~0x3fu) || ((r->bank == 255u) != (r->pin == 255u)) ||
            (r->bank == 255u && r->known) ||
            ((r->known & INKTAG_PANEL_GPIO_INPUT) &&
             (!(r->known & INKTAG_PANEL_GPIO_IE) || r->ie != 1u)) ||
            !field_ok(r->known, INKTAG_PANEL_GPIO_MUX, r->mux, 3) ||
            !field_ok(r->known, INKTAG_PANEL_GPIO_IE, r->ie, 1) ||
            !field_ok(r->known, INKTAG_PANEL_GPIO_OE, r->oe, 1) ||
            !field_ok(r->known, INKTAG_PANEL_GPIO_LATCH, r->latch, 1) ||
            !field_ok(r->known, INKTAG_PANEL_GPIO_INPUT, r->input, 1) ||
            !field_ok(r->known, INKTAG_PANEL_GPIO_PULL, r->pull, 3) ||
            (!(r->known & INKTAG_PANEL_GPIO_PULL) && r->pull_raw != 255u))
            return false;
    }
    return true;
}

inktag_status_v2_t inktag_panel_gpio_snapshot_dispatch(
    const inktag_panel_gpio_snapshot_backend_t *b, uint8_t opcode, uint8_t flags,
    const uint8_t *request, uint16_t length, uint8_t *out,
    uint16_t capacity, uint16_t *written) {
    inktag_panel_gpio_snapshot_t snapshot;
    inktag_status_v2_t status;
    uint32_t begin_ms;
    if (written) *written = 0;
    if (!written || !request || length != 6u || flags ||
        opcode != INKTAG_CMD_DEBUG_READ || request[0] != INKTAG_PANEL_GPIO_SNAPSHOT_OPERATION ||
        request[1] != 1u || !(request[2] | request[3] | request[4] | request[5]))
        return INKTAG_STATUS_INVALID_REQUEST;
    if (!out || capacity < INKTAG_PANEL_GPIO_SNAPSHOT_WIRE_SIZE)
        return INKTAG_STATUS_INVALID_REQUEST;
    if (!b || !b->guard || !b->capture || !b->now_ms) return INKTAG_STATUS_UNSUPPORTED;
    status = b->guard(b->context);
    if (status != INKTAG_STATUS_OK) return status;
    memset(&snapshot, 255, sizeof(snapshot));
    snapshot.state_known = snapshot.state_value = 0u;
    begin_ms = b->now_ms(b->context);
    status = b->capture(b->context, &snapshot);
    if (status != INKTAG_STATUS_OK) return status;
    snapshot.begin_ms = begin_ms;
    snapshot.end_ms = b->now_ms(b->context);
    if (!valid(&snapshot)) return INKTAG_STATUS_INTERNAL_ERROR;
    memcpy(out, request, 6u);
    put32(out + 6, snapshot.begin_ms);
    put32(out + 10, snapshot.end_ms);
    out[14] = (uint8_t)snapshot.state_known;
    out[15] = (uint8_t)(snapshot.state_known >> 8);
    out[16] = (uint8_t)snapshot.state_value;
    out[17] = (uint8_t)(snapshot.state_value >> 8);
    put32(out + 18, snapshot.cache_age_ms);
    out[22] = INKTAG_PANEL_GPIO_SNAPSHOT_ROLES;
    out[23] = 11u;
    for (unsigned i = 0; i < INKTAG_PANEL_GPIO_SNAPSHOT_ROLES; ++i) {
        const inktag_panel_gpio_role_t *r = &snapshot.roles[i];
        uint8_t *p = out + 24u + i * 11u;
        p[0] = (uint8_t)i; p[1] = r->bank; p[2] = r->pin; p[3] = r->known;
        p[4] = r->mux; p[5] = r->ie; p[6] = r->oe; p[7] = r->latch;
        p[8] = r->input; p[9] = r->pull; p[10] = r->pull_raw;
    }
    *written = INKTAG_PANEL_GPIO_SNAPSHOT_WIRE_SIZE;
    return INKTAG_STATUS_OK;
}
#endif
