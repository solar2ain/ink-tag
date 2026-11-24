#ifndef INKTAG_PANEL_GPIO_SNAPSHOT_H
#define INKTAG_PANEL_GPIO_SNAPSHOT_H
#include <stdint.h>
#include "inktag_protocol_v2.h"

#ifndef INKTAG_PANEL_GPIO_SNAPSHOT_ENABLED
#define INKTAG_PANEL_GPIO_SNAPSHOT_ENABLED 0
#endif
#if INKTAG_PANEL_GPIO_SNAPSHOT_ENABLED != 0 && INKTAG_PANEL_GPIO_SNAPSHOT_ENABLED != 1
#error INKTAG_PANEL_GPIO_SNAPSHOT_ENABLED must be 0 or 1
#endif

#if INKTAG_PANEL_GPIO_SNAPSHOT_ENABLED
#define INKTAG_PANEL_GPIO_SNAPSHOT_OPERATION 0x31u
#define INKTAG_PANEL_GPIO_SNAPSHOT_ROLES 7u
#define INKTAG_PANEL_GPIO_SNAPSHOT_WIRE_SIZE 101u
#define INKTAG_PANEL_GPIO_SNAPSHOT_MAX_MS 5u
#define INKTAG_PANEL_GPIO_UNKNOWN 255u
/* Roles0..6 match inktag_pin_role_t: MOSI,CLOCK,CS,DC,RESET,BUSY,ENABLE.
 * Bank/pin are physical port ordinal and bit, NOT an SDK bitmask/GPIO token. */
enum {
    INKTAG_PANEL_GPIO_MUX = 1, INKTAG_PANEL_GPIO_IE = 2,
    INKTAG_PANEL_GPIO_OE = 4, INKTAG_PANEL_GPIO_LATCH = 8,
    INKTAG_PANEL_GPIO_INPUT = 16, INKTAG_PANEL_GPIO_PULL = 32
};
/* mux:0=GPIO,1=alternate,2=analog,3=other; pull:0=float,1=up,2=down,3=other.
 * Unavailable fields MUST be255, not fabricated zeroes. Pull includes raw code.
 * V1 INPUT requires IE known and enabled; otherwise explicitly unavailable.
 * Latch is not pad voltage and software powered=false is not a rail measurement. */
typedef struct {
    uint8_t bank, pin, known;
    uint8_t mux, ie, oe, latch, input, pull, pull_raw;
} inktag_panel_gpio_role_t;
enum {
    INKTAG_PANEL_STATE_INITIALIZED = 1, INKTAG_PANEL_STATE_POWERED = 2,
    INKTAG_PANEL_STATE_DISPLAY_ACTIVE = 4,
    INKTAG_PANEL_STATE_CONNECT_PROBE_DISABLED = 8,
    INKTAG_PANEL_STATE_CACHE_VALID = 16, INKTAG_PANEL_STATE_EN_ACTIVE_LOW = 32
};
typedef struct {
    uint32_t begin_ms, end_ms, cache_age_ms; /* age UINT32_MAX means unavailable */
    uint16_t state_known, state_value;
    inktag_panel_gpio_role_t roles[INKTAG_PANEL_GPIO_SNAPSHOT_ROLES];
} inktag_panel_gpio_snapshot_t;
typedef struct {
    void *context;
    /* Existing AUTH, qualified fixed mapping and observation-only build.
     * No voltage sampling, panel mutex acquisition or hardware action. */
    inktag_status_v2_t (*guard)(void *);
    /* One bounded register/cache capture. No GPIO/analog-bus writes, polling,
     * clear-on-read registers, yielding, init, cleanup, reset or cached fallback.
     * Unsafe fields (e.g. unbounded analog_read) stay explicitly unavailable. */
    inktag_status_v2_t (*capture)(void *, inktag_panel_gpio_snapshot_t *);
    /* Read-only wrap-safe application milliseconds, advancing DURING capture;
     * not a frozen main-loop timestamp. No SET_TIME, ADC or timer reconfigure. */
    uint32_t (*now_ms)(void *);
} inktag_panel_gpio_snapshot_backend_t;
/* DEBUG_READ only, flags0, request31 01 nonceLE32(nonzero). No OPEN/EXECUTE or
 * CLOSE/ABORT session and absolutely no cleanup callback on any outcome. */
inktag_status_v2_t inktag_panel_gpio_snapshot_dispatch(
    const inktag_panel_gpio_snapshot_backend_t *, uint8_t opcode, uint8_t flags,
    const uint8_t *request, uint16_t length, uint8_t *out,
    uint16_t capacity, uint16_t *written);
#endif
#endif
