#ifndef INKTAG_NRF_PINS_H
#define INKTAG_NRF_PINS_H
#include "inktag_pin_config.h"
#include "inktag_epd_config.h"
#include "inktag_protocol_v2.h"

/* SDK/PCB routing only. Durable defaults and override publication are owned
 * by the native storage adapter; a valid wire alone does not commit data. */
bool inktag_nrf_pins_valid(const uint8_t wire[INKTAG_PIN_CONFIG_SIZE]);
const uint8_t *inktag_nrf_pins_current(void);
/* Stable read-only view for the vendor driver. Version zero is unpublished;
 * consumers must check EPD availability before touching the bus. */
extern const uint8_t *const inktag_nrf_pins_wire;
bool inktag_nrf_pins_publish(const uint8_t wire[INKTAG_PIN_CONFIG_SIZE]);
void inktag_nrf_pins_invalidate(void);
void inktag_nrf_pins_project(epd_config_t *config);
bool inktag_nrf_pins_enable_low(void);
void inktag_nrf_pins_led_apply(uint8_t colors);
/* Native immutable Factory and single atomic preference record. These calls
 * never publish routes. NULL restores the protected defaults. */
bool inktag_nrf_factory_pins(uint8_t wire[INKTAG_PIN_CONFIG_SIZE]);
bool inktag_nrf_settings_pins_write(const uint8_t wire[INKTAG_PIN_CONFIG_SIZE]);
inktag_status_v2_t inktag_nrf_settings_pins_commit(
    const uint8_t wire[INKTAG_PIN_CONFIG_SIZE],uint8_t effective[INKTAG_PIN_CONFIG_SIZE]);
#endif
