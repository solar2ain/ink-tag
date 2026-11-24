#ifndef INKTAG_GR_IMAGE_IO_H
#define INKTAG_GR_IMAGE_IO_H

#include <stdint.h>
#include <stdbool.h>

/* Physical IO only: no product state machine or runtime callback table. */
#define INKTAG_GR_IMAGE_IO_MAX_EXTENTS 2u

typedef struct {
    uint32_t start;
    uint32_t bytes;
} inktag_gr_image_io_extent_t;

typedef enum {
    INKTAG_GR_IMAGE_IO_OK,
    INKTAG_GR_IMAGE_IO_INVALID,
    INKTAG_GR_IMAGE_IO_RESERVED,
    INKTAG_GR_IMAGE_IO_UNAVAILABLE,
    INKTAG_GR_IMAGE_IO_LOW_POWER,
    INKTAG_GR_IMAGE_IO_ERROR,
} inktag_gr_image_io_result_t;

/* Read-only hardware admission; call after board/health/GUI initialization.
 * A failed call revokes the previous map, never leaves stale write authority. */
inktag_gr_image_io_result_t inktag_gr_image_io_open(void);
uint8_t inktag_gr_image_io_extents(inktag_gr_image_io_extent_t out[2]);
uint16_t inktag_gr_image_io_sector_count(void);
bool inktag_gr_image_io_power_allowed(void);
uint16_t inktag_gr_image_io_slot_count(uint16_t stride);
/* Direct arithmetic in one physical extent; UINT32_MAX means invalid. */
uint32_t inktag_gr_image_io_slot_address(uint8_t id, uint16_t stride);
inktag_gr_image_io_result_t inktag_gr_image_io_span(uint32_t address,
                                                 uint32_t length);
inktag_gr_image_io_result_t inktag_gr_image_io_read(uint32_t address,
    uint8_t *data, uint16_t length);
inktag_gr_image_io_result_t inktag_gr_image_io_program(uint32_t address,
    const uint8_t *data, uint16_t length);
inktag_gr_image_io_result_t inktag_gr_image_io_erase(uint32_t address);

#endif
