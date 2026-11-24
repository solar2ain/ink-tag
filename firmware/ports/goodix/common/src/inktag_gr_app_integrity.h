#ifndef INKTAG_GR_APP_INTEGRITY_H
#define INKTAG_GR_APP_INTEGRITY_H

#include <stdbool.h>
#include <stdint.h>

#define INKTAG_GR_APP_INTEGRITY_BYTES 40u

typedef bool (*inktag_gr_integrity_read_t)(uint32_t address, void *data,
                                         uint32_t length);

/* native_bytes covers the linked body and its final IKCI record. Neither the
 * SDK image-info nor its optional publisher wrapper belongs to that extent. */
bool inktag_gr_app_integrity_verify(inktag_gr_integrity_read_t read,
    uint32_t app_base, uint32_t native_bytes, uint32_t payload_limit, uint32_t native_checksum,
    uint32_t board_context, uint32_t layout_context, uint32_t epoch);

#endif
