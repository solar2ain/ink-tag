#ifndef INKTAG_OM_APP_INTEGRITY_H
#define INKTAG_OM_APP_INTEGRITY_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* protocol/app-integrity-descriptor-v1.json. This is stored integrity, not
 * publisher authentication. Native signing wraps body+IKCI for old Recovery. */
#define INKTAG_OM_APP_INTEGRITY_BYTES 40u
#ifndef INKTAG_OM_INTEGRITY_CODE
#define INKTAG_OM_INTEGRITY_CODE
#endif

typedef struct {
    uint32_t magic;
    uint8_t schema, descriptor_bytes;
    uint16_t flags;
    uint32_t body_bytes, body_crc32, board, layout, epoch, release;
    uint32_t reserved, descriptor_crc32;
} inktag_om_app_integrity_t;
_Static_assert(sizeof(inktag_om_app_integrity_t) == 40u, "IKCI native size");

/* CRC-valid input must also satisfy the existing native boot format before
 * committing a successful OTA. This does not authenticate its publisher. */
INKTAG_OM_INTEGRITY_CODE static bool om_app_launch_valid(
    const uint8_t *body, uint32_t bytes, uint32_t native_base) {
    uint32_t vectors[2], image[2], launch[3];
    if (!body || bytes < 0x10cu || bytes > UINT32_MAX - native_base) return false;
    memcpy(vectors, body, sizeof(vectors));
    memcpy(image, body + 36u, sizeof(image));
    memcpy(launch, body + 0x100u, sizeof(launch));
    return vectors[0] >= 0x20000000u && vectors[0] <= 0x20013ea0u &&
        vectors[1] == 0x00404101u && image[0] == native_base &&
        image[1] == 0x46533252u && launch[0] == 0x314c4d4fu &&
        launch[2] == 0xb007a55au && (launch[1] & 1u) != 0u &&
        (launch[1] & ~1u) >= native_base &&
        (launch[1] & ~1u) < native_base + bytes;
}

INKTAG_OM_INTEGRITY_CODE static uint32_t om_app_crc32(
    const uint8_t *data, uint32_t bytes) {
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0u; i < bytes; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xffffffffu;
}

INKTAG_OM_INTEGRITY_CODE static bool om_app_descriptor_valid(
    const uint8_t *body, uint32_t available, uint32_t offset,
    uint32_t board, uint32_t layout, inktag_om_app_integrity_t *descriptor) {
    if (!body || !descriptor || offset < 0x10cu ||
        available < INKTAG_OM_APP_INTEGRITY_BYTES ||
        offset > available - INKTAG_OM_APP_INTEGRITY_BYTES) return false;
    memcpy(descriptor, body + offset, sizeof(*descriptor));
    if (descriptor->magic != 0x49434b49u || descriptor->schema != 1u ||
        descriptor->descriptor_bytes != INKTAG_OM_APP_INTEGRITY_BYTES ||
        descriptor->flags != 0u || descriptor->reserved != 0u ||
        descriptor->body_bytes != offset || descriptor->board != board ||
        descriptor->layout != layout || descriptor->epoch != 1u ||
        descriptor->descriptor_crc32 != om_app_crc32(body + offset, 36u))
        return false;
    return descriptor->body_crc32 == om_app_crc32(body, offset);
}

/* Scan only within the physical slot. A locator is never admission by itself.
 * Reject two valid records, including a stale valid longer image after OTA. */
INKTAG_OM_INTEGRITY_CODE static bool om_app_integrity_locate(
    const uint8_t *body, uint32_t available, uint32_t board, uint32_t layout,
    inktag_om_app_integrity_t *descriptor) {
    if (!body || !descriptor || available < 0x10cu + 40u) return false;
    bool found = false;
    for (uint32_t offset = 0x10cu; offset <= available - 40u; ++offset) {
        if (body[offset] != 'I' || body[offset + 1u] != 'K' ||
            body[offset + 2u] != 'C' || body[offset + 3u] != 'I') continue;
        inktag_om_app_integrity_t candidate;
        if (!om_app_descriptor_valid(body, available, offset, board, layout,
                                     &candidate)) continue;
        if (found) return false;
        *descriptor = candidate;
        found = true;
    }
    return found;
}

#endif
