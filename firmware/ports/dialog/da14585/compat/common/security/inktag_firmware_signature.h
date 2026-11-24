#ifndef INKTAG_FIRMWARE_SIGNATURE_H
#define INKTAG_FIRMWARE_SIGNATURE_H

#include <stdint.h>

/* Portable InkTag publisher-signature trailer. The signature covers the raw
 * application followed by the first INKTAG_FIRMWARE_SIGNATURE_PREFIX_BYTES of
 * this trailer. That fixed-size prefix carries target board, layout, release
 * version and verifier identity separately from the compatibility epoch.
 * Multi-byte fields are little-endian; P-256 X/Y and R/S are big-endian. */
#define INKTAG_FIRMWARE_SIGNATURE_MAGIC        0x31534B49u /* "IKS1" */
#define INKTAG_FIRMWARE_SIGNATURE_VERSION      1u
#define INKTAG_FIRMWARE_SIGNATURE_ECDSA_P256   1u
#define INKTAG_FIRMWARE_SIGNATURE_PREFIX_BYTES 32u
#define INKTAG_FIRMWARE_SIGNATURE_BYTES        64u
#define INKTAG_FIRMWARE_SIGNATURE_TRAILER_BYTES \
    (INKTAG_FIRMWARE_SIGNATURE_PREFIX_BYTES + \
     INKTAG_FIRMWARE_SIGNATURE_BYTES)
#define INKTAG_FIRMWARE_SIGNATURE_CONTEXT_BYTES 16u

typedef struct __attribute__((packed)) {
    uint32_t board_signature;
    uint32_t layout_signature;
    uint32_t verifier_signature;
    uint32_t release_version;
} inktag_firmware_signature_context_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t algorithm;
    uint16_t trailer_size;
    uint32_t application_size;
    uint32_t compatibility_epoch;
    inktag_firmware_signature_context_t signed_context;
    uint8_t signature[INKTAG_FIRMWARE_SIGNATURE_BYTES];
} inktag_firmware_signature_trailer_t;

typedef char inktag_firmware_signature_trailer_size_check[
    sizeof(inktag_firmware_signature_trailer_t) ==
            INKTAG_FIRMWARE_SIGNATURE_TRAILER_BYTES ? 1 : -1];
typedef char inktag_firmware_signature_context_size_check[
    sizeof(inktag_firmware_signature_context_t) ==
            INKTAG_FIRMWARE_SIGNATURE_CONTEXT_BYTES ? 1 : -1];

#endif
