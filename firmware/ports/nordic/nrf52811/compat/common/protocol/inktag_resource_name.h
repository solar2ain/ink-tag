#ifndef INKTAG_RESOURCE_NAME_H
#define INKTAG_RESOURCE_NAME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef INKTAG_RESOURCE_NAME_MAX_BYTES
#define INKTAG_RESOURCE_NAME_MAX_BYTES 19u
#endif
/* Persisted image names retain strict UTF-8, control/space/length checks and
 * reserved product vocabulary. ASCII letters fold for duplicate matching;
 * non-ASCII bytes compare exactly. This module has no NFC command codec. */
bool inktag_resource_name_valid(const uint8_t *name,size_t length);
bool inktag_resource_name_matches(const uint8_t *candidate,size_t candidate_length,
                                  const uint8_t *name,size_t name_length);
#endif
