#ifndef INKTAG_DEVICE_STATUS_H
#define INKTAG_DEVICE_STATUS_H
#include "inktag_protocol_v2.h"
/* Existing read-only status v2: Free, no activation scheme, 24 bytes. */
#define INKTAG_ACTIVATION_STATUS_WIRE_SIZE 24u
/* Opaque compatibility argument only; no credential state exists on this port. */
typedef struct inktag_activation_s inktag_activation_t;
#endif
