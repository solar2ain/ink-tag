#ifndef INKTAG_FINDMY_H
#define INKTAG_FINDMY_H

#include <stdbool.h>
#include <stdint.h>

#define INKTAG_FINDMY_PUBLIC_KEY_BYTES 28u
#define INKTAG_FINDMY_ADDRESS_BYTES 6u
#define INKTAG_FINDMY_ADV_BYTES 31u
#define INKTAG_FINDMY_CONFIG_VERSION 1u
#define INKTAG_FINDMY_CONFIG_WIRE_BYTES 3u
#define INKTAG_FINDMY_CONFIG_RESPONSE_WIRE_BYTES 5u
#define INKTAG_FINDMY_INKTAG_WINDOW_MS 5000u
#define INKTAG_FINDMY_FIND_WINDOW_MS 1000u
/* BLE advertising intervals use 0.625 ms units: 0x0320 = 500 ms. */
#define INKTAG_FINDMY_ADV_INTERVAL_UNITS 0x0320u

typedef enum {
    INKTAG_FINDMY_MODE_OFF = 0,
    INKTAG_FINDMY_MODE_TIME_DERIVED = 1,
} inktag_findmy_mode_t;

typedef struct {
    bool enabled;
    uint8_t mode;
} inktag_findmy_config_t;

void inktag_findmy_config_default(inktag_findmy_config_t *config);
bool inktag_findmy_public_key_valid(
    const uint8_t public_key[INKTAG_FINDMY_PUBLIC_KEY_BYTES]);
bool inktag_findmy_config_valid(const inktag_findmy_config_t *config);

/* SET_FIND_CONFIG wire format: version, enabled, mode. Identity secrets are
 * never carried by this command. */
bool inktag_findmy_config_decode_set(
    const uint8_t *payload, uint16_t length,
    const inktag_findmy_config_t *current,
    inktag_findmy_config_t *next);
void inktag_findmy_config_encode_set(
    const inktag_findmy_config_t *config,
    uint8_t payload[INKTAG_FINDMY_CONFIG_WIRE_BYTES]);

/*
 * Encode one derived advertisement identity. address_msb_first uses the
 * conventional
 * human-readable address order; controller-specific ports must reverse it
 * when their GAP API stores Bluetooth addresses least-significant byte first.
 */
bool inktag_findmy_encode_identity(
    const uint8_t public_key[INKTAG_FINDMY_PUBLIC_KEY_BYTES],
    uint8_t address_msb_first[INKTAG_FINDMY_ADDRESS_BYTES],
    uint8_t advertisement[INKTAG_FINDMY_ADV_BYTES]);

#endif
