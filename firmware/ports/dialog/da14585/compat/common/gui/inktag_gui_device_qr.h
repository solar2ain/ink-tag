#ifndef INKTAG_DEVICE_INFO_QR_H
#define INKTAG_DEVICE_INFO_QR_H

#include <stdint.h>

/* The device-information page has one product QR. Its canonical WebUI URL is
 * encoded with a case-insensitive scheme/host and L correction so the same
 * destination fits every compact panel as a 25-module symbol. Platform storage
 * may replace it with one user-provisioned record. */
#define INKTAG_DEFAULT_DEVICE_QR_MODULES 25u

#if defined(INKTAG_GUI_EXTERNAL_FONT_ASSETS) && \
    !defined(INKTAG_DEVICE_INFO_QR_ASSET_DEFINITION)
#include "inktag_gui_fonts.h"
#define inktag_default_device_qr \
    INKTAG_GUI_EXTERNAL_FONT(INKTAG_GUI_DEVICE_QR_OFFSET)
#elif defined(INKTAG_DEVICE_INFO_QR_SHARED_ASSET) && \
    !defined(INKTAG_DEVICE_INFO_QR_ASSET_DEFINITION)
/* One common immutable matrix when the target links the asset TU. Keeping
 * the bound preserves sizeof() consumers; other ports retain their old mode. */
extern const uint8_t inktag_default_device_qr[79];
#else
#if defined(INKTAG_DEVICE_INFO_QR_SHARED_ASSET)
const uint8_t inktag_default_device_qr[] = {
#else
static const uint8_t inktag_default_device_qr[] = {
#endif
    0xFEu, 0xF4u, 0x3Fu, 0xC1u, 0x49u, 0x50u, 0x6Eu, 0x90u, 0x8Bu, 0xB7u, 0x42u,
    0x25u, 0xDBu, 0xA9u, 0xBAu, 0xECu, 0x17u, 0x19u, 0x07u, 0xFAu, 0xAAu, 0xFEu,
    0x01u, 0xB2u, 0x00u, 0xE6u, 0xC8u, 0xF9u, 0xB4u, 0x86u, 0x82u, 0x66u, 0x9Bu,
    0xA8u, 0xB5u, 0x36u, 0x20u, 0x93u, 0x67u, 0xE8u, 0x72u, 0x67u, 0x65u, 0xA7u,
    0x68u, 0x25u, 0x14u, 0x60u, 0xC9u, 0x62u, 0xE3u, 0x2Du, 0xF9u, 0x80u, 0x6Du,
    0x47u, 0xFFu, 0x8Au, 0xEAu, 0xF0u, 0x5Au, 0xB1u, 0xDBu, 0xA4u, 0x4Fu, 0xA5u,
    0xD3u, 0x7Au, 0xE2u, 0xEBu, 0x73u, 0xAFu, 0x05u, 0xFAu, 0x01u, 0xFEu, 0x94u,
    0x8Fu, 0x80u,
};
#endif

#endif
