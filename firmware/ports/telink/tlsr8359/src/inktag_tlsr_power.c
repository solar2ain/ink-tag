#include "main.h"

#include "inktag_tlsr_features.h"
#include "inktag_tlsr_power.h"

#ifdef INKTAG_TLSR_RECOVERY_POWER_PROFILE
#include "inktag_tlsr_gpio_func.h"
#define gpio_set_func(pin, func) inktag_tlsr_gpio_set_func(pin, func)
typedef inktag_tlsr_recovery_profile_t tlsr_supply_profile_t;
#define supply_profile_for(board) inktag_tlsr_board_recovery_profile(board)
#else
typedef inktag_tlsr_board_profile_t tlsr_supply_profile_t;
#define supply_profile_for(board) inktag_tlsr_board_profile(board)
#endif

static void park_supply_adc(uint32_t pin) {
    gpio_set_func(pin, AS_GPIO);
    gpio_set_output_en(pin, 0);
    gpio_set_input_en(pin, 0);
    gpio_setup_up_down_resistor(pin, PM_PIN_UP_DOWN_FLOAT);
}

static bool sample_supply_voltage(
    const tlsr_supply_profile_t *profile, uint16_t *millivolts) {
    uint16_t sampled = 0u;
    bool valid;
    if (millivolts) *millivolts = 0u;
    if (!profile || profile->supply_adc == 0u) return false;

    /* Both exact boards leave the selected ADC-capable pad unused. Probe it
     * with a weak pull-up before the SDK's documented drive-high VDD sample,
     * then restore the high-impedance parked state. A future board must add
     * its own exact profile instead of inheriting PB1 by MCU family. */
    gpio_set_func(profile->supply_adc, AS_GPIO);
    gpio_set_output_en(profile->supply_adc, 0);
    gpio_set_input_en(profile->supply_adc, 1);
    gpio_setup_up_down_resistor(profile->supply_adc, PM_PIN_PULLUP_10K);
    sleep_us(100u);
    if (gpio_read(profile->supply_adc) == 0u) {
        park_supply_adc(profile->supply_adc);
        return false;
    }
    valid = inktag_tlsr_read_supply_voltage(profile->supply_adc, &sampled);
    park_supply_adc(profile->supply_adc);
    if (millivolts) *millivolts = valid ? sampled : 0u;
    return valid;
}

bool inktag_tlsr_supply_voltage(uint16_t *millivolts) {
    return sample_supply_voltage(
        supply_profile_for(inktag_board_detect()), millivolts);
}

bool inktag_tlsr_power_operation_allowed(inktag_power_operation_t operation) {
    inktag_power_thresholds_t thresholds;
    inktag_board_id_t board = inktag_board_detect();
    uint16_t millivolts = 0u;
    bool valid;
    const tlsr_supply_profile_t *profile = supply_profile_for(board);
    /* Unknown boards remain unsupported. A supported profile without an ADC
     * route has no measurement; sample_supply_voltage never probes a fallback. */
    if (!profile) return false;
#ifdef INKTAG_TLSR_RECOVERY_POWER_PROFILE
    /* The old helper returned a qualification bit that this execution-site
     * gate did not consume. Keep exactly the same common fail-safe thresholds
     * and fresh ADC sample without retaining the full App profile table. */
    inktag_power_policy_thresholds(&thresholds);
#else
    (void)inktag_tlsr_board_power_thresholds(board, &thresholds);
#endif
    valid = sample_supply_voltage(profile, &millivolts);
    return inktag_power_operation_allowed(
        operation, millivolts, valid, &thresholds);
}

bool inktag_tlsr_flash_write_page(uint32_t address, uint32_t length,
                                  const uint8_t *data,
                                  inktag_power_operation_t operation) {
    if (length == 0u) return true;
    if (!data || !inktag_tlsr_power_operation_allowed(operation)) return false;
    flash_write_page(address, length, (uint8_t *)data);
    return true;
}

bool inktag_tlsr_flash_erase_sector(uint32_t address,
                                    inktag_power_operation_t operation) {
    if (!inktag_tlsr_power_operation_allowed(operation)) return false;
    flash_erase_sector(address);
    return true;
}
