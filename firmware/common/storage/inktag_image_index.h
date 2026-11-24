#ifndef INKTAG_IMAGE_INDEX_H
#define INKTAG_IMAGE_INDEX_H

#include <stdbool.h>
#include <stdint.h>

/* Settings ID30 payload, not a Flash journal or an image header. */
#define INKTAG_IMAGE_INDEX_VERSION 1u
#define INKTAG_IMAGE_INDEX_HEADER_BYTES 8u
#define INKTAG_IMAGE_INDEX_MAX_SLOTS 255u
#define INKTAG_IMAGE_INDEX_UNUSED 1023u
#define INKTAG_IMAGE_INDEX_MAX_BYTES 504u
#define INKTAG_IMAGE_INDEX_MAP_BYTES(slots) (((uint16_t)(slots) * 10u + 7u) / 8u)

typedef enum {
    INKTAG_IMAGE_INDEX_OK = 0,
    INKTAG_IMAGE_INDEX_INVALID,
    INKTAG_IMAGE_INDEX_NO_SPACE,
    INKTAG_IMAGE_INDEX_DUPLICATE,
    INKTAG_IMAGE_INDEX_NOT_FOUND
} inktag_image_index_result_t;

/* Byte encoding is explicit; no packed C structure/bitfield ABI is persisted.
 * The Settings backend supplies integrity, generation and atomic replacement.
 * capacity is <=504. Reset and unsuccessful mutations leave caller bytes/length
 * untouched. Missing/corrupt persistence is handled by the port, never reset
 * automatically by this library. Physical limit is the accepted layout bound,
 * not the temporarily detected number of available sectors. */
inktag_image_index_result_t inktag_image_index_reset(
    uint8_t *wire, uint16_t capacity, uint8_t slots, uint16_t physical_limit,
    uint16_t *length);
bool inktag_image_index_validate(const uint8_t *wire, uint16_t length,
                                uint8_t slots, uint16_t physical_limit);

/* Accessors/mutations operate on a previously validated candidate.
 * The port must separately verify complete physical headers/CRC, record span,
 * nonoverlap, device availability and quota. Set never publishes persistence. */
uint16_t inktag_image_index_get(const uint8_t *wire, uint16_t length,
                              uint8_t slot);
inktag_image_index_result_t inktag_image_index_set(
    uint8_t *wire, uint16_t *length, uint8_t slot, uint16_t physical);
uint16_t inktag_image_index_count_below(const uint8_t *wire, uint16_t length,
                                      uint16_t slot_limit);
bool inktag_image_index_first_free(const uint8_t *wire, uint16_t length,
                                  uint8_t *slot);
const uint8_t *inktag_image_index_alias_get(
    const uint8_t *wire, uint16_t length, uint8_t slot, uint8_t *alias_length);
/* Empty alias removes an explicit alias. Otherwise product UTF-8 and fixed
 * reserved resource-name rules apply. Reject duplicates; check effective header-name
 * collisions in the port as well. Input alias must not overlap wire. */
inktag_image_index_result_t inktag_image_index_alias_set(
    uint8_t *wire, uint16_t capacity, uint16_t *length,
    uint8_t slot, const uint8_t *alias, uint8_t alias_length);

#endif
