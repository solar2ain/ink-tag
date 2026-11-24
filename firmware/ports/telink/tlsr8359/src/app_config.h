#ifndef INKTAG_TLSR_APP_CONFIG_H
#define INKTAG_TLSR_APP_CONFIG_H

#include "inktag_tlsr_flash_layout.h"
/* SDK software defaults must agree in both images, not remain at 0x76000. */
#define CFG_ADR_MAC INKTAG_TELINK_MAC_SECTOR
#define CUST_CAP_INFO_ADDR INKTAG_TELINK_CAP_SECTOR
#define CUST_FIRMWARE_SIGNKEY_ADDR INKTAG_TELINK_SIGNKEY_ADDRESS

/* Human-selected normal App/Recovery clock after exact-unit validation.
 * Peripheral wire rates and timer units must remain independent of CPU gain. */
#define CLOCK_SYS_CLOCK_HZ 48000000
#define INKTAG_TLSR_CPU_CLOCK_SOURCE SYS_CLK_48M_Crystal
#define INKTAG_TLSR_EPD_SPI_DIVIDER (CLOCK_SYS_CLOCK_HZ / (3000000u * 2u) - 1u)
#define ADVERTISING_INTERVAL 160
#define RAM _attribute_data_retention_
#define INKTAG_WATCHDOG_TIMEOUT_MS 75000u

/* gpio_init(false) still reloads all digital GPIO registers after retention
 * wake.  Keep the stock PA1 BUSY_N input contract in that default image; the
 * analog 100 kOhm pull-down (mode 2) survives retention and is also asserted by the
 * EPD driver before every transaction. */
#define PA1_FUNC AS_GPIO
#define PA1_INPUT_ENABLE 1
#define PA1_OUTPUT_ENABLE 0
#define PULL_WAKEUP_SRC_PA1 2

/* Preload the inactive HIGH latch and pull-up, but do not enable the legacy
 * PB4/PD2/PD3 output drivers before the Factory + ID7 routing proof. A later
 * custom PCB may use those pads for another role. Validated EPD/LED adapters
 * reclaim their configured outputs explicitly after boot/retention. PA7
 * keeps AS_SWIRE; fixed NFC restore and the PM policy remain unchanged.
 * Exact-board wake/idle electrical regression is still required. */
#define PB4_FUNC AS_GPIO
#define PB4_INPUT_ENABLE 0
#define PB4_OUTPUT_ENABLE 0
#define PB4_DATA_OUT 1
#define PULL_WAKEUP_SRC_PB4 1

#define PD2_FUNC AS_GPIO
#define PD2_INPUT_ENABLE 0
#define PD2_OUTPUT_ENABLE 0
#define PD2_DATA_OUT 1
#define PULL_WAKEUP_SRC_PD2 1

#define PD3_FUNC AS_GPIO
#define PD3_INPUT_ENABLE 0
#define PD3_OUTPUT_ENABLE 0
#define PD3_DATA_OUT 1
#define PULL_WAKEUP_SRC_PD3 1

#define PC0_FUNC AS_GPIO
#define PC0_INPUT_ENABLE 1
#define PC0_OUTPUT_ENABLE 0
#define PULL_WAKEUP_SRC_PC0 3

#define PC1_FUNC AS_GPIO
#define PC1_INPUT_ENABLE 1
#define PC1_OUTPUT_ENABLE 0
#define PULL_WAKEUP_SRC_PC1 3

#define PC4_FUNC AS_GPIO
#define PC4_INPUT_ENABLE 1
#define PC4_OUTPUT_ENABLE 0
#define PULL_WAKEUP_SRC_PC4 3

#define PC6_FUNC AS_GPIO
#define PC6_INPUT_ENABLE 0
#define PC6_OUTPUT_ENABLE 1
#define PC6_DATA_OUT 1
#define PULL_WAKEUP_SRC_PC6 1

enum {
    CLOCK_SYS_CLOCK_1S = CLOCK_SYS_CLOCK_HZ,
    CLOCK_SYS_CLOCK_1MS = CLOCK_SYS_CLOCK_HZ / 1000,
    CLOCK_SYS_CLOCK_1US = CLOCK_SYS_CLOCK_HZ / 1000000,
};

typedef enum {
    ATT_H_START = 0,
    GenericAccess_PS_H,
    GenericAccess_DeviceName_CD_H,
    GenericAccess_DeviceName_DP_H,
    GenericAccess_Appearance_CD_H,
    GenericAccess_Appearance_DP_H,
    CONN_PARAM_CD_H,
    CONN_PARAM_DP_H,
    GenericAttribute_PS_H,
    GenericAttribute_ServiceChanged_CD_H,
    GenericAttribute_ServiceChanged_DP_H,
    GenericAttribute_ServiceChanged_CCB_H,
    INKTAG_PS_H,
    INKTAG_EPD_CD_H,
    INKTAG_EPD_DP_H,
    INKTAG_EPD_CCC_H,
    INKTAG_VERSION_CD_H,
    INKTAG_VERSION_DP_H,
    ATT_END_H,
} ATT_HANDLE;

#include "vendor/common/default_config.h"

#endif
