#ifndef INKTAG_PANEL_DIAGNOSTIC_H
#define INKTAG_PANEL_DIAGNOSTIC_H
#include <stdbool.h>
#include <stdint.h>
#include "inktag_protocol_v2.h"

#ifndef INKTAG_PANEL_DIAGNOSTIC_ENABLED
#define INKTAG_PANEL_DIAGNOSTIC_ENABLED 0
#endif

/* Pure SSD1680 status/calibration checks also used by the normal clock path.
 * Keep them available when experimental panel diagnostics are disabled.
 * SSD1680 Rev0.14, R2F and R2C/R2D: call only after registered recipe/geometry
 * admission and reset BUSY release. Waveform versions/options do not identify
 * the silicon. Ignore R2F bits5/4 before explicit HV/VCI detection. These checks
 * do not establish LUT length or optical compatibility; retain panel evidence. */
static inline bool inktag_ssd1680_status_idle(uint8_t status) {
    return (status & 0xcfu) == 0x01u;
}

static inline bool inktag_ssd1680_clock_probe_valid(uint8_t status,
                                                 const uint8_t metadata[11]) {
    return metadata && inktag_ssd1680_status_idle(status) &&
        metadata[1] >= 0x08u && metadata[1] <= 0x78u &&
        !(metadata[1] & 0x03u);
}

#if INKTAG_PANEL_DIAGNOSTIC_ENABLED
/* A bounded experiment, not a raw SPI/GPIO interpreter or persistent setting. */
#define INKTAG_PANEL_DIAG_MAX_DATA 64u
#define INKTAG_PANEL_DIAG_OPERATION 0x30u
enum { INKTAG_PANEL_DIAG_REVISION = 1, INKTAG_PANEL_DIAG_STATUS = 2,
       INKTAG_PANEL_DIAG_OTP = 3, INKTAG_PANEL_DIAG_WAVEFORM = 4,
       INKTAG_PANEL_DIAG_DESCRIBE = 5, INKTAG_PANEL_DIAG_RESET = 6,
       INKTAG_PANEL_DIAG_TEMPERATURE = 7, INKTAG_PANEL_DIAG_POWER_CYCLE = 8,
       INKTAG_PANEL_DIAG_RAM_WRITE = 9 };
enum { INKTAG_PANEL_DIAG_IDLE = 0, INKTAG_PANEL_DIAG_RUNNING = 1,
       INKTAG_PANEL_DIAG_DONE = 2, INKTAG_PANEL_DIAG_FAILED = 3 };
/* Backend must observe these in order, with real edges and bounded phase waits. */
#define INKTAG_PANEL_DIAG_WAVE_GATES 0x1Fu /* reset, PON, RAM, DRF, POF */
typedef struct {
    uint8_t operation, length;
    uint16_t offset, fixture;
} inktag_panel_diag_request_t;
/* Public-manual read plans. Selection requires a qualified UC-family bus;
   these are candidate formats, never automatic unique-die identification. */
enum { INKTAG_PANEL_DIAG_UC8151C = 1, INKTAG_PANEL_DIAG_UC8176 = 2,
       INKTAG_PANEL_DIAG_UC8179 = 3, INKTAG_PANEL_DIAG_SSD1680 = 4,
       INKTAG_PANEL_DIAG_JD79665AA_A0 = 5,
       INKTAG_PANEL_DIAG_SSD1683 = 6, INKTAG_PANEL_DIAG_UC8276 = 7 };
typedef struct {
    uint8_t command, length;
    uint16_t skip;
} inktag_panel_diag_read_plan_t;
inktag_status_v2_t inktag_panel_diag_uc_read_plan(uint8_t model,
    const inktag_panel_diag_request_t *, inktag_panel_diag_read_plan_t *);
/* SSD1680 Rev0.14 pp11,28-29: status and 11-byte display-option register view.
 * No bulk OTP dump, unique die identification, OTP load/program or LUT write. */
inktag_status_v2_t inktag_panel_diag_ssd1680_read_plan(
    const inktag_panel_diag_request_t *, inktag_panel_diag_read_plan_t *);
/* Safe controller plans, not generic register access. A0 uses R70/R40/R92;
 * 4096-byte MTP, exactly one dummy. R11 can trigger display and is NOT used. */
inktag_status_v2_t inktag_panel_diag_read_plan(uint8_t model,
    const inktag_panel_diag_request_t *, inktag_panel_diag_read_plan_t *);
typedef struct {
    uint8_t state, status, gates, length;
    uint32_t pon_ms, drf_ms, pof_ms;
    uint8_t data[INKTAG_PANEL_DIAG_MAX_DATA];
} inktag_panel_diag_result_t;

/* Additional v1 operations: DESCRIBE returns a 16-byte capability record;
 * RESET/POWER_CYCLE/RAM_WRITE return a 16-byte electrical observation, even
 * on failure. POWER_CYCLE and RAM_WRITE require confirm=1 and fixture=0.
 * RAM_WRITE means volatile transport ONLY, not verified panel RAM. No DRF.
 * Common owns the finite sequence. Ports supply bounded primitives, live
 * admission/mutex and mandatory power cutoff on every terminal/cancel path. */
typedef struct {
    void *context;
    bool (*reset)(void *, bool high);
    bool (*busy)(void *); /* Actual logic level, not normalized "busy". */
    bool (*write)(void *, uint8_t command, const uint8_t *, uint8_t length);
    bool (*read_start)(void *, uint8_t command, uint16_t skip, uint8_t length);
    bool (*read_poll)(void *, uint8_t *, uint8_t capacity, uint8_t *, bool *);
} inktag_panel_diag_io_t;
typedef struct {
    uint8_t model, operation, phase, flags, samples, initial_busy, last_busy;
    uint8_t last_command, remaining, failure;
    uint32_t mark_ms, tx_bytes, write_failures;
} inktag_panel_diag_probe_t;
inktag_status_v2_t inktag_panel_diag_probe_start(inktag_panel_diag_probe_t *,
    uint8_t model, const inktag_panel_diag_request_t *,
    const inktag_panel_diag_io_t *, uint32_t now_ms);
void inktag_panel_diag_probe_poll(inktag_panel_diag_probe_t *,
    const inktag_panel_diag_request_t *, const inktag_panel_diag_io_t *,
    uint32_t now_ms, inktag_panel_diag_result_t *);
/* Best-effort POF only when BUSY is idle and not already sent; caller MUST
 * immediately cut board supply/reset even if this returns false. No wait. */
bool inktag_panel_diag_probe_stop(inktag_panel_diag_probe_t *,
    const inktag_panel_diag_io_t *, inktag_panel_diag_result_t *);
inktag_status_v2_t inktag_panel_diag_describe(uint8_t model, uint8_t output[16]);
typedef struct {
    void *context;
    /* Live AUTH, stable identity/profile/pins, board read-side effects, voltage,
       ownership and qualified fixture checks. No missing callback fallback. */
    inktag_status_v2_t (*guard)(void *, const inktag_panel_diag_request_t *);
    bool (*acquire)(void *);
    inktag_status_v2_t (*start)(void *, const inktag_panel_diag_request_t *);
    void (*poll)(void *, inktag_panel_diag_result_t *);
    /* Always bounded POF/HV-off, reset/SDA restore, invalidate old-frame shadow.
       Retain mutex if safe shutdown fails; never release into a powered panel. */
    inktag_status_v2_t (*cleanup)(void *);
} inktag_panel_diag_backend_t;
typedef struct {
    inktag_panel_diag_backend_t backend;
    inktag_panel_diag_request_t request;
    inktag_panel_diag_result_t result;
    uint32_t token, opened_ms, started_ms, polled_ms;
    bool acquired, cleanup_failed;
} inktag_panel_diag_t;
void inktag_panel_diag_init(inktag_panel_diag_t *, const inktag_panel_diag_backend_t *);
inktag_status_v2_t inktag_panel_diag_close(inktag_panel_diag_t *);
void inktag_panel_diag_tick(inktag_panel_diag_t *, uint32_t now_ms);
/* Route only after ordinary V2 AUTH, flags and connection-owner validation.
   On disconnect/reset/profile change call close. Token alone is NOT AUTH. */
inktag_status_v2_t inktag_panel_diag_dispatch(inktag_panel_diag_t *, uint8_t opcode,
    const uint8_t *payload, uint16_t size, uint32_t now_ms,
    uint8_t *output, uint16_t capacity, uint16_t *written);
#endif
#endif
