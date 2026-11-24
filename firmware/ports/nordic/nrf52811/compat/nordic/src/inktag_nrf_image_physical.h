#ifndef INKTAG_NRF_IMAGE_PHYSICAL_H
#define INKTAG_NRF_IMAGE_PHYSICAL_H
#include "inktag_nrf_image_store.h"
#define inktag_image_physical_init inktag_image_store_init
#define inktag_image_physical_process inktag_image_store_process
#define inktag_image_physical_take_event inktag_image_store_take_event
#define inktag_image_physical_supported inktag_image_store_supported
#define inktag_image_physical_busy inktag_image_store_busy
#define inktag_image_physical_sector_count inktag_image_store_sector_count
#define inktag_image_physical_status inktag_image_store_status
#define inktag_image_physical_begin inktag_image_store_begin
#define inktag_image_physical_append inktag_image_store_append
#define inktag_image_physical_commit inktag_image_store_commit
#define inktag_image_physical_abort inktag_image_store_abort
#define inktag_image_physical_capture_active inktag_image_store_capture_active
#define inktag_image_physical_get inktag_image_store_get
#define inktag_image_physical_next_compatible inktag_image_store_next_compatible
#define inktag_image_physical_next_page inktag_image_store_next_page
#define inktag_image_physical_read inktag_image_store_read
#define inktag_image_physical_delete inktag_image_store_delete
#define inktag_image_physical_clear_incompatible inktag_image_store_clear_incompatible
#define inktag_image_physical_clear_all inktag_image_store_clear_all
#define inktag_image_physical_clear_selected inktag_image_store_clear_selected
bool inktag_image_physical_init(void);
void inktag_image_physical_process(void);
bool inktag_image_physical_take_event(inktag_image_store_event_t *event);
bool inktag_image_physical_supported(void);
bool inktag_image_physical_busy(void);
uint16_t inktag_image_physical_sector_count(void);
inktag_image_store_result_t inktag_image_physical_status(
    const epd_screen_config_t *screen, uint32_t payload_size,
    inktag_image_store_status_t *status);
inktag_image_store_result_t inktag_image_physical_begin(
    const epd_screen_config_t *screen, uint8_t format,
    uint8_t segment_count, uint8_t black_mask, uint8_t content_type,
    const uint32_t segment_size[2], uint32_t payload_size,
    uint32_t payload_crc, uint32_t created_at, const char *name, uint8_t *id);
inktag_image_store_result_t inktag_image_physical_append(const uint8_t *data,
                                                       uint16_t length);
inktag_image_store_result_t inktag_image_physical_commit(void);
void inktag_image_physical_abort(void);
bool inktag_image_physical_capture_active(void);
inktag_image_store_result_t inktag_image_physical_get(
    uint8_t id, inktag_image_store_header_t *header);
inktag_image_store_result_t inktag_image_physical_next_compatible(
    uint8_t after_id, const epd_screen_config_t *screen, uint8_t *id);
inktag_image_store_result_t inktag_image_physical_next_page(
    uint8_t after_id, const epd_screen_config_t *screen, uint8_t *id);
inktag_image_store_result_t inktag_image_physical_read(
    uint8_t id, uint32_t offset, uint8_t *data, uint16_t length);
inktag_image_store_result_t inktag_image_physical_delete(uint8_t id);
inktag_image_store_result_t inktag_image_physical_clear_incompatible(
    const epd_screen_config_t *screen);
inktag_image_store_result_t inktag_image_physical_clear_all(void);
inktag_image_store_result_t inktag_image_physical_clear_selected(
    const uint8_t *ids, uint8_t count, bool incompatible_operation);

#endif
