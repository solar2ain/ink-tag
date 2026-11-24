#ifndef INKTAG_GR_PIN_CONFIG_H
#define INKTAG_GR_PIN_CONFIG_H

/* Shared by host fixtures and explicit vendor consumers. A codec is not GPIO,
 * NVDS or Full-write authority. Exact-board qualification remains separate. */
#if !defined(INKTAG_GR_PIN_CONFIG_OFFLINE) && !defined(INKTAG_GR_PIN_CONFIG_PORT)
#error GR configurable wiring is not yet enabled in shipping firmware
#endif

#include "inktag_pin_config.h"
#include "inktag_release_v1.h"
#include "inktag_aux_io.h"

#define INKTAG_GR_PIN_SELECTOR_BYTES 12u
#define INKTAG_GR_PIN_FACTORY_BYTES 64u
#define INKTAG_GR_PIN_FACTORY_SETTINGS_BYTES 76u
#define INKTAG_GR_PIN_USER_BYTES 48u
#define INKTAG_GR_PIN_AUX_BYTES INKTAG_RELEASE_AUX_BYTES
#define INKTAG_GR_PIN_SCREEN_BYTES 17u
#define INKTAG_GR_PIN_SELECTOR_OFFSET 0xF00u
#define INKTAG_GR_PIN_FACTORY_OFFSET 0xF20u
#define INKTAG_GR_PIN_EDITABLE_ROLES 0x03FFu

_Static_assert(INKTAG_GR_PIN_SELECTOR_OFFSET + INKTAG_GR_PIN_SELECTOR_BYTES <=
                   INKTAG_GR_PIN_FACTORY_OFFSET &&
               INKTAG_GR_PIN_FACTORY_OFFSET + INKTAG_GR_PIN_FACTORY_BYTES <= 4096u,
               "GR wiring proposal must remain inside existing boot metadata");

typedef struct {
    uint8_t family, board, mcu; /* mcu: 0x31=5331, 0x13=5513, 0x15=5515 */
    uint32_t usable[3];        /* normal, AON, MSIO; no group-3 alias */
    uint8_t button, reserved_a, reserved_b; /* button ownership; opaque reserved bytes */
    uint8_t rail;              /* rail pad cannot become an SPI/LED output */
} inktag_gr_pin_facts_t;

typedef struct {
    inktag_pin_config_t pins;
    uint8_t screen[INKTAG_GR_PIN_SCREEN_BYTES];
} inktag_gr_pin_factory_t;

typedef struct {
    uint8_t aux[INKTAG_GR_PIN_AUX_BYTES];
    bool override_present;
    inktag_pin_config_t pins;
} inktag_gr_pin_user_t;

typedef bool (*inktag_gr_pin_screen_allowed_fn)(void *context,
                                                const uint8_t screen[17]);

/* All decoders leave output untouched on failure. Zero/erased/corrupt records
 * are not a declaration of absence; the qualified storage backend owns that
 * distinction. No function performs IO or synthesizes an NVDS serialization. */
bool inktag_gr_pin_facts_valid(const inktag_gr_pin_facts_t *facts);
bool inktag_gr_pin_allowed(void *context, inktag_pin_role_t role, uint8_t pin);
bool inktag_gr_pin_validate(const inktag_pin_config_t *pins,
                             const inktag_gr_pin_facts_t *facts,
                             const uint8_t aux[INKTAG_GR_PIN_AUX_BYTES]);
bool inktag_gr_pin_aux_valid(const uint8_t aux[INKTAG_GR_PIN_AUX_BYTES],
                              const inktag_gr_pin_facts_t *facts);
void inktag_gr_pin_aux_default(uint8_t aux[INKTAG_GR_PIN_AUX_BYTES]);
bool inktag_gr_pin_factory_decode(const uint8_t *selector, size_t selector_length,
    const uint8_t *record, size_t record_length, const inktag_gr_pin_facts_t *facts,
    inktag_gr_pin_screen_allowed_fn screen_allowed, void *screen_context,
    inktag_gr_pin_factory_t *out);
bool inktag_gr_pin_user_decode(const uint8_t *data, size_t length,
    const inktag_gr_pin_facts_t *facts, const inktag_pin_config_t *defaults,
    inktag_gr_pin_user_t *out);
bool inktag_gr_pin_user_encode(const inktag_gr_pin_user_t *user,
    const inktag_gr_pin_facts_t *facts, const inktag_pin_config_t *defaults,
    uint8_t out[48]);

#endif
