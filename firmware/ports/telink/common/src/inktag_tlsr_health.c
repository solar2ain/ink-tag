#include "inktag_tlsr_health.h"

#include <string.h>

#include "drivers/8258/compiler.h"

static _attribute_data_retention_
    inktag_tlsr_health_voltage_reader_t s_voltage_reader;
static _attribute_data_retention_ inktag_power_thresholds_t s_thresholds;
static _attribute_data_retention_ uint16_t s_voltage_mv;
static _attribute_data_retention_ bool s_voltage_valid;
/* Keep policy availability separate from reader availability in the same byte. */
static _attribute_data_retention_ uint8_t s_policy_flags;
static _attribute_data_retention_ bool s_watchdog_active;

static void put_u16(uint8_t *output, uint16_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

static void sample_voltage(bool force_sample) {
    uint16_t sampled;
    if (!s_voltage_reader || (!force_sample && s_voltage_valid)) return;
    sampled = s_voltage_reader();
    s_voltage_mv = sampled;
    s_voltage_valid = sampled >= 1800u && sampled <= 3600u;
}

void inktag_tlsr_health_init(
    inktag_tlsr_health_voltage_reader_t voltage_reader,
    const inktag_power_thresholds_t *thresholds,
    bool thresholds_qualified,
    bool watchdog_active) {
    s_voltage_reader = thresholds ? voltage_reader : NULL;
    memset(&s_thresholds, 0, sizeof(s_thresholds));
    if (thresholds) s_thresholds = *thresholds;
    s_voltage_mv = 0u;
    s_voltage_valid = false;
    s_policy_flags = (thresholds ? 1u : 0u) | (thresholds_qualified ? 2u : 0u);
    s_watchdog_active = watchdog_active;
}

uint16_t inktag_tlsr_health_voltage_mv(bool force_sample) {
    sample_voltage(force_sample);
    return s_voltage_valid ? s_voltage_mv : 0u;
}

bool inktag_tlsr_health_voltage_valid(void) {
    return s_voltage_valid;
}

inktag_power_state_t inktag_tlsr_health_power_state(void) {
    return inktag_power_classify(s_voltage_mv, s_voltage_valid, &s_thresholds);
}

bool inktag_tlsr_health_operation_allowed(
    inktag_power_operation_t operation, bool force_sample) {
    sample_voltage(force_sample);
    if (!(s_policy_flags & 1u)) return false;
    return inktag_power_operation_allowed(
        operation, s_voltage_mv, s_voltage_valid, &s_thresholds);
}

uint16_t inktag_tlsr_health_encode(
    uint8_t record_index, uint8_t *output, uint16_t output_size) {
    bool display_allowed;
    bool flash_allowed;
    bool ota_allowed;
    uint8_t flags;

    if (!output || output_size < INKTAG_TLSR_HEALTH_WIRE_SIZE) return 0u;
    if (record_index != 0u) return 0u;
    memset(output, 0, INKTAG_TLSR_HEALTH_WIRE_SIZE);
    sample_voltage(true);
    display_allowed = inktag_tlsr_health_operation_allowed(
        INKTAG_POWER_OPERATION_DISPLAY, false);
    flash_allowed = inktag_tlsr_health_operation_allowed(
        INKTAG_POWER_OPERATION_FLASH, false);
    ota_allowed = inktag_tlsr_health_operation_allowed(
        INKTAG_POWER_OPERATION_OTA, false);
    flags = (s_voltage_valid ? 0x01u : 0u) |
            (display_allowed ? 0x02u : 0u) |
            (flash_allowed ? 0x04u : 0u) |
            (ota_allowed ? 0x08u : 0u) |
            (s_watchdog_active ? 0x10u : 0u) |
            (s_policy_flags & 2u ? 0u : 0x80u);
    /* Retired warning-page bit 0x40 remains reserved and clear. */
    output[0] = INKTAG_TLSR_HEALTH_WIRE_VERSION;
    output[1] = (uint8_t)inktag_tlsr_health_power_state();
    output[2] = flags;
    /* TLSR8359 reset-reason and retained crash-record backends are not yet
     * qualified. Their canonical fields stay zero instead of inventing data. */
    output[3] = 0u;
    put_u16(&output[4], s_voltage_mv);
    put_u16(&output[6], s_thresholds.warning_mv);
    put_u16(&output[8], s_thresholds.display_min_mv);
    put_u16(&output[10], s_thresholds.flash_min_mv);
    put_u16(&output[12], s_thresholds.ota_min_mv);
    put_u16(&output[14], s_thresholds.recovery_hysteresis_mv);
    return INKTAG_TLSR_HEALTH_WIRE_SIZE;
}
