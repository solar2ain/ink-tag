#ifndef INKTAG_GR_DEVICE_INFO_QR_H
#define INKTAG_GR_DEVICE_INFO_QR_H

#include <stdint.h>

#include "inktag_protocol_core.h"
#include "inktag_device_password.h"

bool inktag_gr_password_load(inktag_device_password_config_t *);
/* Internal final step after authenticated ordinary settings reset; not a BLE
 * ingress. Never infer OFF from missing, delete ID5 or preserve secret QR. */
inktag_status_v2_t inktag_gr_password_reset_settings(
    inktag_device_password_config_t *);
inktag_status_v2_t inktag_gr_password_bundle_begin(
    const inktag_transfer_descriptor_t *);
inktag_status_v2_t inktag_gr_password_bundle_commit(
    inktag_device_password_t *, inktag_device_password_config_t *, uint32_t);

inktag_status_v2_t inktag_gr_device_info_qr_begin(
    const inktag_transfer_descriptor_t *descriptor);
inktag_status_v2_t inktag_gr_device_info_qr_write(
    uint32_t offset, const uint8_t *data, uint16_t length);
inktag_status_v2_t inktag_gr_device_info_qr_commit(
    inktag_device_password_config_t *);
void inktag_gr_device_info_qr_abort(void);

/* The returned data remains valid until the next QR read or transfer begin. */
bool inktag_gr_device_info_qr_read(uint8_t slot, uint8_t *modules,
                                    const uint8_t **data, uint16_t *length);

#endif
