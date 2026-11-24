#include "inktag_om_health.h"

#include <stddef.h>
#include <string.h>

#include "nvds.h"
#include "om.h"
#include "om_driver.h"
#include "inktag_board.h"
#include "inktag_om_nvds.h"
#include "inktag_om_voltage.h"
#include "inktag_protocol_core.h"

#define HEALTH_NVDS_TAG ((nvds_tag_id_t)(NVDS_TAG_USER_BEGIN + 12u))
#define HEALTH_STORE_MAGIC 0x31484B49u /* IKH1 */
#define HEALTH_STORE_VERSION 1u
#define HEALTH_MAILBOX_MAGIC 0x48534C54u /* HSLT */
#define HEALTH_RECORD_COUNT 4u
#define HEALTH_SAMPLE_INTERVAL_MS 5000u
#define HEALTH_DISPLAY_SAMPLE_INTERVAL_MS 500u
#define HEALTH_STABLE_UPTIME_MS 60000u
#define HEALTH_STORE_RETRY_INTERVAL_MS 60000u

typedef enum {
    HEALTH_CRASH_NONE = 0,
    HEALTH_CRASH_WATCHDOG = 1,
    HEALTH_CRASH_HARDFAULT = 2,
    HEALTH_CRASH_LOW_VOLTAGE_RESET = 3,
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
    uint8_t warning_page_shown; /* Historical byte; keep record layout/CRC. */
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
static bool s_pending_crash_staged;
static bool s_store_dirty;
static bool s_store_retry_pending;
static bool s_initialized;
static bool s_watchdog_active;
static bool s_stable_marked;
static uint8_t s_reset_reason;
static inktag_power_sample_state_t s_power_samples;
static uint32_t s_now_ms;
static uint32_t s_last_store_attempt_ms;
static uint32_t s_last_policy_sample_ms;
static uint32_t s_last_display_sample_ms;
static uint32_t s_stage;
static inktag_om_health_operation_t s_operation = INKTAG_OM_HEALTH_OPERATION_BOOT;

static uint32_t crc32_for(const void *value, uint32_t length) {
    return inktag_crc32_update(
               UINT32_C(0xffffffff), value, (uint16_t)length) ^
           UINT32_C(0xffffffff);
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
        s_operation == INKTAG_OM_HEALTH_OPERATION_DISPLAY &&
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

static bool store_retry_due(void) {
    /* Existing main-loop opportunities only; no timer or extra wakeup. */
    return !s_store_retry_pending ||
           (uint32_t)(s_now_ms - s_last_store_attempt_ms) >=
               HEALTH_STORE_RETRY_INTERVAL_MS;
}

static bool store_write(void) {
    uint8_t result = NVDS_FAIL;
    s_last_store_attempt_ms = s_now_ms;
    /* Fresh health admission can clear warning_page_shown in this very record.
     * Keep the ordinary Flash gate, then finalize CRC immediately before the
     * same vendor write. The generic wrapper would sample/mutate it again. */
    if (inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true)) {
        s_store.crc32 = crc32_for(&s_store, offsetof(health_store_t, crc32));
        result = nvds_put(HEALTH_NVDS_TAG, sizeof(s_store), &s_store);
    }
    if (result != NVDS_OK) {
        s_store_retry_pending = true;
        return false;
    }
    s_store_retry_pending = false;
    s_store_dirty = false;
    return true;
}

static uint16_t sample_voltage(void) {
    uint16_t value = inktag_om_voltage_sample_mv();
    return value >= 1500u && value <= 6000u ? value : 0u;
}

static void update_display_voltage(bool force) {
    if (!s_power_samples.operation_active) return;
    /* UNKNOWN is cached too; a failed ADC attempt must not become a retry
     * loop in display maintenance. Actual writes still force fresh samples. */
    if (!force &&
        (uint32_t)(s_now_ms - s_last_display_sample_ms) <
            HEALTH_DISPLAY_SAMPLE_INTERVAL_MS) return;
    uint16_t sampled = sample_voltage();
    s_last_display_sample_ms = s_now_ms;
    inktag_power_operation_sample_record(&s_power_samples, sampled,
                                         sampled != 0u);
}

static void update_policy_voltage(bool force) {
    /* Record only idle/entry samples as policy. Display-load samples stay in
     * the operation channel, separate from idle policy and legacy warning-bit
     * recovery. Cache failed attempts to avoid an idle retry loop. */
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
        if (s_store.warning_page_shown) {
            s_store.warning_page_shown = 0u;
            s_store_dirty = true;
        }
    }
}

void inktag_om_health_watchdog_alarm(void *om_reg, drv_event_t event,
                                   void *param0, void *param1) {
    (void)om_reg;
    (void)event;
    (void)param0;
    (void)param1;
    /* Registration enables the existing SDK interrupt. The strong wrapper
     * saves its interrupted frame before SDK service; do not overwrite it. */
}

static bool watchdog_reset_has_context(uint8_t reset_reason) {
    /* CPU-only reset preserves the first-bank frame. Cold boot, ordinary
     * reset and a stale/corrupt mailbox must never replay it as a new WDT. */
    return reset_reason == (uint8_t)PMU_REBOOT_FROM_WDT ||
           ((OM_PMU->SW_STATUS & PMU_SW_STATUS_REBOOT_FROM_SOFT_RESET_MASK) &&
            mailbox_valid() &&
            s_mailbox.record.reason == HEALTH_CRASH_WATCHDOG);
}

void inktag_om_health_init(uint8_t reset_reason, uint32_t boot_stage) {
    health_store_t loaded;
    nvds_tag_len_t length = sizeof(loaded);
    memset(&loaded, 0, sizeof(loaded));
    if (nvds_get(HEALTH_NVDS_TAG, &length, &loaded) == NVDS_OK &&
        length == sizeof(loaded) && store_valid(&loaded)) {
        s_store = loaded;
    } else {
        store_defaults();
    }
    s_reset_reason = reset_reason;
    s_stage = boot_stage;
    s_operation = INKTAG_OM_HEALTH_OPERATION_BOOT;
    inktag_power_sample_state_reset(&s_power_samples);
    s_last_policy_sample_ms = 0u;
    s_last_display_sample_ms = 0u;
    s_last_store_attempt_ms = 0u;
    s_store_retry_pending = false;
    s_pending_crash_staged = false;
    s_initialized = true;
    /* The caller invokes health_init before status LED setup, EPD/external
     * Flash power or any project-owned setting migration. */
    update_policy_voltage(true);
    if (watchdog_reset_has_context(reset_reason)) {
        if (mailbox_valid()) {
            memcpy(&s_pending_crash, (const void *)&s_mailbox.record,
                   sizeof(s_pending_crash));
        } else {
            memset(&s_pending_crash, 0, sizeof(s_pending_crash));
            s_pending_crash.reason = HEALTH_CRASH_WATCHDOG;
            s_pending_crash.operation = INKTAG_OM_HEALTH_OPERATION_BOOT;
            s_pending_crash.stage = boot_stage;
        }
        s_pending_crash.reset_reason = reset_reason;
        s_pending_crash_valid = true;
    } else if (reset_reason == (uint8_t)PMU_REBOOT_FROM_IFLASH_LOW_V) {
        /* The low-voltage reset has no fault ISR in which to fill the retained
         * mailbox.  Preserve the reset reason and the first valid post-boot
         * voltage instead, then defer the NVDS append until Flash is safe. */
        mailbox_clear();
        memset(&s_pending_crash, 0, sizeof(s_pending_crash));
        s_pending_crash.reason = HEALTH_CRASH_LOW_VOLTAGE_RESET;
        s_pending_crash.reset_reason = reset_reason;
        s_pending_crash.operation = INKTAG_OM_HEALTH_OPERATION_BOOT;
        s_pending_crash.voltage_mv = s_power_samples.policy_valid ?
                                         s_power_samples.policy_mv : 0u;
        s_pending_crash.stage = boot_stage;
        s_pending_crash_valid = true;
    } else {
        mailbox_clear();
    }
    s_watchdog_active = true;
}

void inktag_om_health_process(uint32_t now_ms) {
    if (!s_initialized) return;
    s_now_ms = now_ms;
    update_policy_voltage(false);
    if (s_pending_crash_valid && store_retry_due() &&
        inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, false)) {
        /* Stage a pending crash exactly once. NVDS may reject a write while a
         * marginal rail is recovering; subsequent process turns must retry
         * the identical store bytes instead of appending the same reset and
         * incrementing total_crashes again. */
        if (!s_pending_crash_staged) {
            s_store.records[s_store.next_record] = s_pending_crash;
            s_store.next_record = (uint8_t)((s_store.next_record + 1u) %
                                            HEALTH_RECORD_COUNT);
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
    if (s_store_dirty && !s_pending_crash_valid && store_retry_due() &&
        inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, false)) {
        (void)store_write();
    }
}

void inktag_om_health_watchdog_feed(void) {
    if (s_watchdog_active) drv_wdt_keep_alive();
}

void inktag_om_health_stage(uint32_t stage,
                            inktag_om_health_operation_t operation) {
    s_stage = stage;
    s_operation = operation;
}

uint16_t inktag_om_health_voltage_mv(bool force_sample) {
    if (!s_initialized) return 0u;
    if (s_power_samples.operation_active) {
        update_display_voltage(force_sample);
        return s_power_samples.operation_valid ?
                   s_power_samples.operation_mv : 0u;
    }
    update_policy_voltage(force_sample);
    return s_power_samples.policy_valid ? s_power_samples.policy_mv : 0u;
}

bool inktag_om_health_voltage_valid(void) {
    return s_power_samples.operation_active ?
               s_power_samples.operation_valid :
               s_power_samples.policy_valid;
}

inktag_power_state_t inktag_om_health_power_state(void) {
    return inktag_power_classify(s_power_samples.policy_mv,
                                 s_power_samples.policy_valid,
                                 &s_thresholds);
}

bool inktag_om_health_operation_allowed(inktag_power_operation_t operation,
                                        bool force_sample) {
    if (!s_initialized) return false;
    if (s_power_samples.operation_active) {
        update_display_voltage(force_sample);
        return inktag_power_operation_allowed(
            operation, s_power_samples.operation_mv,
            s_power_samples.operation_valid, &s_thresholds);
    }
    update_policy_voltage(force_sample);
    return inktag_power_operation_allowed(operation,
                                           s_power_samples.policy_mv,
                                           s_power_samples.policy_valid,
                                           &s_thresholds);
}

void inktag_om_health_display_begin(void) {
    inktag_power_operation_sample_begin(&s_power_samples);
    s_last_display_sample_ms = s_now_ms;
}

void inktag_om_health_display_end(void) {
    inktag_power_operation_sample_end(&s_power_samples);
}

bool inktag_om_health_display_continue_allowed(uint32_t now_ms) {
    if (!s_initialized) return false;
    s_now_ms = now_ms;
    update_display_voltage(false);
    return inktag_power_operation_allowed(INKTAG_POWER_OPERATION_DISPLAY,
                                           s_power_samples.operation_mv,
                                           s_power_samples.operation_valid,
                                           &s_thresholds);
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

uint16_t inktag_om_health_encode(uint8_t record_index, uint8_t *output,
                                 uint16_t output_size) {
    if (!output || output_size < INKTAG_OM_HEALTH_WIRE_SIZE) return 0u;
    memset(output, 0, INKTAG_OM_HEALTH_WIRE_SIZE);
    update_policy_voltage(false);
    bool display_allowed = inktag_om_health_operation_allowed(
        INKTAG_POWER_OPERATION_DISPLAY, false);
    bool flash_allowed = inktag_om_health_operation_allowed(
        INKTAG_POWER_OPERATION_FLASH, false);
    bool ota_allowed = inktag_om_health_operation_allowed(
        INKTAG_POWER_OPERATION_OTA, false);
    uint8_t flags = (s_power_samples.policy_valid ? 0x01u : 0u) |
                    (display_allowed ? 0x02u : 0u) |
                    (flash_allowed ? 0x04u : 0u) |
                    (ota_allowed ? 0x08u : 0u) |
                    (s_watchdog_active ? 0x10u : 0u) |
                    (s_store.record_count ? 0x20u : 0u) |
                    (s_store.warning_page_shown ? 0x40u : 0u) |
                    (INKTAG_BOARD_POWER_THRESHOLDS_QUALIFIED ? 0u : 0x80u);
    output[0] = INKTAG_OM_HEALTH_WIRE_VERSION;
    output[1] = (uint8_t)inktag_om_health_power_state();
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
    return INKTAG_OM_HEALTH_WIRE_SIZE;
}

static void interrupted_frame(uint32_t sp, uint32_t exception_return,
                              uint32_t *pc, uint32_t *lr) {
    /* Extended Cortex-M4 FP frames precede the basic frame by 18 words.
     * Require alignment and the complete frame inside physical 80 KiB SRAM. */
    uint32_t prefix = (exception_return & 0x10u) ? 0u : 72u;
    *pc = 0u;
    *lr = 0u;
    if ((sp & 3u) || sp < 0x20000000u ||
        sp > 0x20014000u - prefix - 32u) return;
    const uint32_t *stack = (const uint32_t *)(uintptr_t)(sp + prefix);
    *lr = stack[5];
    *pc = stack[6];
}

void inktag_om_health_watchdog_capture(uint32_t sp, uint32_t exception_return) {
    uint32_t pc, lr;
    interrupted_frame(sp, exception_return, &pc, &lr);
    mailbox_write(HEALTH_CRASH_WATCHDOG, pc, lr, SCB->CFSR, SCB->HFSR);
    /* Retain the vendor peripheral acknowledgement, then reset the CPU
     * immediately instead of waiting for the RAM-destructive full WDT reset.
     * The hardware timer stays armed as fallback; no Flash/delay or new
     * reset-domain, timeout, priority or sleep policy is introduced. */
    REGW1(&OM_PMU->WDT_STATUS, PMU_WDT_STATUS_WDT_FLAG_CLR_MASK);
    drv_wdt_keep_alive();
    drv_pmu_cpu_reset();
}

__attribute__((naked)) void WDT_IRQHandler(void) {
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
        "b inktag_om_health_watchdog_capture\n");
}

void inktag_om_health_fault_capture(uint32_t sp, uint32_t exception_return) {
    uint32_t pc, lr;
    interrupted_frame(sp, exception_return, &pc, &lr);
    mailbox_write(HEALTH_CRASH_HARDFAULT, pc, lr, SCB->CFSR, SCB->HFSR);
    for (;;) { }
}

__attribute__((naked)) void HardFault_Handler(void) {
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
        "b inktag_om_health_fault_capture\n");
}
