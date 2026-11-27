#ifndef INKTAG_OM_PIN_LAYOUT_H
#define INKTAG_OM_PIN_LAYOUT_H
#include <stdbool.h>
#include <stdint.h>
#include "inktag_pin_config.h"
#include "inktag_aux_io.h"

/* GPIO allowlist remains board-qualified. EN/LED polarity is configurable;
 * controller communication and proven board nets remain fixed. */
#define INKTAG_OM_PIN_EDITABLE_MASK 0x03e8u
#define INKTAG_OM_PIN_FACTORY_SIZE 28u
#define INKTAG_OM_PREF_SIZE 43u
#define INKTAG_OM_PREF_MAGIC 0x31554D4Fu /* OMU1 */
#define INKTAG_OM_PREF_VERSION 1u

bool inktag_om_pin_board_valid(uint8_t board, bool hs);
void inktag_om_pin_defaults(bool hs, inktag_pin_config_t *out);
bool inktag_om_pin_allowed(void *context,inktag_pin_role_t role,uint8_t pin);
bool inktag_om_pin_valid(const inktag_pin_config_t *pins,bool hs);
uint32_t inktag_om_pin_hash(const uint8_t *data,unsigned n);
bool inktag_om_pin_factory_decode(const uint8_t *data,unsigned n,
    bool hs,uint8_t *board,inktag_pin_config_t *pins);
bool inktag_om_pin_pref_decode(const uint8_t *data,unsigned n,
    bool hs,bool *present,inktag_pin_config_t *pins);
#endif
