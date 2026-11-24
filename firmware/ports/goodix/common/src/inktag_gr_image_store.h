#ifndef INKTAG_GR_IMAGE_STORE_H
#define INKTAG_GR_IMAGE_STORE_H

#include "inktag_image_fixed_store.h"

/* The application supplies its already validated active profile. */
bool inktag_gr_image_store_config_read(epd_screen_config_t *screen);
inktag_image_store_result_t inktag_gr_image_store_wait(
    inktag_image_store_result_t admission, inktag_image_store_event_type_t type,
    uint16_t *removed);
inktag_image_store_result_t inktag_image_store_begin_replace(
    const epd_screen_config_t *, uint8_t format, uint8_t segment_count,
    uint8_t black_mask, uint8_t content_type, const uint32_t segment_size[2],
    uint32_t payload_size, uint32_t payload_crc, uint32_t created_at,
    const char *name, uint8_t requested_slot, uint8_t *id);
inktag_image_store_result_t inktag_image_store_next(
    uint8_t after_id, uint8_t *id, inktag_image_store_header_t *header);
bool inktag_image_store_resolve_name(const uint8_t *, uint16_t length, uint8_t *slot);
void inktag_image_store_error_context(uint16_t *sector);

#endif
