#ifndef INKTAG_DA_TRANSFER_H
#define INKTAG_DA_TRANSFER_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool active;
    uint8_t object_type;
    uint8_t transaction_id;
    uint8_t format;
    uint8_t segment_count;
    uint8_t black_mask;
    uint8_t resource_id;
    uint32_t total_size;
    uint32_t written;
    uint32_t expected_crc;
    uint32_t running_crc;
    uint32_t segment_size[2];
    uint32_t last_activity_tick;
} inktag_da_transfer_t;
#endif
