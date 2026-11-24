#ifndef INKTAG_RESOURCE_NAME_H
#define INKTAG_RESOURCE_NAME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Source-profile materializers always define this explicitly. Unsplit legacy
 * private builds retain their existing symbol/TU while they are migrated. */
#ifndef INKTAG_BUILD_PRO
#define INKTAG_BUILD_PRO 1
#endif
#ifndef INKTAG_RESOURCE_NAME_MAX_BYTES
#ifdef INKTAG_NFC_RESOURCE_ALIAS_MAX_BYTES
#define INKTAG_RESOURCE_NAME_MAX_BYTES INKTAG_NFC_RESOURCE_ALIAS_MAX_BYTES
#else
#define INKTAG_RESOURCE_NAME_MAX_BYTES 19u
#endif
#endif
#if defined(INKTAG_NFC_RESOURCE_ALIAS_MAX_BYTES) && \
    INKTAG_RESOURCE_NAME_MAX_BYTES != INKTAG_NFC_RESOURCE_ALIAS_MAX_BYTES
#error "Resource name and retained private alias bounds must agree"
#endif

#if INKTAG_BUILD_PRO
/* Compile-time aliases preserve the original private caller/implementation ABI.
 * No private header or positive NFC implementation is included in base. */
#define inktag_resource_name_valid inktag_nfc_resource_alias_valid
#define inktag_resource_name_matches inktag_nfc_resource_alias_matches
#endif
bool inktag_resource_name_valid(const uint8_t *, size_t);
bool inktag_resource_name_matches(const uint8_t *, size_t, const uint8_t *, size_t);
#endif
