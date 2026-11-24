#ifndef INKTAG_GR_ACTIVATION_H
#define INKTAG_GR_ACTIVATION_H

#include <stdbool.h>

#include "inktag_activation.h"

bool inktag_gr_activation_init(inktag_activation_t *activation);
bool inktag_gr_security_random(void *context, uint8_t *output, uint16_t length);

#endif
