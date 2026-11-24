#include "inktag_nrf_health.h"

#include <stddef.h>
#include <string.h>

#include "inktag_epd_driver.h"
#include "inktag_epd_temperature.h"
#include "inktag_health_store_retry.h"
#include "inktag_board.h"
#include "inktag_nrf_record_store.h"
#include "inktag_protocol_core.h"
#include "inktag_status.h"
#include "nrf.h"
#include "nrf_error.h"
#include "nrf_soc.h"

#define HEALTH_STORE_MAGIC 0x31484B49u /* IKH1 */
#define HEALTH_STORE_VERSION 1u
#define HEALTH_MAILBOX_MAGIC 0x48534C54u /* HSLT */
#define HEALTH_RECORD_COUNT 4u
#define HEALTH_SAMPLE_INTERVAL_MS 5000u
#define HEALTH_STABLE_UPTIME_MS 60000u
#define PANEL_TEMPERATURE_MAX_AGE_MS (15u * 60u * 1000u)

typedef enum {
    HEALTH_CRASH_NONE = 0,
    HEALTH_CRASH_WATCHDOG = 1,
    HEALTH_CRASH_HARDFAULT = 2,
} health_crash_reason_t;

typedef struct {
    uint8_t reason;
    uint8_t reset_reason;
    uint8_t operation;
    uint8_t reserved;
    uint16_t voltage_mv;
    uint16_t reserved2;
    uint32_t uptime_ms;
    uint32_t stage;
    uint32_t pc;
    uint32_t lr;
    uint32_t cfsr;
    uint32_t hfsr;
} health_crash_record_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t record_count;
    uint8_t next_record;
    uint8_t consecutive_crashes;
    uint8_t warning_page_shown;
    uint8_t reserved[3];
    uint32_t total_crashes;
    health_crash_record_t records[HEALTH_RECORD_COUNT];
    uint32_t crc32;
} health_store_t;

typedef struct {
    uint32_t magic;
    uint32_t inverse;
    health_crash_record_t record;
    uint32_t checksum;
} health_mailbox_t;

static const inktag_power_thresholds_t s_thresholds =
    INKTAG_POWER_POLICY_THRESHOLDS_INITIALIZER;

static volatile health_mailbox_t s_mailbox
    __attribute__((section(".inktag_noinit"), used));
static health_store_t s_store;
static health_crash_record_t s_pending_crash;
static bool s_pending_crash_valid;
static bool s_pending_crash_in_store;
static bool s_store_dirty;
static bool s_store_write_pending;
static bool s_store_retry_pending;
static bool s_initialized;
static bool s_mcu_temperature_valid;
static bool s_panel_temperature_valid;
static bool s_watchdog_active;
static bool s_stable_marked;
static uint8_t s_reset_reason;
static int16_t s_mcu_temperature_sixteenths;
static int16_t s_panel_temperature_sixteenths;
static uint32_t s_now_ms;
static uint32_t s_last_store_attempt_ms;
static uint32_t s_last_policy_sample_ms;
static uint32_t s_last_operation_sample_ms;
static uint32_t s_panel_temperature_sampled_ms;
static uint32_t s_stage;
static inktag_power_sample_state_t s_power_samples;
static inktag_nrf_health_operation_t s_operation =
    INKTAG_NRF_HEALTH_OPERATION_BOOT;

static uint32_t crc32_for(const void *value, uint16_t length) {
    return inktag_crc32_update(UINT32_MAX, value, length) ^ UINT32_MAX;
}

static bool mailbox_valid(void) {
    health_mailbox_t copy;
    memcpy(&copy, (const void *)&s_mailbox, sizeof(copy));
    return copy.magic == HEALTH_MAILBOX_MAGIC &&
           copy.inverse == ~HEALTH_MAILBOX_MAGIC &&
           copy.record.reason != HEALTH_CRASH_NONE &&
           copy.checksum == crc32_for(&copy.record, sizeof(copy.record));
}

static void mailbox_write(health_crash_reason_t reason, uint32_t pc,
                          uint32_t lr, uint32_t cfsr, uint32_t hfsr) {
    health_crash_record_t record = {
        .reason = (uint8_t)reason,
        .reset_reason = s_reset_reason,
        .operation = (uint8_t)s_operation,
        .voltage_mv = s_power_samples.operation_active ?
                          (s_power_samples.operation_valid ?
                               s_power_samples.operation_mv : 0u) :
                          (s_power_samples.policy_valid ?
                               s_power_samples.policy_mv : 0u),
        .uptime_ms = s_now_ms,
        .stage = s_stage,
        .pc = pc,
        .lr = lr,
        .cfsr = cfsr,
        .hfsr = hfsr,
    };
    s_mailbox.magic = 0u;
    s_mailbox.record = record;
    s_mailbox.checksum = crc32_for(&record, sizeof(record));
    s_mailbox.inverse = ~HEALTH_MAILBOX_MAGIC;
    __DMB();
    s_mailbox.magic = HEALTH_MAILBOX_MAGIC;
    __DMB();
}

static void mailbox_clear(void) {
    s_mailbox.magic = 0u;
    s_mailbox.inverse = 0u;
    s_mailbox.checksum = 0u;
    __DMB();
}

static bool store_valid(const health_store_t *store) {
    return store->magic == HEALTH_STORE_MAGIC &&
           store->version == HEALTH_STORE_VERSION &&
           store->record_count <= HEALTH_RECORD_COUNT &&
           store->next_record < HEALTH_RECORD_COUNT &&
           store->crc32 == crc32_for(store, offsetof(health_store_t, crc32));
}

static void store_defaults(void) {
    memset(&s_store, 0, sizeof(s_store));
    s_store.magic = HEALTH_STORE_MAGIC;
    s_store.version = HEALTH_STORE_VERSION;
}

static uint16_t sample_voltage(void) {
    uint16_t value = EPD_ReadVoltage();
    return value >= 1500u && value <= 6000u ? value : 0u;
}

static void update_policy_voltage(bool force) {
    uint16_t sampled;
    if (s_power_samples.operation_active) return;
    if (!force &&
        (uint32_t)(s_now_ms - s_last_policy_sample_ms) <
            HEALTH_SAMPLE_INTERVAL_MS) return;
    sampled = sample_voltage();
    s_last_policy_sample_ms = s_now_ms;
    inktag_power_policy_sample_record(&s_power_samples, sampled,
                                      sampled != 0u);
}

static void update_operation_voltage(bool force) {
    uint16_t sampled;
    if (!s_power_samples.operation_active) return;
    if (!force && s_power_samples.operation_valid &&
        (uint32_t)(s_now_ms - s_last_operation_sample_ms) <
            HEALTH_SAMPLE_INTERVAL_MS) return;
    sampled = sample_voltage();
    s_last_operation_sample_ms = s_now_ms;
    inktag_power_operation_sample_record(&s_power_samples, sampled,
                                         sampled != 0u);
}

void inktag_nrf_health_init(uint32_t reset_reason, uint32_t boot_stage) {
    health_store_t loaded;
    uint16_t length = sizeof(loaded);
    inktag_power_sample_state_reset(&s_power_samples);
    s_last_policy_sample_ms = 0u;
    s_last_operation_sample_ms = 0u;
    s_now_ms = 0u;
    s_store_retry_pending = false;
    s_last_store_attempt_ms = 0u;
    s_initialized = true;
    /* Capture the startup policy sample before Settings initialization can
     * erase/program Flash and before services can start the RGB or EPD rails. */
    update_policy_voltage(true);
    memset(&loaded, 0, sizeof(loaded));
    (void)inktag_nrf_record_store_init();
    if (inktag_nrf_record_store_read(INKTAG_NRF_RECORD_HEALTH, &loaded,
                                     sizeof(loaded), &length) &&
        length == sizeof(loaded) && store_valid(&loaded)) {
        s_store = loaded;
    } else {
        store_defaults();
    }
    s_reset_reason = (uint8_t)reset_reason;
    s_stage = boot_stage;
    s_operation = INKTAG_NRF_HEALTH_OPERATION_BOOT;
    s_watchdog_active = true;
    s_mcu_temperature_valid = false;
    /* No sample survives reboot. Status reads never power the panel. */
    s_panel_temperature_valid = false;
    if ((reset_reason & POWER_RESETREAS_DOG_Msk) != 0u) {
        if (mailbox_valid()) {
            memcpy(&s_pending_crash, (const void *)&s_mailbox.record,
                   sizeof(s_pending_crash));
        } else {
            memset(&s_pending_crash, 0, sizeof(s_pending_crash));
            s_pending_crash.reason = HEALTH_CRASH_WATCHDOG;
            s_pending_crash.operation = INKTAG_NRF_HEALTH_OPERATION_BOOT;
            s_pending_crash.stage = boot_stage;
        }
        s_pending_crash.reset_reason = (uint8_t)reset_reason;
        s_pending_crash_valid = true;
    } else {
        mailbox_clear();
    }
}

void inktag_nrf_health_process(uint32_t now_ms) {
    if (!s_initialized) return;
    s_now_ms = now_ms;
    /* Expire once when this existing main-loop clock advances. Getters then
     * share the validity bit, and an old sample cannot revive after wrap. */
    if (!inktag_epd_temperature_sample_fresh(s_panel_temperature_valid,
            s_panel_temperature_sampled_ms, now_ms,
            PANEL_TEMPERATURE_MAX_AGE_MS)) s_panel_temperature_valid = false;
    if (s_power_samples.operation_active)
        update_operation_voltage(false);
    else
        update_policy_voltage(false);
    if (s_pending_crash_valid && !s_pending_crash_in_store) {
        s_store.records[s_store.next_record] = s_pending_crash;
        s_store.next_record =
            (uint8_t)((s_store.next_record + 1u) % HEALTH_RECORD_COUNT);
        if (s_store.record_count < HEALTH_RECORD_COUNT) s_store.record_count++;
        if (s_store.consecutive_crashes < UINT8_MAX)
            s_store.consecutive_crashes++;
        s_store.total_crashes++;
        s_pending_crash_in_store = true;
        s_store_dirty = true;
    }
    if (!s_stable_marked && now_ms >= HEALTH_STABLE_UPTIME_MS) {
        s_stable_marked = true;
        if (s_store.consecutive_crashes != 0u) {
            s_store.consecutive_crashes = 0u;
            s_store_dirty = true;
        }
    }
    if (s_store_dirty && !s_store_write_pending &&
        inktag_health_store_retry_due(s_store_retry_pending, now_ms,
                                      s_last_store_attempt_ms) &&
        !inktag_nrf_record_store_pending(INKTAG_NRF_RECORD_HEALTH) &&
        inktag_nrf_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, false)) {
        s_store.crc32 = crc32_for(&s_store, offsetof(health_store_t, crc32));
        /* Existing service opportunities only. A rejected journal write may
         * already have mutated Flash; unsigned elapsed time handles wrap. */
        s_last_store_attempt_ms = now_ms;
        s_store_retry_pending = true;
        if (inktag_nrf_record_store_write(INKTAG_NRF_RECORD_HEALTH, &s_store,
                                          sizeof(s_store))) {
            s_store_dirty = false;
            s_store_write_pending = true;
        }
    }
}

void inktag_nrf_health_record_complete(bool success) {
    if (!s_store_write_pending) return;
    s_store_write_pending = false;
    if (!success) {
        s_store_dirty = true;
        return;
    }
    s_store_retry_pending = false;
    if (s_pending_crash_valid && s_pending_crash_in_store) {
        s_pending_crash_valid = false;
        s_pending_crash_in_store = false;
        mailbox_clear();
    }
}

void inktag_nrf_health_watchdog_capture(void) {
    if (!mailbox_valid())
        mailbox_write(HEALTH_CRASH_WATCHDOG, 0u, 0u, SCB->CFSR, SCB->HFSR);
}

void inktag_nrf_health_stage(uint32_t stage,
                             inktag_nrf_health_operation_t operation) {
    s_stage = stage;
    s_operation = operation;
}

uint16_t inktag_nrf_health_voltage_mv(bool force_sample) {
    if (!s_initialized) return 0u;
    if (s_power_samples.operation_active) {
        update_operation_voltage(force_sample);
        return s_power_samples.operation_valid ?
                   s_power_samples.operation_mv : 0u;
    }
    update_policy_voltage(force_sample);
    return s_power_samples.policy_valid ? s_power_samples.policy_mv : 0u;
}

bool inktag_nrf_health_voltage_valid(void) {
    return s_power_samples.operation_active ?
               s_power_samples.operation_valid :
               s_power_samples.policy_valid;
}

void inktag_nrf_health_temperature_sample(void) {
    int32_t quarter_celsius = 0;
    ret_code_t result = sd_temp_get(&quarter_celsius);
    /* The product status contract accepts the qualified operating range used
     * by the other MCU and panel adapters. A failed SoftDevice read or an
     * implausible sample invalidates this source instead of exposing a stale
     * or sentinel value as a measurement. */
    if (result != NRF_SUCCESS || quarter_celsius < -40 * 4 ||
        quarter_celsius > 85 * 4) {
        s_mcu_temperature_valid = false;
        return;
    }
    s_mcu_temperature_sixteenths = (int16_t)(quarter_celsius * 4);
    s_mcu_temperature_valid = true;
}

bool inktag_nrf_health_mcu_temperature(int16_t *sixteenths) {
    if (!s_mcu_temperature_valid) return false;
    if (sixteenths != NULL) *sixteenths = s_mcu_temperature_sixteenths;
    return true;
}

bool inktag_nrf_health_panel_temperature(int16_t *sixteenths) {
    if (!s_panel_temperature_valid) return false;
    if (sixteenths != NULL) *sixteenths = s_panel_temperature_sixteenths;
    return true;
}

void inktag_nrf_health_panel_temperature_sample(const epd_screen_config_t *screen) {
    s_panel_temperature_valid = false;
#if defined(INKTAG_BOARD_PANEL_TEMPERATURE_ENABLED)
    uint8_t raw[2] = {0xFFu, 0xFFu};
    bool read_ok;
    if (screen == NULL) return;
    /* Caller passes the screen already admitted by epd_screen_resolve: the
     * named recipes own fixed geometry/color validation. Select only the
     * corresponding register here, without duplicating that admission gate.
     * Read the TSC register populated by the existing full refresh; no new
     * B1/activation, waveform, reset or rail lifetime is introduced here. */
    if (screen->driver_id == EPD_SCREEN_DRIVER_SSD1619 &&
        screen->panel_recipe == INKTAG_RECIPE_SSD1619_HEMA_213_BWR) {
        read_ok = EPD_ReadCommand(SSD1xxx_TSENSOR_READ, raw, sizeof(raw));
        s_panel_temperature_valid = inktag_ssd1680_r1b_temperature_sixteenths(
            read_ok, raw, &s_panel_temperature_sixteenths);
    } else if (screen->driver_id == EPD_SCREEN_DRIVER_JD79665AA &&
        (screen->panel_recipe == INKTAG_RECIPE_JD79665AA_HUAWEI_398_A0 ||
         screen->panel_recipe == INKTAG_RECIPE_JD79665AA_HUAWEI_398_A1)) {
        read_ok = EPD_ReadCommand(0x40u, raw, sizeof(raw)); /* JD79665AA R40 */
        s_panel_temperature_valid = inktag_jd_r40_temperature_sixteenths(
            read_ok, raw, &s_panel_temperature_sixteenths);
    }
    s_panel_temperature_sampled_ms = s_now_ms;
#else
    (void)screen;
#endif
}

bool inktag_nrf_health_effective_temperature(int8_t *celsius) {
    return inktag_temperature_select_lower(
        s_mcu_temperature_valid, s_mcu_temperature_sixteenths,
        inktag_nrf_health_panel_temperature(NULL),
        s_panel_temperature_sixteenths, celsius);
}

bool EPD_ReadEffectiveTemperature(int8_t *celsius) {
    inktag_nrf_health_temperature_sample();
    return inktag_nrf_health_effective_temperature(celsius);
}

inktag_power_state_t inktag_nrf_health_power_state(void) {
    return inktag_power_classify(s_power_samples.policy_mv,
                                 s_power_samples.policy_valid,
                                 &s_thresholds);
}

bool inktag_nrf_health_operation_allowed(inktag_power_operation_t operation,
                                         bool force_sample) {
    /* The settings journal can legitimately need its first page before the
     * health record has been loaded. Sample directly in that narrow boot
     * window so fail-safe Flash gating does not depend on initialization
     * order. */
    if (!s_initialized) {
        uint16_t voltage_mv = sample_voltage();
        return inktag_power_operation_allowed(
            operation, voltage_mv, voltage_mv != 0u, &s_thresholds);
    }
    if (s_power_samples.operation_active) {
        update_operation_voltage(force_sample);
        return inktag_power_operation_allowed(
            operation, s_power_samples.operation_mv,
            s_power_samples.operation_valid, &s_thresholds);
    }
    update_policy_voltage(force_sample);
    /* Unavailable ADC remains UNKNOWN without a software voltage veto.
     * Qualification is evidence-only; valid-voltage thresholds stay intact. */
    return inktag_power_operation_allowed(operation,
                                           s_power_samples.policy_mv,
                                           s_power_samples.policy_valid,
                                           &s_thresholds);
}

void inktag_nrf_health_display_begin(void) {
    s_panel_temperature_valid = false;
    inktag_power_operation_sample_begin(&s_power_samples);
    s_last_operation_sample_ms = s_now_ms;
}

void inktag_nrf_health_display_end(void) {
    inktag_power_operation_sample_end(&s_power_samples);
}

static void put_u16(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

uint16_t inktag_nrf_health_encode(uint8_t record_index, uint8_t *output,
                                  uint16_t output_size) {
    bool display_allowed;
    bool flash_allowed;
    bool ota_allowed;
    uint8_t flags;
    if (!output || output_size < INKTAG_NRF_HEALTH_WIRE_SIZE) return 0u;
    memset(output, 0, INKTAG_NRF_HEALTH_WIRE_SIZE);
    update_policy_voltage(false);
    display_allowed = inktag_power_operation_allowed(
        INKTAG_POWER_OPERATION_DISPLAY, s_power_samples.policy_mv,
        s_power_samples.policy_valid, &s_thresholds);
    flash_allowed = inktag_power_operation_allowed(
        INKTAG_POWER_OPERATION_FLASH, s_power_samples.policy_mv,
        s_power_samples.policy_valid, &s_thresholds);
    ota_allowed = inktag_power_operation_allowed(
        INKTAG_POWER_OPERATION_OTA, s_power_samples.policy_mv,
        s_power_samples.policy_valid, &s_thresholds);
    flags = (s_power_samples.policy_valid ? 0x01u : 0u) |
            (display_allowed ? 0x02u : 0u) |
            (flash_allowed ? 0x04u : 0u) |
            (ota_allowed ? 0x08u : 0u) |
            (s_watchdog_active ? 0x10u : 0u) |
            (s_store.record_count ? 0x20u : 0u) |
            (INKTAG_BOARD_POWER_THRESHOLDS_QUALIFIED ? 0u : 0x80u);
    output[0] = INKTAG_NRF_HEALTH_WIRE_VERSION;
    output[1] = (uint8_t)inktag_nrf_health_power_state();
    output[2] = flags;
    output[3] = s_reset_reason;
    put_u16(&output[4], s_power_samples.policy_mv);
    put_u16(&output[6], s_thresholds.warning_mv);
    put_u16(&output[8], s_thresholds.display_min_mv);
    put_u16(&output[10], s_thresholds.flash_min_mv);
    put_u16(&output[12], s_thresholds.ota_min_mv);
    put_u16(&output[14], s_thresholds.recovery_hysteresis_mv);
    put_u32(&output[16], s_store.total_crashes);
    output[20] = s_store.record_count;
    output[21] = record_index;
    output[22] = s_store.consecutive_crashes;
    if (record_index < s_store.record_count) {
        uint8_t slot = (uint8_t)((s_store.next_record + HEALTH_RECORD_COUNT -
                                  1u - record_index) % HEALTH_RECORD_COUNT);
        const health_crash_record_t *record = &s_store.records[slot];
        output[23] = record->reason;
        output[24] = record->reset_reason;
        output[25] = record->operation;
        put_u16(&output[28], record->voltage_mv);
        put_u32(&output[32], record->uptime_ms);
        put_u32(&output[36], record->stage);
        put_u32(&output[40], record->pc);
        put_u32(&output[44], record->lr);
        put_u32(&output[48], record->cfsr);
        put_u32(&output[52], record->hfsr);
    }
    return INKTAG_NRF_HEALTH_WIRE_SIZE;
}

/* The naked handler reaches this symbol only through inline assembly, so LTO
 * cannot discover the edge on its own. Keep a public Thumb entry point. */
__attribute__((used, externally_visible, noinline))
void inktag_nrf_health_fault_capture(uint32_t sp) {
    const uint32_t *stack = (const uint32_t *)sp;
    uint32_t pc = 0u;
    uint32_t lr = 0u;
    if (sp >= INKTAG_BOARD_RAM_START &&
        sp <= INKTAG_BOARD_RAM_START + INKTAG_BOARD_RAM_SIZE - 32u) {
        lr = stack[5];
        pc = stack[6];
    }
    mailbox_write(HEALTH_CRASH_HARDFAULT, pc, lr, SCB->CFSR, SCB->HFSR);
    for (;;) { }
}

__attribute__((naked)) void HardFault_Handler(void) {
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "b inktag_nrf_health_fault_capture\n");
}
