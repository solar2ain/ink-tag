#ifndef INKTAG_OM_BLE_RESET_H
#define INKTAG_OM_BLE_RESET_H

#include "om_driver.h"

/* A CPU-only reset can leave the OM6626 BLE/PHY domains reporting a stale
 * advertising or connection state. Reset both peripherals before the RF and
 * BLE stacks are initialized so App and Recovery always start a fresh role. */
static inline void inktag_om_ble_reset_for_role_start(void) {
    DRV_RCC_RESET(RCC_CLK_BLE);
    DRV_RCC_RESET(RCC_CLK_PHY);
}

#endif
