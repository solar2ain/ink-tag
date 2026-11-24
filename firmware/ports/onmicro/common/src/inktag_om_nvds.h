#ifndef INKTAG_OM_NVDS_H
#define INKTAG_OM_NVDS_H

#include <stdbool.h>
#include "nvds.h"

/* Project-owned NVDS mutations must pass a fresh ordinary-Flash voltage
 * sample immediately before entering the vendor persistence engine. */
uint8_t inktag_om_nvds_put(nvds_tag_id_t tag, nvds_tag_len_t length,
                           const void *buffer);
uint8_t inktag_om_nvds_del(nvds_tag_id_t tag);
/* Logical deletion/read-back only; not physical secret scrubbing. */
bool inktag_om_nvds_delete_verified(nvds_tag_id_t tag);
/* Necessary ID30 admission only, never boot/read-only housekeeping. */
bool inktag_om_public_cache_retire_for_index(void);
bool inktag_om_public_cache_retirement_allowed(void);
struct inktag_public_cache_workspace;
struct inktag_public_cache_workspace *inktag_om_public_cache_workspace(void);

#endif
