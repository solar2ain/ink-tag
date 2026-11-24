/* DA normal profile: E6 implementations are not included. */
#if INKTAG_EPD_ENABLE_JD79665CA || INKTAG_EPD_ENABLE_OSPREY_E6
#error DA_source_source_requires_E6_disabled
#endif
#ifndef INKTAG_EPD_SCREEN_H
#define INKTAG_EPD_SCREEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "inktag_protocol_v2.h"

#define EPD_SCREEN_SCHEMA_VERSION INKTAG_SCREEN_PROFILE_V2_VERSION
#define EPD_SCREEN_WIRE_SIZE INKTAG_SCREEN_PROFILE_V2_WIRE_SIZE
#define EPD_SCREEN_RECORD_VERSION 1u
#define EPD_SCREEN_RECORD_SIZE 28u

/* Image-store BEGIN packet offsets, including the command byte. */
#define EPD_IMAGE_BEGIN_SCREEN_OFFSET 5u
#define EPD_IMAGE_BEGIN_METADATA_OFFSET (EPD_IMAGE_BEGIN_SCREEN_OFFSET + EPD_SCREEN_WIRE_SIZE)
#define EPD_IMAGE_BEGIN_V2_MIN_LENGTH (EPD_IMAGE_BEGIN_METADATA_OFFSET + 16u)
#define EPD_IMAGE_BEGIN_V3_MIN_LENGTH (EPD_IMAGE_BEGIN_METADATA_OFFSET + 17u)
#define EPD_IMAGE_BEGIN_PAYLOAD_SIZE_OFFSET EPD_IMAGE_BEGIN_METADATA_OFFSET
#define EPD_IMAGE_BEGIN_SEGMENT0_SIZE_OFFSET (EPD_IMAGE_BEGIN_METADATA_OFFSET + 4u)
#define EPD_IMAGE_BEGIN_SEGMENT1_SIZE_OFFSET (EPD_IMAGE_BEGIN_METADATA_OFFSET + 8u)
#define EPD_IMAGE_BEGIN_CRC32_OFFSET (EPD_IMAGE_BEGIN_METADATA_OFFSET + 12u)
#define EPD_IMAGE_BEGIN_CONTENT_TYPE_OFFSET (EPD_IMAGE_BEGIN_METADATA_OFFSET + 16u)
#define EPD_IMAGE_BEGIN_V2_NAME_OFFSET EPD_IMAGE_BEGIN_V2_MIN_LENGTH
#define EPD_IMAGE_BEGIN_V3_NAME_OFFSET EPD_IMAGE_BEGIN_V3_MIN_LENGTH

#define EPD_SCREEN_TRANSFORM_ROTATION_MASK 0x03u
#define EPD_SCREEN_TRANSFORM_ROTATE_90 0x01u
#define EPD_SCREEN_TRANSFORM_ROTATE_180 0x02u
#define EPD_SCREEN_TRANSFORM_ROTATE_270 0x03u
#define EPD_SCREEN_TRANSFORM_MIRROR_X 0x04u
#define EPD_SCREEN_TRANSFORM_MIRROR_Y 0x08u
#define EPD_SCREEN_NATIVE_UI_SCALE_SHIFT 4u
#define EPD_SCREEN_NATIVE_UI_SCALE_MASK 0xf0u
#define EPD_SCREEN_TRANSFORM_RESERVED_MASK 0xf0u
#define EPD_SCREEN_WIRE_RESERVED_MASK 0x00u

/*
 * Protocol driver IDs are append-only, but a firmware target may compile only
 * the controller paths qualified for its boards. These switches describe
 * command-path availability; they do not encode panel geometry, GPIO wiring,
 * color capability or orientation.
 */
#ifndef INKTAG_EPD_ENABLE_UC8XXX
#define INKTAG_EPD_ENABLE_UC8XXX 1
#endif
#ifndef INKTAG_EPD_ENABLE_UC8151
#define INKTAG_EPD_ENABLE_UC8151 INKTAG_EPD_ENABLE_UC8XXX
#endif
#ifndef INKTAG_EPD_ENABLE_UC8159
#define INKTAG_EPD_ENABLE_UC8159 INKTAG_EPD_ENABLE_UC8XXX
#endif
#ifndef INKTAG_EPD_ENABLE_UC8179
#define INKTAG_EPD_ENABLE_UC8179 INKTAG_EPD_ENABLE_UC8XXX
#endif
#ifndef INKTAG_EPD_ENABLE_SSD1XXX
#define INKTAG_EPD_ENABLE_SSD1XXX 1
#endif
#ifndef INKTAG_EPD_ENABLE_SSD1675
#define INKTAG_EPD_ENABLE_SSD1675 INKTAG_EPD_ENABLE_SSD1XXX
#endif
#ifndef INKTAG_EPD_ENABLE_SSD1677
#define INKTAG_EPD_ENABLE_SSD1677 INKTAG_EPD_ENABLE_SSD1XXX
#endif
#ifndef INKTAG_EPD_ENABLE_SSD1677_GRAY4
#define INKTAG_EPD_ENABLE_SSD1677_GRAY4 INKTAG_EPD_ENABLE_SSD1677
#endif
#ifndef INKTAG_EPD_ENABLE_SSD1683
#define INKTAG_EPD_ENABLE_SSD1683 INKTAG_EPD_ENABLE_SSD1XXX
#endif
#ifndef INKTAG_EPD_ENABLE_JD79XXX
#define INKTAG_EPD_ENABLE_JD79XXX 1
#endif
#ifndef INKTAG_EPD_ENABLE_JD79665CA
#define INKTAG_EPD_ENABLE_JD79665CA 0
#endif

typedef enum {
    EPD_SCREEN_STATUS_OK = 0,
    EPD_SCREEN_STATUS_UNSUPPORTED_SCHEMA = 1,
    EPD_SCREEN_STATUS_UNSUPPORTED_DRIVER = 2,
    EPD_SCREEN_STATUS_UNSUPPORTED_RECIPE = 3,
    EPD_SCREEN_STATUS_UNSUPPORTED_COLOR = 4,
    EPD_SCREEN_STATUS_UNSUPPORTED_COMBINATION = 5,
    EPD_SCREEN_STATUS_INVALID_GEOMETRY = 6,
    EPD_SCREEN_STATUS_INVALID_TRANSFORM = 7,
    EPD_SCREEN_STATUS_DISPLAY_BUSY = 8,
    EPD_SCREEN_STATUS_PERSISTENCE_FAILURE = 9,
} epd_screen_status_t;

/*
 * Public controller IDs come from the generated protocol registry. The family
 * driver owns common code; panel_recipe selects a reviewed panel-specific
 * initialization/scan path. Board GPIO and power wiring stay outside this
 * record. The EPD_SCREEN_DRIVER aliases keep internal driver code readable
 * while it is migrated to controller terminology.
 */
#define EPD_SCREEN_DRIVER_UC8XXX INKTAG_CONTROLLER_UC8XXX
#define EPD_SCREEN_DRIVER_UC8151 INKTAG_CONTROLLER_UC8151
#define EPD_SCREEN_DRIVER_UC8159 INKTAG_CONTROLLER_UC8159
#define EPD_SCREEN_DRIVER_UC8176 INKTAG_CONTROLLER_UC8176
#define EPD_SCREEN_DRIVER_UC8276 INKTAG_CONTROLLER_UC8276
#define EPD_SCREEN_DRIVER_UC8179 INKTAG_CONTROLLER_UC8179
#define EPD_SCREEN_DRIVER_SSD1619 INKTAG_CONTROLLER_SSD1619
#define EPD_SCREEN_DRIVER_SSD1675 INKTAG_CONTROLLER_SSD1675
#define EPD_SCREEN_DRIVER_SSD1677 INKTAG_CONTROLLER_SSD1677
#define EPD_SCREEN_DRIVER_SSD1680 INKTAG_CONTROLLER_SSD1680
#define EPD_SCREEN_DRIVER_SSD1683 INKTAG_CONTROLLER_SSD1683
#define EPD_SCREEN_DRIVER_JD79661 INKTAG_CONTROLLER_JD79661
#define EPD_SCREEN_DRIVER_JD79665AA INKTAG_CONTROLLER_JD79665AA
#define EPD_SCREEN_DRIVER_JD79665CA INKTAG_CONTROLLER_JD79665CA
#define EPD_SCREEN_DRIVER_JD79668AA INKTAG_CONTROLLER_JD79668AA
#define EPD_SCREEN_DRIVER_JD79XXX INKTAG_CONTROLLER_JD79XXX

typedef inktag_screen_controller_v2_t epd_screen_driver_id_t;

/*
 * Physical color capabilities are protocol values; append new values instead
 * of renumbering. Transfer packing is resolved separately by the driver.
 */
#define EPD_SCREEN_COLOR_BW INKTAG_COLOR_BW
#define EPD_SCREEN_COLOR_BWR INKTAG_COLOR_BWR
#define EPD_SCREEN_COLOR_BWRY INKTAG_COLOR_BWRY
#define EPD_SCREEN_COLOR_GRAY4 INKTAG_COLOR_GRAY4
#define EPD_SCREEN_COLOR_BWY INKTAG_COLOR_BWY
#define EPD_SCREEN_COLOR_BWB INKTAG_COLOR_BWB
#define EPD_SCREEN_COLOR_BWG INKTAG_COLOR_BWG
#define EPD_SCREEN_COLOR_BWO INKTAG_COLOR_BWO
#define EPD_SCREEN_COLOR_GRAY16 INKTAG_COLOR_GRAY16
#define EPD_SCREEN_COLOR_BWRYGB INKTAG_COLOR_BWRYGB
#define EPD_SCREEN_COLOR_BWRYGBO INKTAG_COLOR_BWRYGBO

typedef inktag_screen_color_v2_t epd_screen_color_t;

typedef enum {
    EPD_SCREEN_TRANSFER_MONO_1BPP = 1,
    EPD_SCREEN_TRANSFER_DUAL_1BPP = 2,
    EPD_SCREEN_TRANSFER_INDEXED_2BPP = 3,
    EPD_SCREEN_TRANSFER_PLANAR_2X1BPP = 4,
    EPD_SCREEN_TRANSFER_INDEXED_4BPP = 5,
    EPD_SCREEN_TRANSFER_INDEXED_2X2BPP = 6,
} epd_screen_transfer_format_t;

uint32_t epd_record_checksum(const uint8_t *data, size_t length);

typedef struct {
    uint8_t schema_version;
    /* driver_id is the public controller_id; board pins never belong here. */
    uint8_t driver_id;
    uint8_t panel_recipe;
    uint8_t color_id;
    uint8_t transform;
    uint16_t width;
    uint16_t height;
    uint16_t visible_x;
    uint16_t visible_y;
    uint16_t visible_width;
    uint16_t visible_height;
    /* Native-page text scale; packed into profile byte 4 high nibble. */
    uint8_t native_ui_scale;
} epd_screen_config_t;

typedef struct {
    uint8_t bytes[EPD_SCREEN_RECORD_SIZE];
} epd_screen_record_t;

typedef struct {
    epd_screen_config_t config;
    uint16_t row_bytes;
    uint32_t frame_bytes;
    uint8_t transfer_format;
} epd_screen_t;

/* Rotation is clockwise; mirror X/Y are applied in rotated coordinates. */
epd_screen_status_t epd_screen_resolve(
    const epd_screen_config_t *candidate, epd_screen_t *screen);
epd_screen_status_t epd_screen_config_decode(
    const uint8_t *payload, size_t length, epd_screen_config_t *config);
/* Configuration authority must reject an unimplemented recipe, not choose a
 * driver's fallback. Resolver defaults remain available to existing callers. */
epd_screen_status_t epd_screen_config_decode_exact(
    const uint8_t *payload, size_t length, epd_screen_config_t *config);
epd_screen_status_t epd_screen_config_encode(
    const epd_screen_config_t *config, uint8_t payload[EPD_SCREEN_WIRE_SIZE]);
bool epd_screen_wire_image_compatible(
    const uint8_t left[EPD_SCREEN_WIRE_SIZE],
    const uint8_t right[EPD_SCREEN_WIRE_SIZE]);
epd_screen_status_t epd_screen_record_encode(
    const epd_screen_config_t *config, epd_screen_record_t *record);
epd_screen_status_t epd_screen_record_decode(
    const epd_screen_record_t *record, epd_screen_config_t *config);

#endif
