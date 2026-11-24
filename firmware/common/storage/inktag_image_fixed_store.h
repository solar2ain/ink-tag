#ifndef INKTAG_IMAGE_FIXED_STORE_H
#define INKTAG_IMAGE_FIXED_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_epd_screen.h"

/* Release-v1 image grammar. 35 UTF-8 bytes plus the terminating NUL. */
#define INKTAG_IMAGE_STORE_SECTOR_SIZE 4096u
#define INKTAG_IMAGE_STORE_HEADER_SIZE 96u
#define INKTAG_IMAGE_STORE_NAME_SIZE 36u
#define INKTAG_IMAGE_STORE_MAX_SEGMENTS 2u

typedef enum {
    INKTAG_IMAGE_CONTENT_IMAGE = 0,
    INKTAG_IMAGE_CONTENT_SCHEDULE = 1,
    INKTAG_IMAGE_CONTENT_WIFI = 2,
    INKTAG_IMAGE_CONTENT_TODO = 3,
    INKTAG_IMAGE_CONTENT_BUSINESS = 4,
    INKTAG_IMAGE_CONTENT_PAGE = 5,
    INKTAG_IMAGE_CONTENT_MAX = INKTAG_IMAGE_CONTENT_PAGE,
} inktag_image_content_type_t;

typedef enum {
    INKTAG_IMAGE_STORE_OK = 0,
    INKTAG_IMAGE_STORE_UNSUPPORTED,
    INKTAG_IMAGE_STORE_BAD_ARGUMENT,
    INKTAG_IMAGE_STORE_BUSY,
    INKTAG_IMAGE_STORE_NO_SPACE,
    INKTAG_IMAGE_STORE_LOW_POWER,
    INKTAG_IMAGE_STORE_IO_ERROR,
    INKTAG_IMAGE_STORE_SIZE_MISMATCH,
    INKTAG_IMAGE_STORE_CRC_MISMATCH,
    INKTAG_IMAGE_STORE_NOT_FOUND,
} inktag_image_store_result_t;

typedef enum {
    INKTAG_IMAGE_STORE_EVENT_NONE = 0,
    INKTAG_IMAGE_STORE_EVENT_BEGIN,
    INKTAG_IMAGE_STORE_EVENT_WRITE,
    INKTAG_IMAGE_STORE_EVENT_COMMIT,
    INKTAG_IMAGE_STORE_EVENT_ABORT,
    INKTAG_IMAGE_STORE_EVENT_DELETE,
    INKTAG_IMAGE_STORE_EVENT_CLEAR_INCOMPATIBLE,
    INKTAG_IMAGE_STORE_EVENT_CLEAR_ALL,
    INKTAG_IMAGE_STORE_EVENT_METADATA,
} inktag_image_store_event_type_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint16_t record_sectors;
    uint8_t format;
    uint8_t segment_count;
    /* Plane bits0..1 and reading CW quarter turns in bits2..3. No wire RLE. */
    uint8_t black_mask;
    uint8_t content_type;
    /* Geometry17 retains one reserved byte in the fixed96-byte record. */
    uint8_t reserved[1];
    uint8_t screen_config[EPD_SCREEN_WIRE_SIZE];
    uint32_t segment_size[INKTAG_IMAGE_STORE_MAX_SEGMENTS];
    uint32_t payload_size;
    uint32_t payload_crc;
    uint32_t created_at;
    char name[INKTAG_IMAGE_STORE_NAME_SIZE];
    uint8_t reserved_tail[4];
    uint32_t header_crc;
} inktag_image_store_header_t;

typedef struct {
    uint16_t total_sectors;
    uint16_t free_sectors;
    uint16_t largest_free_run;
    uint16_t required_sectors;
    uint16_t saved_count;
    uint16_t compatible_count;
    uint16_t playable_count;
    uint16_t incompatible_count;
    uint16_t dirty_sectors;
} inktag_image_store_status_t;

typedef struct {
    inktag_image_store_event_type_t type;
    inktag_image_store_result_t result;
    uint8_t id;
    uint16_t removed;
} inktag_image_store_event_t;

bool inktag_image_store_init(void);
/* Caller-owned transient scanner output, never a Flash record or retained cache.
 * At most 255 fixed slots need 32 bitmap bytes, regardless of physical extents. */
typedef struct {
    uint32_t frame;
    uint16_t stride;
    uint8_t slots;
    uint8_t valid[32];
    inktag_image_store_result_t result;
} inktag_image_store_profile_view_t;
/* Read-only scan; never reads live screen settings or changes store state.
 * Caller must first enforce profile_allowed and hold foreground ownership from
 * preflight through the durable screen commit and publication. Only OK and
 * NO_SPACE candidates may be published by that workflow. Other errors preserve
 * live cache and leave the candidate disposable. No extra mutation permission. */
inktag_image_store_result_t inktag_image_store_prepare_profile(
    const epd_screen_config_t *profile, inktag_image_store_profile_view_t *view);
/* Trusted prepared candidate, same ownership scope; no IO or rejectable checks.
 * Not a public BLE setter and not permission to publish a stale/modified view. */
void inktag_image_store_publish_profile(const inktag_image_store_profile_view_t *view);
/* Unknown durable outcome: disable resources/capture with IO_ERROR, no data IO.
 * A deliberate normal init is required to restore availability. */
void inktag_image_store_invalidate(void);
bool inktag_image_store_replace_slot(uint8_t slot);
bool inktag_image_store_profile_allowed(const epd_screen_config_t *profile);
uint16_t inktag_image_store_fixed_slot_count(void);
/* Physical occupancy only; does not expose hidden content metadata or pixels. */
inktag_image_store_result_t inktag_image_store_occupancy(uint8_t *bitmap, uint8_t capacity);
/* Ascending occupied rank for the existing common quota decision, not numeric ID. */
inktag_image_store_result_t inktag_image_store_ordinal(uint8_t id, uint8_t *ordinal);
void inktag_image_store_process(void);
bool inktag_image_store_take_event(inktag_image_store_event_t *event);
bool inktag_image_store_supported(void);
bool inktag_image_store_busy(void);
uint16_t inktag_image_store_sector_count(void);
inktag_image_store_result_t inktag_image_store_status(
    const epd_screen_config_t *screen, uint32_t payload_size,
    inktag_image_store_status_t *status);
inktag_image_store_result_t inktag_image_store_begin(
    const epd_screen_config_t *screen, uint8_t format,
    uint8_t segment_count, uint8_t black_mask, uint8_t content_type,
    const uint32_t segment_size[2], uint32_t payload_size,
    uint32_t payload_crc, uint32_t created_at, const char *name, uint8_t *id);
inktag_image_store_result_t inktag_image_store_append(const uint8_t *data,
                                                       uint16_t length);
inktag_image_store_result_t inktag_image_store_commit(void);
/* Internal streaming producer only. Bind its independently accumulated CRC to
 * an exactly complete capture; normal BLE declared-CRC commit is unchanged. */
#if INKTAG_BUILD_PRO
#include "inktag_nfc_r16_profile.h"
#if INKTAG_NFC_R16_STREAMING
inktag_image_store_result_t inktag_image_store_accept_stream_crc(uint32_t crc);
#endif
#endif
void inktag_image_store_abort(void);
bool inktag_image_store_capture_active(void);
inktag_image_store_result_t inktag_image_store_get(
    uint8_t id, inktag_image_store_header_t *header);
inktag_image_store_result_t inktag_image_store_next_compatible(
    uint8_t after_id, const epd_screen_config_t *screen, uint8_t *id);
inktag_image_store_result_t inktag_image_store_next_page(
    uint8_t after_id, const epd_screen_config_t *screen, uint8_t *id);
inktag_image_store_result_t inktag_image_store_read(
    uint8_t id, uint32_t offset, uint8_t *data, uint16_t length);
inktag_image_store_result_t inktag_image_store_delete(uint8_t id);
inktag_image_store_result_t inktag_image_store_clear_incompatible(
    const epd_screen_config_t *screen);
inktag_image_store_result_t inktag_image_store_clear_all(void);
inktag_image_store_result_t inktag_image_store_clear_selected(
    const uint8_t *ids, uint8_t count, bool incompatible_operation);

#endif
