#ifndef INKTAG_TEMPERATURE_WIRE_H
#define INKTAG_TEMPERATURE_WIRE_H

#include <stdbool.h>
#include <stdint.h>

/* Optional final GET_STATUS trailer. Keep the 16-byte core and any existing
 * vendor suffix unchanged; append this record at the very end. Values are
 * signed little-endian degrees Celsius in sixteenths, not whole degrees. */
#define INKTAG_TEMPERATURE_WIRE_SIZE 9u
#define INKTAG_TEMPERATURE_WIRE_MCU_VALID 0x01u
#define INKTAG_TEMPERATURE_WIRE_PANEL_VALID 0x02u

static inline void inktag_temperature_wire_encode(
    uint8_t out[INKTAG_TEMPERATURE_WIRE_SIZE], bool mcu_valid,
    int16_t mcu_sixteenths, bool panel_valid, int16_t panel_sixteenths) {
    uint16_t mcu = (uint16_t)(mcu_valid ? mcu_sixteenths : 0);
    uint16_t panel = (uint16_t)(panel_valid ? panel_sixteenths : 0);
    out[0] = 'I';
    out[1] = 'T';
    out[2] = 'T';
    out[3] = 1u;
    out[4] = (mcu_valid ? INKTAG_TEMPERATURE_WIRE_MCU_VALID : 0u) |
             (panel_valid ? INKTAG_TEMPERATURE_WIRE_PANEL_VALID : 0u);
    out[5] = (uint8_t)mcu;
    out[6] = (uint8_t)(mcu >> 8);
    out[7] = (uint8_t)panel;
    out[8] = (uint8_t)(panel >> 8);
}

#endif
