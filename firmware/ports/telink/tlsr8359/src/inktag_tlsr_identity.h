#ifndef INKTAG_TLSR_IDENTITY_H
#define INKTAG_TLSR_IDENTITY_H

#include <stdint.h>
#include <stdbool.h>

bool inktag_tlsr_identity_init(uint8_t ordinary_mac[6]);
/* Canonical ordinary type, not the current temporary Find radio address. */
uint8_t inktag_tlsr_identity_address_type(void);
bool inktag_tlsr_identity_read_uid(uint8_t uid[16]);

#endif
