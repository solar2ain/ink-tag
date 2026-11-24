#ifndef INKTAG_DA_DEFAULT_QR_H
#define INKTAG_DA_DEFAULT_QR_H

#include <stdbool.h>
#include <stdint.h>

/* Optional, fixed release-resource extension, NOT a writable QR capability.
 * The original manifest, payload, offsets and CRC remain byte-identical. */
#define INKTAG_DA_DEFAULT_QR_OFFSET 10528u
#define INKTAG_DA_DEFAULT_QR_SIZE 120u
#define INKTAG_DA_DEFAULT_QR_MAGIC 0x31515449u /* ITQ1 */
#define INKTAG_DA_DEFAULT_QR_DESCRIPTOR 0x006A1D01u
#define INKTAG_DA_DEFAULT_QR_MODULES 29u
#define INKTAG_DA_DEFAULT_QR_BYTES 106u

/* Requires an open asset session. NULL output means erased legacy extension.
 * Dirty/short/IO/wrong-name resources fail the owning render session. CRC binds
 * bytes to the normal App name; it is not publisher or identity authentication. */
bool inktag_da_default_qr(const char *name, const uint8_t **matrix);

#endif
