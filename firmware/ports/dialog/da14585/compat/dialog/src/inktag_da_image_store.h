#ifndef INKTAG_DA_IMAGE_STORE_H
#define INKTAG_DA_IMAGE_STORE_H

#include "inktag_image_fixed_store.h"

/* Thin synchronous bridge for DA's serialized outer SDK loop. The common
 * engine owns all product state and v5 records; each erase still services BLE.
 * These adapters consume the engine event before the next public command.
 */
inktag_image_store_result_t inktag_da_store_begin(
    const epd_screen_config_t *screen, uint8_t format,
    uint8_t segment_count, uint8_t black_mask, uint8_t content_type,
    const uint32_t segment_size[2], uint32_t payload_size,
    uint32_t payload_crc, uint32_t created_at, const char *name,
    uint8_t requested_slot, uint8_t *id);
inktag_image_store_result_t inktag_da_store_commit(uint8_t *id);
void inktag_da_store_abort(void);
bool inktag_da_store_profile_allowed(const epd_screen_config_t *profile);
inktag_image_store_result_t inktag_da_store_next(
    uint8_t after_id, uint8_t *id, inktag_image_store_header_t *header);
inktag_image_store_result_t inktag_da_store_delete(uint8_t id);
inktag_image_store_result_t inktag_da_store_clear(
    const epd_screen_config_t *incompatible_screen, uint16_t *removed);

#endif
