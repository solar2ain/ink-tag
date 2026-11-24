#ifndef INKTAG_OM_FINDMY_STORAGE_H
#define INKTAG_OM_FINDMY_STORAGE_H

#include "inktag_settings_records.h"
#include "nvds.h"

/* NVDS_TAG_USER_BEGIN + 13 held the retired 0.1 single-key record. Keep it
 * untouched so current-only firmware cannot reinterpret legacy bytes.
 * +15 is the non-secret Find enable record and +16 is display settings.
 * Common record 31 maps to protected OM NVDS tag +17. */
#define INKTAG_OM_FINDMY_CONFIG_NVDS_TAG \
    ((nvds_tag_id_t)(NVDS_TAG_USER_BEGIN + 15u))
#define INKTAG_OM_FINDMY_DERIVATION_SEED_NVDS_TAG \
    ((nvds_tag_id_t)(NVDS_TAG_USER_BEGIN + 17u))

_Static_assert(INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED == 31u,
               "OM NVDS tag +17 maps common protected record 31");

#endif
