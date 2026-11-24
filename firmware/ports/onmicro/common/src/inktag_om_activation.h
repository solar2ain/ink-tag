#ifndef INKTAG_OM_ACTIVATION_H
#define INKTAG_OM_ACTIVATION_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_activation.h"

/* Derive the activation identity only from the exact board's immutable
 * hardware root. A writable GAP address or persisted user setting is never a
 * fallback; absence/read failure leaves activation unavailable. */
bool inktag_om_activation_init(inktag_activation_t *activation);

#endif
