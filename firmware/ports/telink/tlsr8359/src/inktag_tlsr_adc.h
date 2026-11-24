#ifndef INKTAG_TLSR_ADC_H
#define INKTAG_TLSR_ADC_H

#include <stdbool.h>
#include <stdint.h>

/* Sample the TLSR8359 on-die temperature sensor for diagnostics.  The official
 * B85 SDK exposes neither a supported temperature mode nor a temperature
 * calibration record, so the function deliberately returns false and never
 * claims a Celsius value. sensor_raw receives the SDK-filtered opaque sample
 * so future bench calibration remains auditable. */
bool inktag_tlsr_read_die_temperature(int8_t *celsius,
                                     uint16_t *sensor_raw);

/* Return the 1.2 V ADC reference selected from the B85 eFuse calibration.
 * The boolean distinguishes a programmed eFuse value from the documented
 * 1175 mV statistical fallback. */
bool inktag_tlsr_adc_reference(uint16_t *millivolts);

/* Read the MCU supply through a GPIO which can also feed the SAR ADC.  The
 * TLSR8258-compatible SDK briefly drives that pin high; the board layer must
 * therefore choose a pin whose high state is safe and restore its parked GPIO
 * state after this helper returns. */
bool inktag_tlsr_read_supply_voltage(uint32_t pin, uint16_t *millivolts);

#endif
