#ifndef INKTAG_TLSR_ACTIVATION_H
#define INKTAG_TLSR_ACTIVATION_H

#include "inktag_tlsr_build_profile.h"
#include <stdbool.h>
#include <stdint.h>

#include "inktag_activation.h"

#if INKTAG_BUILD_PRO
#include "inktag_tlsr_activation_key_pro.inc"
#endif

typedef bool (*inktag_tlsr_activation_uid_fn)(
    void *context, uint8_t *output, uint16_t output_size,
    uint16_t *output_length);

typedef struct {
    void *context;
    inktag_activation_read_fn read;
    inktag_activation_write_fn write;
    inktag_activation_clear_fn clear;
    inktag_tlsr_activation_uid_fn read_uid;
    uint16_t platform_id;
    uint16_t product_id;
    uint16_t security_epoch;
    uint32_t feature_mask;
    uint8_t key_id;
} inktag_tlsr_activation_config_t;

bool inktag_tlsr_activation_init(
    inktag_activation_t *activation,
    const inktag_tlsr_activation_config_t *config);

#endif
