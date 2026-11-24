#ifndef INKTAG_RESOURCE_NAME_H
#define INKTAG_RESOURCE_NAME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Historical macro and function names retained as ABI; this module only
 * validates/matches image names. It has no command encoder, decoder or session. */
#ifndef INKTAG_NFC_RESOURCE_ALIAS_MAX_BYTES
#define INKTAG_NFC_RESOURCE_ALIAS_MAX_BYTES 19u
#endif
bool inktag_nfc_resource_alias_valid(const uint8_t *alias,size_t length);
bool inktag_nfc_resource_alias_matches(const uint8_t *candidate,size_t candidate_length,
                                      const uint8_t *alias,size_t alias_length);
#endif
