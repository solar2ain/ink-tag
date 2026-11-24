#ifndef INKTAG_GUI_ASSET_PACKAGE_H
#define INKTAG_GUI_ASSET_PACKAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_protocol_core.h"

#define INKTAG_GUI_ASSET_PACKAGE_SCHEMA 1u
#define INKTAG_GUI_ASSET_PACKAGE_HEADER_SIZE 24u
#define INKTAG_GUI_ASSET_ENTRY_SIZE 16u
#define INKTAG_GUI_ASSET_FONT_COUNT 4u
#define INKTAG_GUI_ASSET_PACKAGE_MAX_BYTES 16384u

typedef enum {
    INKTAG_GUI_ASSET_KIND_FONT = 1u,
} inktag_gui_asset_kind_t;

typedef enum {
    INKTAG_GUI_FONT_WQY9_TEXT = 0u,
    INKTAG_GUI_FONT_WQY12_TEXT = 1u,
    INKTAG_GUI_FONT_HELV14_NUMERIC = 2u,
    INKTAG_GUI_FONT_HELV18_NUMERIC = 3u,
} inktag_gui_font_face_t;

typedef enum {
    INKTAG_GUI_ASSET_FORMAT_U8G2 = 1u,
} inktag_gui_asset_format_t;

typedef struct {
    uint32_t generation;
    uint32_t total_size;
    uint32_t package_crc32;
    uint8_t entry_count;
    uint16_t header_size;
} inktag_gui_asset_package_header_t;

typedef struct {
    uint8_t kind;
    uint8_t id;
    uint8_t format;
    uint8_t flags;
    uint32_t offset;
    uint32_t length;
    uint32_t crc32;
} inktag_gui_asset_entry_t;

typedef bool (*inktag_gui_asset_read_t)(void *context, uint32_t offset,
                                        uint8_t *output, uint16_t length);

bool inktag_gui_asset_transfer_valid(
    const inktag_transfer_descriptor_t *descriptor);
bool inktag_gui_asset_decode_header(
    const uint8_t *data, uint16_t length,
    inktag_gui_asset_package_header_t *header);
bool inktag_gui_asset_decode_entry(const uint8_t *data, uint16_t length,
                                   inktag_gui_asset_entry_t *entry);
bool inktag_gui_asset_validate(inktag_gui_asset_read_t read, void *context,
                               inktag_gui_asset_package_header_t *header,
                               inktag_gui_asset_entry_t entries[INKTAG_GUI_ASSET_FONT_COUNT]);

#endif
