#include "inktag_da_health.h"

#include <stdbool.h>
#include <string.h>

#include "adc.h"
#include "arch.h"

#ifndef INKTAG_BOARD_POWER_THRESHOLDS_QUALIFIED
#define INKTAG_BOARD_POWER_THRESHOLDS_QUALIFIED 0
#endif

static const inktag_power_thresholds_t s_thresholds =
    INKTAG_POWER_POLICY_THRESHOLDS_INITIALIZER;

static inktag_power_sample_state_t s_samples
#if defined(INKTAG_DA_APPLICATION_MEMORY_MAP)
    __attribute__((section(".bss.inktag_free_area"), aligned(2)))
#endif
    ;

uint16_t inktag_da_read_battery_mv(void)
{
    const bool sample_vbat1v = GetBits16(ANA_STATUS_REG, BOOST_SELECTED) == 1;
    uint32_t sample;

    adc_offset_calibrate(ADC_INPUT_MODE_SINGLE_ENDED);
    sample = adc_get_vbat_sample(sample_vbat1v);
    if (sample > 2047u) return 0u;

    /* DA14585 exposes an 11-bit calibrated VBAT sample. SDK6 maps full scale
     * to 3.6 V for both buck and boost supply paths. Keep this conversion in
     * the vendor layer so protocol and GUI users cannot silently diverge. */
    return (uint16_t)((sample * 3600u + 1023u) / 2047u);
}

void inktag_da_health_init(void)
{
    uint16_t voltage;
    s_samples.operation_active = false;
    voltage = inktag_da_read_battery_mv();
    inktag_power_policy_sample_record(&s_samples, voltage,
                                      voltage != 0u);
}

uint16_t inktag_da_health_policy_voltage_mv(void)
{
    return s_samples.policy_valid ? s_samples.policy_mv : 0u;
}

uint16_t inktag_da_health_last_operation_voltage_mv(bool *valid)
{
    if (valid != NULL) *valid = s_samples.operation_valid;
    return s_samples.operation_valid ? s_samples.operation_mv : 0u;
}

void inktag_da_health_display_active(bool active)
{
    s_samples.operation_active = active;
}

bool inktag_da_health_operation_allowed(
    inktag_power_operation_t operation, bool force_sample)
{
    bool operation_active = s_samples.operation_active;
    bool valid;
    uint16_t voltage;
    voltage = inktag_da_read_battery_mv();
    valid = voltage != 0u;
    if (force_sample && !operation_active) {
        inktag_power_policy_sample_record(&s_samples, voltage, valid);
    } else {
        /* Physical-load samples serve action safety/min telemetry without
         * replacing the stable policy sample used by UI and status. */
        s_samples.operation_active = true;
        inktag_power_operation_sample_record(&s_samples, voltage, valid);
        s_samples.operation_active = operation_active;
    }
    /* DA has no independently accepted unavailable-voltage pilot. App entry,
     * physical mutations and Recovery all keep the common strict policy:
     * unavailable samples refuse; cached UI telemetry cannot authorize I/O. */
    return inktag_power_operation_allowed(
        operation, voltage, valid, &s_thresholds);
}

static void put_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

uint16_t inktag_da_health_encode(
    uint8_t record_index, uint8_t *output, uint16_t output_size)
{
    uint16_t voltage;
    bool valid;
    bool display_allowed;
    bool flash_allowed;
    bool ota_allowed;
    uint8_t flags;
    if (output == NULL || output_size < INKTAG_DA_HEALTH_WIRE_SIZE ||
        record_index != 0u) return 0u;

    voltage = s_samples.policy_mv;
    valid = s_samples.policy_valid;
    display_allowed = inktag_power_operation_allowed(
        INKTAG_POWER_OPERATION_DISPLAY, voltage, valid, &s_thresholds);
    flash_allowed = inktag_power_operation_allowed(
        INKTAG_POWER_OPERATION_FLASH, voltage, valid, &s_thresholds);
    ota_allowed = inktag_power_operation_allowed(
        INKTAG_POWER_OPERATION_OTA, voltage, valid, &s_thresholds);
    flags = (valid ? 0x01u : 0u) |
            (display_allowed ? 0x02u : 0u) |
            (flash_allowed ? 0x04u : 0u) |
            (ota_allowed ? 0x08u : 0u) |
            (INKTAG_BOARD_POWER_THRESHOLDS_QUALIFIED ? 0u : 0x80u);

    memset(output, 0, INKTAG_DA_HEALTH_WIRE_SIZE);
    output[0] = INKTAG_DA_HEALTH_WIRE_VERSION;
    output[1] = (uint8_t)inktag_power_classify(
        voltage, valid, &s_thresholds);
    output[2] = flags;
    /* Reset/fault retention is not qualified on DA14585. Keep the canonical
     * fields zero instead of inventing persistent crash evidence. */
    put_u16(&output[4], voltage);
    put_u16(&output[6], s_thresholds.warning_mv);
    put_u16(&output[8], s_thresholds.display_min_mv);
    put_u16(&output[10], s_thresholds.flash_min_mv);
    put_u16(&output[12], s_thresholds.ota_min_mv);
    put_u16(&output[14], s_thresholds.recovery_hysteresis_mv);
    return INKTAG_DA_HEALTH_WIRE_SIZE;
}
