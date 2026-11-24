#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "evt.h"
#include "evt_timer.h"
#include "inktag_om_ble_reset.h"
#include "inktag_om_debug_retention.h"
#include "inktag_om_swap.h"
#include "inktag_om_voltage.h"
#include "inktag_om_address.h"
#include "inktag_status.h"
#include "inktag_code_guard.h"
#include "inktag_recovery_version.h"
#include "board.h"
#include "nvds.h"
#include "nvds_tags.h"
#include "ob_config.h"
#include "onmicro_dfu.h"
#include "om.h"
#include "om_driver.h"
#include "om_device.h"
#include "omble.h"
#include "pm.h"
#include "service_onmicro_dfu.h"
#include "inktag_om_flash_layout.h"
#include "inktag_om_pins.h"

#define INKTAG_OM_APP_XIP_END \
    (INKTAG_OM_APP_XIP_ADDRESS + INKTAG_OM_SLOT_SIZE)
#define INKTAG_OM_RAM_START       0x20000000u
#define INKTAG_OM_RAM_END         0x20013EA0u
#define INKTAG_OM_RECOVERY_LED_TICK_MS      120u
#define INKTAG_OM_RECOVERY_TRANSFER_HALF_TICKS 2u
#define INKTAG_OM_RECOVERY_STARTUP_COLOR_TICKS \
    (INKTAG_STATUS_STARTUP_COLOR_MS / INKTAG_OM_RECOVERY_LED_TICK_MS)
#define INKTAG_OM_RECOVERY_READY_HALF_TICKS \
    (INKTAG_STATUS_READY_HALF_MS / INKTAG_OM_RECOVERY_LED_TICK_MS)
#define INKTAG_OM_RECOVERY_ADV_RETRY_MS     500u
#include "inktag_recovery_led.h"
#define INKTAG_OM_RECOVERY_LED_RED          0x01u
#define INKTAG_OM_RECOVERY_LED_GREEN        0x02u
#define INKTAG_OM_RECOVERY_LED_BLUE         0x04u

_Static_assert(INKTAG_OM_RECOVERY_STARTUP_COLOR_TICKS == 3u,
               "Recovery LED table assumes three 120 ms ticks per startup color");
_Static_assert(INKTAG_OM_RECOVERY_READY_HALF_TICKS == 1u,
               "Recovery LED table assumes a 120 ms ready half-period");

typedef enum {
    RECOVERY_LED_OFF = 0,
    RECOVERY_LED_STARTUP,
    RECOVERY_LED_TRANSFER,
    RECOVERY_LED_INSTALL,
    RECOVERY_LED_ERROR,
} recovery_led_mode_t;

extern void Reset_Handler(void);

/* Offline identity only; IKRP retains its 20-byte prefix plus the explicit policy suffix. */
__attribute__((used, section(".inktag_recovery_info")))
const uint8_t inktag_om_recovery_info[8] = INKTAG_RECOVERY_INFO_BYTES;

_Static_assert(INKTAG_RECOVERY_EPOCH == 1u,
               "OM layout-v1 Recovery keeps compatibility epoch 1");

__attribute__((naked, used, section(".inktag_boot_entry")))
void inktag_om_fixed_boot_entry(void) {
    __asm volatile("b Reset_Handler");
}

static ob_adv_param_t s_adv_param;
static ob_gap_addr_t s_peer_addr = {1, {0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC}};
static uint8_t s_local_addr[6];
static uint8_t s_local_addr_type;
static volatile uint8_t s_identity_status;
static char s_recovery_name[] = "IKT_OM_0000_OTA";
static uint8_t s_scan_rsp_data[31];
static const uint8_t s_boot_led[] = {
    INKTAG_OM_RECOVERY_LED_RED, INKTAG_OM_RECOVERY_LED_RED,
    INKTAG_OM_RECOVERY_LED_RED, INKTAG_OM_RECOVERY_LED_GREEN,
    INKTAG_OM_RECOVERY_LED_GREEN, INKTAG_OM_RECOVERY_LED_GREEN,
    INKTAG_OM_RECOVERY_LED_BLUE, INKTAG_OM_RECOVERY_LED_BLUE,
    INKTAG_OM_RECOVERY_LED_BLUE, 0u,
    INKTAG_OM_RECOVERY_LED_BLUE, 0u,
    INKTAG_OM_RECOVERY_LED_BLUE, 0u,
    INKTAG_OM_RECOVERY_LED_BLUE, 0u,
};
static evt_timer_t s_led_timer;
static evt_timer_t s_adv_retry_timer;
static volatile bool s_reboot_pending;
static uint8_t s_return_kind;
static volatile bool s_adv_retry_due;
static volatile bool s_adv_retry_armed;
static volatile bool s_advertising_active;
static volatile bool s_connected;
static volatile bool s_role_ready;
static bool s_led_timer_armed;
static bool s_transfer_pwm_active;
static recovery_led_mode_t s_led_mode;
static uint32_t s_led_started_tick;

static uint8_t s_adv_data[] = {
    0x02, 0x01, 0x06,
    0x11, 0x07,
    0x41, 0x54, 0x4F, 0x2D, 0x4D, 0x4F, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x5F, 0xBF, 0x01, 0x00, 0x00,
};
static ob_data_t s_adv = {s_adv_data, sizeof(s_adv_data)};
static ob_data_t s_scan_rsp = {s_scan_rsp_data, 0};

static char hex_digit(uint8_t value) {
    value &= 0x0Fu;
    return (char)(value < 10u ? '0' + value : 'A' + value - 10u);
}

static void recovery_led_init(void) {
    const uint8_t pins[] = {
        inktag_om_pins_led(0u), inktag_om_pins_led(1u), inktag_om_pins_led(2u),
    };
    for (uint8_t i = 0u; i < sizeof(pins); i++) {
        if(pins[i]>25u)continue;
        pin_config_t pin_cfg = {
            pins[i], {PINMUX_GPIO_MODE_CFG}, PMU_PIN_MODE_PP,
            PMU_PIN_DRIVER_CURRENT_NORMAL
        };
        gpio_config_t gpio_cfg = {
            OM_GPIO0, pins[i], GPIO_DIR_OUTPUT,
            inktag_om_pins_active_low((inktag_pin_role_t)(INKTAG_PIN_LED_RED+i))?GPIO_LEVEL_HIGH:GPIO_LEVEL_LOW,
            GPIO_TRIG_NONE
        };
        drv_pin_init(&pin_cfg, 1);
        drv_gpio_init(&gpio_cfg, 1);
    }
}

static void recovery_led_write(uint8_t pin, bool on) {
    if(pin>25u)return;
    bool low=true;
    for(unsigned i=0;i<3u;++i)
        if(pin==inktag_om_pins_led(i))low=inktag_om_pins_active_low((inktag_pin_role_t)(INKTAG_PIN_LED_RED+i));
    gpio_level_t level = (on!=low)?GPIO_LEVEL_HIGH:GPIO_LEVEL_LOW;
    drv_gpio_write(OM_GPIO0, 1u << pin, level);
}

static void recovery_led_output(bool red, bool green, bool blue) {
    recovery_led_write(inktag_om_pins_led(0u), red);
    recovery_led_write(inktag_om_pins_led(1u), green);
    recovery_led_write(inktag_om_pins_led(2u), blue);
}

static void recovery_led_timer_handler(evt_timer_t *timer, void *context);

static void recovery_led_timer_stop(void) {
    if (s_led_timer_armed) {
        evt_timer_del(&s_led_timer);
        s_led_timer_armed = false;
    }
    recovery_led_output(false, false, false);
}

static void recovery_led_transfer_pwm_stop(void) {
    if (!s_transfer_pwm_active) return;
    drv_tim_pwm_output_stop(INKTAG_OM_RECOVERY_PWM_TIMER, TIM_CHAN_ALL);
    (void)drv_tim_uninit(INKTAG_OM_RECOVERY_PWM_TIMER);
    recovery_led_init();
    pm_sleep_allow(INKTAG_OM_RECOVERY_PWM_PM_ID);
    s_transfer_pwm_active = false;
}

static bool recovery_led_transfer_pwm_start(void) {
    if(inktag_om_pins_led(2u)!=INKTAG_OM_RECOVERY_LED_BLUE_PIN)return false;
    /* GPIO17 is Timer1 CH1N on both qualified OM ZKC boards. The LED is
     * active low, so the complementary output is low for the first 240 ms
     * of each 480 ms period. Hardware edges continue through DFU's IRQ-off
     * internal-Flash writes; an evt_timer callback cannot guarantee that. */
    tim_pwm_complementary_output_config_t config = {
        .cnt_freq = 1000u,
        .period_cnt = 480u,
    };
    pin_config_t pin = {
        INKTAG_OM_RECOVERY_LED_BLUE_PIN,
        {INKTAG_OM_RECOVERY_PWM_PINMUX},
        PMU_PIN_MODE_PP,
        PMU_PIN_DRIVER_CURRENT_NORMAL,
    };
    config.chan[INKTAG_OM_RECOVERY_PWM_CHANNEL].en = 1u;
    config.chan[INKTAG_OM_RECOVERY_PWM_CHANNEL].cfg.pol =
        inktag_om_pins_active_low(INKTAG_PIN_LED_BLUE)?TIM_PWM_POL_ACTIVE_HIGH:TIM_PWM_POL_ACTIVE_LOW;
    config.chan[INKTAG_OM_RECOVERY_PWM_CHANNEL].cfg.oc_val = 240u;
    config.chan[INKTAG_OM_RECOVERY_PWM_CHANNEL].cfg.complementary_output_enable = true;
    if (drv_tim_init(INKTAG_OM_RECOVERY_PWM_TIMER) != OM_ERROR_OK) return false;
    if (drv_tim_pwm_complementary_output_start(INKTAG_OM_RECOVERY_PWM_TIMER, &config) !=
        OM_ERROR_OK) {
        (void)drv_tim_uninit(INKTAG_OM_RECOVERY_PWM_TIMER);
        return false;
    }
    pm_sleep_prevent(INKTAG_OM_RECOVERY_PWM_PM_ID);
    drv_pin_init(&pin, 1u);
    s_transfer_pwm_active = true;
    return true;
}

static void recovery_led_set(recovery_led_mode_t mode) {
    recovery_led_transfer_pwm_stop();
    s_led_mode = mode;
    s_led_started_tick = drv_pmu_timer_cnt_get();
    if (mode == RECOVERY_LED_OFF) {
        recovery_led_timer_stop();
    } else if (mode == RECOVERY_LED_TRANSFER) {
        recovery_led_timer_stop();
        if (recovery_led_transfer_pwm_start()) return;
    }
    if (mode != RECOVERY_LED_OFF && !s_led_timer_armed) {
        s_led_timer_armed = evt_timer_set(
            &s_led_timer, INKTAG_OM_RECOVERY_LED_TICK_MS,
            EVT_TIMER_REPEAT, recovery_led_timer_handler, NULL);
    }
    if (mode != RECOVERY_LED_OFF) recovery_led_timer_handler(NULL, NULL);
}

static void recovery_led_timer_handler(evt_timer_t *timer, void *context) {
    (void)timer;
    (void)context;
    uint32_t now_tick = drv_pmu_timer_cnt_get();
    /* DFU Flash writes can delay evt_schedule(). Count elapsed PMU ticks,
     * not delivered callbacks, or every delayed tick stretches the pattern. */
    uint32_t phase = (now_tick - s_led_started_tick) /
        PMU_TIMER_MS2TICK(INKTAG_OM_RECOVERY_LED_TICK_MS);
    uint8_t leds = 0u;
    if (s_led_mode == RECOVERY_LED_STARTUP) {
        if (phase >= INKTAG_OM_RECOVERY_STARTUP_COLOR_TICKS * 3u &&
            !s_role_ready) {
            /* Hold the required dark separator until BLE advertising is
             * genuinely ready; no role-success pulse is emitted early. */
            phase = INKTAG_OM_RECOVERY_STARTUP_COLOR_TICKS * 3u;
        } else if (phase < sizeof(s_boot_led)) {
            leds = s_boot_led[phase];
        } else {
            s_led_mode = RECOVERY_LED_OFF;
        }
    } else if (s_led_mode == RECOVERY_LED_TRANSFER) {
        if ((phase % (INKTAG_OM_RECOVERY_TRANSFER_HALF_TICKS * 2u)) <
            INKTAG_OM_RECOVERY_TRANSFER_HALF_TICKS)
            leds = INKTAG_OM_RECOVERY_LED_BLUE;
    } else if (s_led_mode == RECOVERY_LED_INSTALL) {
        if ((phase & 1u) == 0u) leds = INKTAG_OM_RECOVERY_LED_BLUE;
    } else if (s_led_mode == RECOVERY_LED_ERROR) {
        if (phase < 6u && (phase & 1u) == 0u)
            leds = INKTAG_OM_RECOVERY_LED_RED;
        if (phase >= 6u) s_led_mode = RECOVERY_LED_OFF;
    }
    recovery_led_output((leds & INKTAG_OM_RECOVERY_LED_RED) != 0u,
                        (leds & INKTAG_OM_RECOVERY_LED_GREEN) != 0u,
                        (leds & INKTAG_OM_RECOVERY_LED_BLUE) != 0u);
    if (s_led_mode == RECOVERY_LED_OFF) recovery_led_timer_stop();
}

static void recovery_led_role_ready(void) {
    if (s_role_ready) return;
    if (s_led_mode == RECOVERY_LED_STARTUP) {
        uint32_t now_tick = drv_pmu_timer_cnt_get();
        uint32_t separator_tick = PMU_TIMER_MS2TICK(
            INKTAG_OM_RECOVERY_LED_TICK_MS) *
            INKTAG_OM_RECOVERY_STARTUP_COLOR_TICKS * 3u;
        if (now_tick - s_led_started_tick >= separator_tick)
            s_led_started_tick = now_tick - separator_tick;
    }
    s_role_ready = true;
    if (s_led_mode == RECOVERY_LED_STARTUP)
        recovery_led_timer_handler(NULL, NULL);
}

static bool app_vectors_valid(uint32_t stack, uint32_t reset) {
    uint32_t reset_address = reset & ~1u;
    return stack >= INKTAG_OM_RAM_START && stack <= INKTAG_OM_RAM_END &&
           reset_address >= INKTAG_OM_APP_XIP_ADDRESS &&
           reset_address < INKTAG_OM_APP_XIP_END && (reset & 1u) != 0u;
}

INKTAG_GUARD_C static bool app_image_valid(uint32_t *stack_out, uint32_t *reset_out) {
    const uint32_t *vectors = (const uint32_t *)INKTAG_OM_APP_XIP_ADDRESS;
    const inktag_om_launch_record_t *launch =
        (const inktag_om_launch_record_t *)INKTAG_OM_LAUNCH_RECORD_XIP;
    uint32_t stack = vectors[0];
    uint32_t reset = launch->reset_handler;
    if (launch->magic != INKTAG_OM_LAUNCH_RECORD_MAGIC ||
        launch->guard != INKTAG_OM_LAUNCH_RECORD_GUARD ||
        !app_vectors_valid(stack, reset)) {
        return false;
    }
    if (stack_out) *stack_out = stack;
    if (reset_out) *reset_out = reset;
    return true;
}

static void jump_to_application(void) {
    uint32_t stack;
    uint32_t reset;
    if (!app_image_valid(&stack, &reset) ||
        !inktag_om_swap_installed_app_valid()) return;
    __disable_irq();
    SysTick->CTRL = 0u;
    SCB->VTOR = INKTAG_OM_APP_XIP_ADDRESS;
    __DSB();
    __ISB();
    /* Both operands must be in registers before MSP changes. A normal C call
     * may reload the entry from the new, uninitialized application stack. */
    __asm volatile(
        "msr msp, %0\n"
        "bx %1\n"
        :
        : "r"(stack), "r"(reset)
        : "memory");
    __builtin_unreachable();
}

static void load_recovery_identity(uint8_t nvds_status) {
    s_identity_status = inktag_om_address_load(nvds_status, s_local_addr, &s_local_addr_type);
    if (s_identity_status > INKTAG_OM_ADDRESS_HS_LEGACY)
        for (;;) { __WFI(); }
    s_recovery_name[7] = hex_digit((uint8_t)(s_local_addr[1] >> 4));
    s_recovery_name[8] = hex_digit(s_local_addr[1]);
    s_recovery_name[9] = hex_digit((uint8_t)(s_local_addr[0] >> 4));
    s_recovery_name[10] = hex_digit(s_local_addr[0]);
    uint8_t name_len = (uint8_t)(sizeof(s_recovery_name) - 1u);
    s_scan_rsp_data[0] = (uint8_t)(name_len + 1u);
    s_scan_rsp_data[1] = 0x09;
    memcpy(&s_scan_rsp_data[2], s_recovery_name, name_len);
    s_scan_rsp.len = (uint16_t)(name_len + 2u);
}

static uint32_t start_advertising(void) {
    memset(&s_adv_param, 0, sizeof(s_adv_param));
    s_adv_param.own_addr_type = s_local_addr_type;
    s_adv_param.prim_phy = OB_ADV_PHY_1M;
    s_adv_param.secd_phy = OB_ADV_PHY_1M;
    s_adv_param.tx_pwr = 0;
    s_adv_param.filter_policy = OB_ADV_FILTER_NONE;
    s_adv_param.prim_ch_map = OB_ADV_CH_ALL;
    s_adv_param.prim_intv_min = 0xA0;
    s_adv_param.prim_intv_max = 0xA0;
    s_adv_param.local_addr = s_local_addr;
    s_adv_param.peer_addr = &s_peer_addr;
    s_adv_param.adv_properties = OB_ADV_PROP_LEGACY_IND;
    return ob_gap_adv_start(0, &s_adv_param, &s_adv, &s_scan_rsp);
}

static void advertising_retry_timer_handler(evt_timer_t *timer, void *context) {
    (void)timer;
    (void)context;
    s_adv_retry_armed = false;
    s_adv_retry_due = true;
}

static void schedule_advertising_retry(void) {
    if (s_connected || s_advertising_active || s_adv_retry_armed) return;
    if (evt_timer_set(&s_adv_retry_timer, INKTAG_OM_RECOVERY_ADV_RETRY_MS,
                      EVT_TIMER_ONE_SHOT, advertising_retry_timer_handler,
                      NULL)) {
        s_adv_retry_armed = true;
    } else {
        s_adv_retry_due = true;
    }
}

static void start_advertising_with_retry(void) {
    if (s_connected || s_advertising_active) return;
    /* A successful command is not proof that the controller reached STARTED.
     * Keep one watchdog retry armed until the GAP state event confirms it. */
    (void)start_advertising();
    schedule_advertising_retry();
}

static void recovery_gap_event(uint16_t evt_id, const omble_evt_t *evt) {
    if (evt_id == OB_GAP_EVT_CONNECTED) {
        s_connected = true;
        recovery_led_role_ready();
        s_advertising_active = false;
        s_adv_retry_due = false;
        if (s_adv_retry_armed) {
            evt_timer_del(&s_adv_retry_timer);
            s_adv_retry_armed = false;
        }
    } else if (evt_id == OB_GAP_EVT_DISCONNECTED) {
        s_connected = false;
        s_advertising_active = false;
        schedule_advertising_retry();
    } else if (evt_id == OB_GAP_EVT_ADV_STATE_CHANGED &&
               evt->gap.adv_state_changed.adv_idx == 0u) {
        if (evt->gap.adv_state_changed.state == OB_GAP_ADV_ST_STARTED) {
            s_advertising_active = true;
            recovery_led_role_ready();
            s_adv_retry_due = false;
            if (s_adv_retry_armed) {
                evt_timer_del(&s_adv_retry_timer);
                s_adv_retry_armed = false;
            }
        } else {
            s_advertising_active = false;
            schedule_advertising_retry();
        }
    } else if (evt_id == OB_GAP_EVT_CONN_PARAMS_REQUEST) {
        ob_gap_conn_params_t param = {0x18, 0, 500};
        ob_gap_conn_param_update(evt->gap.conn_idx, &param);
    }
}

static bool recovery_return_allowed(bool completed_ota) {
    if (!inktag_om_voltage_operation_allowed(INKTAG_POWER_OPERATION_FLASH) ||
        !app_image_valid(NULL, NULL) ||
        !inktag_om_swap_installed_app_valid()) return false;
    if (!completed_ota) return inktag_om_swap_unmodified_recovery();
    inktag_swap_state_t state = INKTAG_SWAP_STATE_NONE;
    return inktag_om_swap_status(&state, NULL) == INKTAG_SWAP_OK &&
           state == INKTAG_SWAP_STATE_TEST_PENDING;
}

void app_onmicro_dfu_update_end_ind_handler(uint8_t status, void *context) {
    (void)context;
    if (status == DFU_UPDATE_ST_SUCCESS) {
        /* Recovery has only committed a verified candidate.  Keep success
         * blue while installation/reboot is pending; the trial application
         * alone emits green after BLE service, advertising and confirmation. */
        recovery_led_set(RECOVERY_LED_INSTALL);
        /* Called only after this terminal response's real TX_COMPLETE. */
        s_return_kind = 2u;
        s_reboot_pending = true;
    } else {
        recovery_led_set(RECOVERY_LED_ERROR);
    }
}

bool app_onmicro_dfu_safe_resume_handler(bool rejected) {
    if (rejected) recovery_led_set(RECOVERY_LED_ERROR);
    /* Admission is read-only. No journal cancellation or reboot before ACK. */
    return recovery_return_allowed(false);
}

void app_onmicro_dfu_safe_resume_tx_complete(void) {
    s_return_kind = 1u;
    s_reboot_pending = true;
}

static void process_pending_return(void) {
    if (!s_reboot_pending) return;
    s_reboot_pending = false;
    bool completed_ota = s_return_kind == 2u;
    s_return_kind = 0u;
    /* Main-loop context, after event dispatch. The service closes command
     * admission once the terminal ACK is queued, including during this check.
     * A failed recheck remains in Recovery; reconnect can begin a new attempt. */
    if (!recovery_return_allowed(completed_ota) ||
        (!completed_ota && !inktag_om_swap_cancel_unmodified_recovery())) {
        recovery_led_set(RECOVERY_LED_ERROR);
        return;
    }
    inktag_om_swap_enter_fixed_boot();
}

void app_onmicro_dfu_update_start_ind_handler(uint8_t status, void *context) {
    (void)status;
    (void)context;
    ob_gap_conn_params_t param = {0x0c, 0, 500};
    ob_gap_conn_param_update(0, &param);
    recovery_led_set(RECOVERY_LED_TRANSFER);
}

void app_onmicro_dfu_update_prog_ind_handler(uint8_t status, void *context) {
    (void)status;
    (void)context;
    if (s_led_mode == RECOVERY_LED_OFF) {
        recovery_led_set(RECOVERY_LED_TRANSFER);
    }
}

static void start_ble_recovery(void) {
    struct ob_stack_param param = {
        .max_connection = 1,
        .max_ext_adv_set = 1,
        .max_att_mtu = OB_LE_HOST_ATT_MTU,
        .max_gatt_serv_num = 2,
        .max_gatt_write_cache = OB_LE_HOST_ATT_WRITE_CACHE_SIZE,
        .smp_sc_support = 0,
    };
    drv_pmu_pin_io13_reset_mode_enable(1);
    drv_pmu_jtag_enable(true);
    drv_wdt_init(0u);
    board_init();
    /* GPIO0/1 are the OM6626 SWCLK/SWDIO pads. The application may leave
     * them in NFC I2C mode across its low-power Recovery handoff. Restore
     * their debug mux explicitly; Recovery never uses the NFC I2C bus. */
    static const pin_config_t swd_pins[] = {
        {0u, {PINMUX_JTAG_MODE_CFG}, PMU_PIN_MODE_PU,
         PMU_PIN_DRIVER_CURRENT_NORMAL},
        {1u, {PINMUX_JTAG_MODE_CFG}, PMU_PIN_MODE_PU,
         PMU_PIN_DRIVER_CURRENT_NORMAL},
    };
    drv_pin_init(swd_pins, sizeof(swd_pins) / sizeof(swd_pins[0]));
    drv_pmu_jtag_enable(true);
    inktag_om_ble_reset_for_role_start();
    drv_rf_init();
    evt_init();
    evt_timer_init();
    inktag_om_swd_attach_enable();
    load_recovery_identity(nvds_init(INKTAG_OM_NVDS_ADDRESS));
    inktag_om_pins_reload();
    recovery_led_init();
    recovery_led_set(RECOVERY_LED_STARTUP);
    (void)omble_init(&param);
    app_onmicro_dfu_init();
    (void)ob_event_callback_reg(recovery_gap_event);
    drv_pmu_32k_enable_in_deep_sleep(true);
    pm_sleep_allow(PM_ID_SHELL);
    pm_sleep_enable(true);
    __enable_irq();
    start_advertising_with_retry();
    while (1) {
        evt_schedule();
        if (s_adv_retry_due) {
            s_adv_retry_due = false;
            start_advertising_with_retry();
        }
        process_pending_return();
        /* Keep the queue test and sleep entry atomic. Timer/GAP callbacks set
         * the volatile work flags, so pending work also blocks WFI even when
         * the event queue itself has already drained. */
        OM_CRITICAL_BEGIN();
        if (evt_get_all() == 0u && !s_adv_retry_due && !s_reboot_pending) {
            pm_power_manage();
        }
        OM_CRITICAL_END();
    }
}

int main(void) {
    drv_pmu_jtag_enable(true);
    drv_wdt_init(0u);
    inktag_swap_result_t result = inktag_om_swap_boot(NULL, NULL);
    if ((result == INKTAG_SWAP_OK || result == INKTAG_SWAP_NOT_PENDING) &&
        app_image_valid(NULL, NULL)) {
        jump_to_application();
    }
    start_ble_recovery();
    for (;;) { }
}
