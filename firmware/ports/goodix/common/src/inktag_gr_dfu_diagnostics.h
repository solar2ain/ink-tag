#ifndef INKTAG_GR_DFU_DIAGNOSTICS_H
#define INKTAG_GR_DFU_DIAGNOSTICS_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_ota_reject.h"

void inktag_gr_dfu_reject_clear(void);

bool inktag_gr_dfu_reject_pending(void);

void inktag_gr_dfu_reject_set(inktag_ota_reject_reason_t reason,
                              inktag_ota_reject_stage_t stage,
                              uint8_t flags, uint32_t detail,
                              uint16_t sampled_mv, uint16_t threshold_mv);

void inktag_gr_dfu_reject_set_power(uint16_t sampled_mv,
                                    bool sampled_mv_valid);

void inktag_gr_dfu_reject_set_power_threshold(uint16_t sampled_mv,
                                              bool sampled_mv_valid,
                                              uint16_t threshold_mv);

/* Strong Goodix implementation of the weak hook injected into dfu_port.c. */
bool inktag_dfu_rejection_info(inktag_ota_reject_stage_t stage,
                               uint8_t detail_flags, uint32_t detail,
                               inktag_ota_reject_info_t *info);

#endif
