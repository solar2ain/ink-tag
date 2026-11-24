#ifndef INKTAG_DA_BLE_SDK_ADDRESS_H
#define INKTAG_DA_BLE_SDK_ADDRESS_H

#include "gapm.h"
#include "gapm_task.h"
#include "user_config.h"
#include "inktag_da_ble_address.h"

/* Same read-only adapter in App and Recovery. Check the configured AND live
 * SDK type explicitly; never infer type from address bits or change it to
 * make a value pass. dev_bdaddr is the retained OTP copy from arch_system.c,
 * not the NVDS getter which silently substitutes role-specific samples. */
static inline bool inktag_da_ble_sdk_address(inktag_ble_address_t *output)
{
    extern struct bd_addr dev_bdaddr;
    if (user_gapm_conf.addr_type != GAPM_CFG_ADDR_PUBLIC ||
        GAPM_F_GET(gapm_env.cfg_flags, ADDR_TYPE) != GAPM_CFG_ADDR_PUBLIC)
        return false;
    return inktag_da_ble_factory_address(dev_bdaddr.addr, gapm_env.addr.addr,
        INKTAG_BLE_ADDRESS_PUBLIC, output);
}

#endif
