#ifndef INKTAG_DA_RUNTIME_SCRATCH_H
#define INKTAG_DA_RUNTIME_SCRATCH_H

#include <stdint.h>

/* SDK6 actively uses the first 140 bytes of rwip_heap_non_ret after BLE is
 * connected.  Protocol staging must therefore have independent storage.
 * Only one bounded image-record transaction uses this arena. Dynamic
 * device-info QR upload/persistence is intentionally unsupported on DA;
 * the product QR remains an immutable external GUI asset. */
#define INKTAG_DA_TRANSFER_SCRATCH_BYTES 96u
#define INKTAG_DA_JD79XX_A0_STREAM_BYTES 196u

extern uint32_t
    inktag_da_transfer_scratch[INKTAG_DA_TRANSFER_SCRATCH_BYTES / sizeof(uint32_t)];
extern uint8_t inktag_da_gui_frame_buffer[];

#endif
