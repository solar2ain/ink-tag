#ifndef INKTAG_NRF_ACTIVATION_H
#define INKTAG_NRF_ACTIVATION_H

#include <stdbool.h>

#include "inktag_activation.h"

bool inktag_nrf_activation_init(inktag_activation_t *activation);
bool inktag_nrf_random_bytes(void *context, uint8_t *output, uint16_t length);

#endif
