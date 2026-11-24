#include "inktag_gr_health.h"

#include <stddef.h>
#include <string.h>

#include "app_aon_wdt.h"
#include "gr_soc.h"
#include "inktag_board.h"
#if INKTAG_BUILD_PRO
#include "inktag_gr_advertising_pro.h"
#endif
#if defined(INKTAG_GOODIX_GR5515)
#include "gr55xx.h"
#include "gr55xx_hal.h"
#include "gr55xx_sys.h"
#else
#include "gr533x.h"
#include "gr533x_hal.h"
#endif
#include "inktag_gr_settings_store.h"
#include "inktag_health_store_retry.h"
#include "inktag_gr_adc_poll.inc"

#define HEALTH_STORE_MAGIC 0x31484B49u /* IKH1 */
#define HEALTH_STORE_VERSION 1u
#define HEALTH_MAILBOX_MAGIC 0x48534C54u /* HSLT */
#define HEALTH_RECORD_COUNT 4u
#define HEALTH_SAMPLE_INTERVAL_MS 5000u
#define HEALTH_DISPLAY_SAMPLE_INTERVAL_MS 500u
#define HEALTH_STABLE_UPTIME_MS 60000u

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

/* Goodix excludes RAM_RESERVE from normal C-runtime clearing. The checksum
 * prevents random retained SRAM after POR from being accepted as evidence. */
static volatile health_mailbox_t s_mailbox
    __attribute__((section(".inktag_noinit"), used));
static health_store_t s_store;
static health_crash_record_t s_pending_crash;
static bool s_pending_crash_valid;
static bool s_pending_crash_staged;
static bool s_store_dirty;
static bool s_store_retry_pending;
static bool s_initialized;
static bool s_watchdog_active;
static bool s_stable_marked;
static bool s_warning_page_attempted;
static uint8_t s_reset_reason;
static inktag_power_sample_state_t s_power_samples;
static uint32_t s_now_ms;
static uint32_t s_last_store_attempt_ms;
static uint32_t s_last_policy_sample_ms;
static uint32_t s_last_display_sample_ms;
static uint32_t s_stage;
static inktag_gr_health_operation_t s_operation = INKTAG_GR_HEALTH_OPERATION_BOOT;
static app_aon_wdt_params_t s_watchdog;

static uint32_t crc32_for(const void *value, uint32_t length) {
    const uint8_t *data = (const uint8_t *)value;
    uint32_t crc = 0xFFFFFFFFu;
    while (length-- > 0u) {
        crc ^= *data++;
        for (uint8_t bit = 0u; bit < 8u; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
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
    bool use_operation_min =
        s_operation == INKTAG_GR_HEALTH_OPERATION_DISPLAY &&
        s_power_samples.operation_min_valid;
    health_crash_record_t record = {
        .reason = (uint8_t)reason,
        .reset_reason = s_reset_reason,
        .operation = (uint8_t)s_operation,
        .voltage_mv = use_operation_min ?
                          s_power_samples.operation_min_mv :
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

static bool store_write(void) {
    s_last_store_attempt_ms = s_now_ms;
    s_store_retry_pending = true;
    /* A fresh sample can clear warning_page_shown in this same store.
     * Check admission first, then checksum the final tuple, even on denial. */
    bool power_allowed = inktag_gr_health_operation_allowed(
        INKTAG_POWER_OPERATION_FLASH, true);
    s_store.crc32 = crc32_for(&s_store, offsetof(health_store_t, crc32));
    if (!power_allowed) return false;
    if (!inktag_gr_settings_put(INKTAG_GR_SETTINGS_SLOT_HEALTH,
                                sizeof(s_store),
                                (const uint8_t *)&s_store)) return false;
    s_store_retry_pending = false;
    s_store_dirty = false;
    return true;
}

static uint16_t sample_voltage(void) {
    adc_handle_t adc = {0};
    uint16_t samples[16] = {0};
    uint32_t sum = 0u;
    uint16_t result = 0u;
    adc.init.channel_n = ADC_INPUT_SRC_BAT;
    adc.init.channel_p = ADC_INPUT_SRC_BAT;
    adc.init.input_mode = ADC_INPUT_SINGLE;
    adc.init.ref_source = ADC_REF_SRC_BUF_INT;
    adc.init.ref_value = ADC_REF_VALUE_0P8;
#if defined(INKTAG_GOODIX_GR5515)
    adc.init.clock = ADC_CLK_1P6M;
#else
    adc.init.clock = ADC_CLK_1M;
#endif
    if (hal_adc_init(&adc) != HAL_OK) return 0u;
#if defined(INKTAG_GOODIX_GR5515)
    SET_BITS(AON->SNSADC_CFG, AON_SNSADC_CFG_VBAT_EN_Msk);
#else
    SET_BITS(AON_PMU->SNSADC_CFG, AON_PMU_SNSADC_CFG_VBAT_EN_Msk);
#endif
    hal_status_t conversion = inktag_gr_adc_sample16(&adc, samples);
#if defined(INKTAG_GOODIX_GR5515)
    CLEAR_BITS(AON->SNSADC_CFG, AON_SNSADC_CFG_VBAT_EN_Msk);
#else
    CLEAR_BITS(AON_PMU->SNSADC_CFG, AON_PMU_SNSADC_CFG_VBAT_EN_Msk);
#endif
    if (conversion == HAL_OK) {
        for (uint8_t i = 8u; i < 16u; i++) sum += samples[i];
        uint16_t average = (uint16_t)(sum / 8u);
#if defined(INKTAG_GOODIX_GR5515)
        adc_trim_info_t trim = {0};
        int32_t offset = 8362;
        int32_t slope = 4754;
        if (sys_adc_trim_get(&trim) == SDK_SUCCESS && trim.slope_int_0p8 > 0) {
            offset = trim.offset_int_0p8;
            slope = trim.slope_int_0p8;
        }
        int32_t numerator = (offset - (int32_t)average) * 27000;
        int32_t denominator = slope * 7;
        if (numerator > 0 && denominator > 0) {
            result = (uint16_t)((numerator + denominator / 2) / denominator);
        }
#else
        double converted = 0.0;
        hal_adc_vbat_conv(&adc, &average, &converted, 1u);
        if (converted > 0.0 && converted < 6.0) {
            result = (uint16_t)(converted * 1000.0 + 0.5);
        }
#endif
    }
    hal_adc_deinit(&adc);
    return result >= 1500u && result <= 6000u ? result : 0u;
}

static void update_display_voltage(bool force) {
    if (!s_power_samples.operation_active) return;
    if (!force &&
        (uint32_t)(s_now_ms - s_last_display_sample_ms) <
            HEALTH_DISPLAY_SAMPLE_INTERVAL_MS) return;
    uint16_t sampled = sample_voltage();
    s_last_display_sample_ms = s_now_ms;
    inktag_power_operation_sample_record(&s_power_samples, sampled,
                                         sampled != 0u);
}

static void update_policy_voltage(bool force) {
    /* Cache the attempt, not only a valid conversion.  A disconnected or
     * settling ADC stays UNKNOWN without turning the otherwise-idle main
     * loop into an unbounded retry/power drain. Transaction entry points
     * still pass force=true and therefore always take a fresh sample. */
    if (s_power_samples.operation_active) return;
    if (!force &&
        (uint32_t)(s_now_ms - s_last_policy_sample_ms) <
            HEALTH_SAMPLE_INTERVAL_MS) return;
    uint16_t sampled = sample_voltage();
    s_last_policy_sample_ms = s_now_ms;
    inktag_power_policy_sample_record(&s_power_samples, sampled,
                                      sampled != 0u);
    if (s_power_samples.policy_valid &&
        inktag_power_warning_cleared(s_power_samples.policy_mv, true,
                                     &s_thresholds)) {
        s_warning_page_attempted = false;
        if (s_store.warning_page_shown) {
            s_store.warning_page_shown = 0u;
            s_store_dirty = true;
        }
    }
}

bool inktag_gr_flash_mutation_allowed(uint32_t address, uint32_t size) {
    (void)address;
    (void)size;
    /* NVDS may erase or compact during platform_init(), before health_init().
     * Sample directly here so valid low voltage also protects startup writes;
     * unavailable ADC remains UNKNOWN and does not veto normal boot. */
    if (s_power_samples.operation_active) {
        update_display_voltage(true);
        return inktag_power_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH,
            s_power_samples.operation_mv,
            s_power_samples.operation_valid,
            &s_thresholds);
    }
    update_policy_voltage(true);
    return inktag_power_operation_allowed(INKTAG_POWER_OPERATION_FLASH,
                                           s_power_samples.policy_mv,
                                           s_power_samples.policy_valid,
                                           &s_thresholds);
}

static void watchdog_alarm(void) {
    mailbox_write(HEALTH_CRASH_WATCHDOG, 0u, 0u, SCB->CFSR, SCB->HFSR);
}

void inktag_gr_health_init(uint8_t reset_reason, uint32_t boot_stage) {
    uint16_t length = sizeof(s_store);
    health_store_t loaded;
    memset(&loaded, 0, sizeof(loaded));
    if (inktag_gr_settings_get(INKTAG_GR_SETTINGS_SLOT_HEALTH, &length,
                               (uint8_t *)&loaded) ==
            INKTAG_GR_SETTINGS_READ_OK &&
        length == sizeof(loaded) && store_valid(&loaded)) {
        s_store = loaded;
    } else {
        store_defaults();
    }
    s_reset_reason = reset_reason;
    s_stage = boot_stage;
    s_operation = INKTAG_GR_HEALTH_OPERATION_BOOT;
    s_pending_crash_staged = false;
    s_store_retry_pending = false;
    s_last_store_attempt_ms = 0u;
    inktag_power_sample_state_reset(&s_power_samples);
    s_last_policy_sample_ms = 0u;
    s_last_display_sample_ms = 0u;
    s_initialized = true;
    /* This startup policy sample is taken before screen/profile loading or
     * any EPD GPIO/high-voltage activity. */
    update_policy_voltage(true);

    if ((reset_reason & SYS_RESET_REASON_AONWDT) != 0u) {
        if (mailbox_valid()) {
            memcpy(&s_pending_crash, (const void *)&s_mailbox.record,
                   sizeof(s_pending_crash));
        } else {
            /* The terminal watchdog reset is hardware-driven and can still
             * occur when interrupts were masked, so retain a useful reset
             * record even when its alarm callback could not fill the mailbox. */
            memset(&s_pending_crash, 0, sizeof(s_pending_crash));
            s_pending_crash.reason = HEALTH_CRASH_WATCHDOG;
            s_pending_crash.operation = INKTAG_GR_HEALTH_OPERATION_BOOT;
            s_pending_crash.stage = boot_stage;
        }
        s_pending_crash.reset_reason = reset_reason;
        s_pending_crash_valid = true;
    } else {
        mailbox_clear();
    }

    memset(&s_watchdog, 0, sizeof(s_watchdog));
#if defined(INKTAG_GOODIX_GR5515)
    s_watchdog.init.counter = 32768u * 8u - 1u;
    s_watchdog.init.alarm_counter = 0x1Fu;
#else
    s_watchdog.init.counter = 8000u;
    s_watchdog.init.alarm_counter = 1000u;
#endif
    s_watchdog_active = app_aon_wdt_init(&s_watchdog, watchdog_alarm) == APP_DRV_SUCCESS;
}

void inktag_gr_health_process(uint32_t now_ms) {
    if (!s_initialized) return;
    s_now_ms = now_ms;
    update_policy_voltage(false);
    if (s_pending_crash_valid &&
        inktag_health_store_retry_due(s_store_retry_pending, s_now_ms,
                                      s_last_store_attempt_ms) &&
        inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, false)) {
        /* Stage once: a failed commit must not overwrite the old ring or
         * count this same retained crash again on the next service turn. */
        if (!s_pending_crash_staged) {
            s_store.records[s_store.next_record] = s_pending_crash;
            s_store.next_record = (uint8_t)((s_store.next_record + 1u) % HEALTH_RECORD_COUNT);
            if (s_store.record_count < HEALTH_RECORD_COUNT) s_store.record_count++;
            if (s_store.consecutive_crashes < UINT8_MAX) s_store.consecutive_crashes++;
            s_store.total_crashes++;
            s_store_dirty = true;
            s_pending_crash_staged = true;
        }
        if (store_write()) {
            s_pending_crash_valid = false;
            s_pending_crash_staged = false;
            mailbox_clear();
        }
    }
    if (!s_stable_marked && !s_pending_crash_valid &&
        now_ms >= HEALTH_STABLE_UPTIME_MS) {
        s_stable_marked = true;
        if (s_store.consecutive_crashes != 0u) {
            s_store.consecutive_crashes = 0u;
            s_store_dirty = true;
        }
    }
    if (s_store_dirty && !s_pending_crash_valid &&
        inktag_health_store_retry_due(s_store_retry_pending, s_now_ms,
                                      s_last_store_attempt_ms) &&
        inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, false)) {
        (void)store_write();
    }
}

#if INKTAG_GR_MINUTE_IDLE_ENABLED
#include "inktag_gr_idle_watchdog_pro.inc"
#else
void inktag_gr_health_watchdog_feed(void) {
    if (s_watchdog_active) (void)app_aon_wdt_refresh();
}
#endif

void inktag_gr_health_stage(uint32_t stage, inktag_gr_health_operation_t operation) {
    s_stage = stage;
    s_operation = operation;
}

uint16_t inktag_gr_health_voltage_mv(bool force_sample) {
    if (!s_initialized) return 0u;
    if (s_power_samples.operation_active) {
        update_display_voltage(force_sample);
        return s_power_samples.operation_valid ?
                   s_power_samples.operation_mv : 0u;
    }
    update_policy_voltage(force_sample);
    return s_power_samples.policy_valid ? s_power_samples.policy_mv : 0u;
}

bool inktag_gr_health_voltage_valid(void) {
    return s_power_samples.operation_active ?
               s_power_samples.operation_valid :
               s_power_samples.policy_valid;
}

inktag_power_state_t inktag_gr_health_power_state(void) {
    return inktag_power_classify(s_power_samples.policy_mv,
                                 s_power_samples.policy_valid,
                                 &s_thresholds);
}

bool inktag_gr_health_operation_allowed(inktag_power_operation_t operation,
                                        bool force_sample) {
    if (!s_initialized) return false;
    if (s_power_samples.operation_active) {
        update_display_voltage(force_sample);
        return inktag_power_operation_allowed(
            operation,
            s_power_samples.operation_mv,
            s_power_samples.operation_valid,
            &s_thresholds);
    }
    update_policy_voltage(force_sample);
    return inktag_power_operation_allowed(operation,
                                           s_power_samples.policy_mv,
                                           s_power_samples.policy_valid,
                                           &s_thresholds);
}

void inktag_gr_health_display_begin(void) {
    inktag_power_operation_sample_begin(&s_power_samples);
    s_last_display_sample_ms = s_now_ms;
}

void inktag_gr_health_display_end(void) {
    inktag_power_operation_sample_end(&s_power_samples);
}

bool inktag_gr_health_display_continue_allowed(uint32_t now_ms) {
    if (!s_initialized) return false;
    s_now_ms = now_ms;
    /* EPD-load samples are safety/sag telemetry only.  In particular, a
     * transient below the warning threshold must not overwrite the stable
     * policy sample and schedule a low-battery page after this refresh. */
    update_display_voltage(false);
    return inktag_power_operation_allowed(INKTAG_POWER_OPERATION_DISPLAY,
                                           s_power_samples.operation_mv,
                                           s_power_samples.operation_valid,
                                           &s_thresholds);
}

bool inktag_gr_health_warning_page_pending(void) {
    inktag_power_state_t state = inktag_gr_health_power_state();
    return s_initialized && !s_warning_page_attempted &&
           !s_store.warning_page_shown &&
           (state == INKTAG_POWER_WARNING || state == INKTAG_POWER_CRITICAL) &&
           inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_DISPLAY, false);
}

void inktag_gr_health_warning_page_attempted(void) {
    s_warning_page_attempted = true;
}

void inktag_gr_health_warning_page_shown(void) {
    if (!s_initialized) return;
    s_warning_page_attempted = true;
    if (s_store.warning_page_shown) return;
    s_store.warning_page_shown = 1u;
    s_store_dirty = true;
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

uint16_t inktag_gr_health_encode(uint8_t record_index, uint8_t *output,
                                 uint16_t output_size) {
    if (!output || output_size < INKTAG_GR_HEALTH_WIRE_SIZE) return 0u;
    memset(output, 0, INKTAG_GR_HEALTH_WIRE_SIZE);
    update_policy_voltage(false);
    bool display_allowed = inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_DISPLAY, false);
    bool flash_allowed = inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, false);
    bool ota_allowed = inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_OTA, false);
    uint8_t flags = (s_power_samples.policy_valid ? 0x01u : 0u) |
                    (display_allowed ? 0x02u : 0u) |
                    (flash_allowed ? 0x04u : 0u) |
                    (ota_allowed ? 0x08u : 0u) |
                    (s_watchdog_active ? 0x10u : 0u) |
                    (s_store.record_count ? 0x20u : 0u) |
                    (s_store.warning_page_shown ? 0x40u : 0u) |
                    (INKTAG_BOARD_POWER_THRESHOLDS_QUALIFIED ? 0u : 0x80u);
    output[0] = INKTAG_GR_HEALTH_WIRE_VERSION;
    output[1] = (uint8_t)inktag_gr_health_power_state();
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
        uint8_t slot = (uint8_t)((s_store.next_record + HEALTH_RECORD_COUNT - 1u -
                                  record_index) % HEALTH_RECORD_COUNT);
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
    return INKTAG_GR_HEALTH_WIRE_SIZE;
}

#if defined(INKTAG_GOODIX_GR5515)
void inktag_gr_health_fault_capture(uint32_t sp) {
    const uint32_t *stack = (const uint32_t *)sp;
    uint32_t pc = 0u;
    uint32_t lr = 0u;
    if (sp >= 0x30000000u && sp <= 0x3001FFE0u) {
        lr = stack[5];
        pc = stack[6];
    }
    mailbox_write(HEALTH_CRASH_HARDFAULT, pc, lr, SCB->CFSR, SCB->HFSR);
    for (;;) { }
}
#else
void inktag_gr_health_fault_capture(uint32_t raw_stack) {
    unsigned int *stack = (unsigned int *)raw_stack;
    uint32_t sp = (uint32_t)stack;
    uint32_t pc = 0u;
    uint32_t lr = 0u;
    /* GR5331 owns 96 KiB SRAM at 0x20000000; unlike GR551x it is not
     * mapped into the 0x30000000 secure SRAM window. Keep enough room for
     * the eight-word Cortex-M exception frame before dereferencing it. */
    if (sp >= 0x20000000u && sp <= 0x20017FE0u) {
        lr = stack[5];
        pc = stack[6];
    }
    mailbox_write(HEALTH_CRASH_HARDFAULT, pc, lr, SCB->CFSR, SCB->HFSR);
    for (;;) { }
}
#endif
