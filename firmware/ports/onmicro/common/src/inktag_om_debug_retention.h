#ifndef INKTAG_OM_DEBUG_RETENTION_H
#define INKTAG_OM_DEBUG_RETENTION_H

#include "evt_timer.h"
#include "pm.h"
#include "om_driver.h"

#define INKTAG_OM_SWD_ATTACH_WINDOW_MS 30000u

/* App owns the attach window for the shared PAD0/1 SWD pins. */
bool inktag_om_swd_attach_window_active(void);

#if defined(INKTAG_OM_APPLICATION_DEBUG_WINDOW)
static evt_timer_t s_inktag_om_swd_attach_timer;
static bool s_inktag_om_swd_window_active;

bool inktag_om_swd_attach_window_active(void) {
    return s_inktag_om_swd_window_active;
}

static void inktag_om_swd_attach_window_expired(evt_timer_t *timer,
                                                 void *context) {
    (void)timer;
    (void)context;
    s_inktag_om_swd_window_active = false;
    pm_sleep_allow(PM_ID_USER3);
}

static inline void inktag_om_swd_attach_window_start(void) {
    /* Keep reset-less SWD attach available for the product-wide application
     * grace period. If the timer cannot be armed, fail safe by retaining the
     * stronger permanent-attach behavior instead of closing the debug path. */
    pm_sleep_prevent(PM_ID_USER3);
    const pin_config_t pins[] = {
        {0u, {PINMUX_JTAG_MODE_CFG}, PMU_PIN_MODE_PU,
         PMU_PIN_DRIVER_CURRENT_NORMAL},
        {1u, {PINMUX_JTAG_MODE_CFG}, PMU_PIN_MODE_PU,
         PMU_PIN_DRIVER_CURRENT_NORMAL},
    };
    s_inktag_om_swd_window_active = true;
    drv_pin_init(pins, sizeof(pins) / sizeof(pins[0]));
    drv_pmu_jtag_enable(true);
    (void)evt_timer_set(&s_inktag_om_swd_attach_timer,
                        INKTAG_OM_SWD_ATTACH_WINDOW_MS,
                        EVT_TIMER_ONE_SHOT,
                        inktag_om_swd_attach_window_expired,
                        NULL);
}
#endif

static inline void inktag_om_swd_attach_enable(void) {
    /* Fixed Recovery must remain directly attachable for its entire lifetime.
     * PM_STATUS_SLEEP gates the OM6626 DAP, so cap Recovery at IDLE/WFI. */
    pm_sleep_prevent(PM_ID_USER3);
}

#endif
