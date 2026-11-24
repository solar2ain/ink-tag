#include "inktag_gui_asset_package.h"

#include <limits.h>
#include <string.h>

static bool add_u32(uint32_t left, uint32_t right, uint32_t *result) {
    if (UINT32_MAX - left < right) return false;
    *result = left + right;
    return true;
}

bool inktag_gui_asset_transfer_valid(
    const inktag_transfer_descriptor_t *descriptor) {
    return descriptor != NULL &&
           descriptor->object_type == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE &&
           descriptor->format == INKTAG_GUI_ASSET_PACKAGE_SCHEMA &&
           descriptor->segment_count == 1u && descriptor->black_mask == 0u &&
           descriptor->total_size >=
               INKTAG_GUI_ASSET_PACKAGE_HEADER_SIZE +
                   INKTAG_GUI_ASSET_FONT_COUNT * INKTAG_GUI_ASSET_ENTRY_SIZE &&
           descriptor->total_size <= INKTAG_GUI_ASSET_PACKAGE_MAX_BYTES &&
           descriptor->segment_size[0] == descriptor->total_size &&
           descriptor->segment_size[1] == 0u;
}

bool inktag_gui_asset_decode_header(
    const uint8_t *data, uint16_t length,
    inktag_gui_asset_package_header_t *header) {
    if (data == NULL || header == NULL ||
        length < INKTAG_GUI_ASSET_PACKAGE_HEADER_SIZE || data[0] != 'I' ||
        data[1] != 'T' || data[2] != 'F' || data[3] != 'A' ||
        inktag_read_u16_le(data + 4u) != INKTAG_GUI_ASSET_PACKAGE_SCHEMA)
        return false;
    header->header_size = inktag_read_u16_le(data + 6u);
    header->generation = inktag_read_u32_le(data + 8u);
    header->total_size = inktag_read_u32_le(data + 12u);
    header->package_crc32 = inktag_read_u32_le(data + 16u);
    header->entry_count = data[20u];
    return data[21u] == 0u && data[22u] == 0u && data[23u] == 0u &&
           header->entry_count == INKTAG_GUI_ASSET_FONT_COUNT &&
           header->header_size ==
               INKTAG_GUI_ASSET_PACKAGE_HEADER_SIZE +
                   INKTAG_GUI_ASSET_FONT_COUNT * INKTAG_GUI_ASSET_ENTRY_SIZE &&
           header->total_size >= header->header_size &&
           header->total_size <= INKTAG_GUI_ASSET_PACKAGE_MAX_BYTES;
}

bool inktag_gui_asset_decode_entry(const uint8_t *data, uint16_t length,
                                   inktag_gui_asset_entry_t *entry) {
    if (data == NULL || entry == NULL || length < INKTAG_GUI_ASSET_ENTRY_SIZE)
        return false;
    entry->kind = data[0u];
    entry->id = data[1u];
    entry->format = data[2u];
    entry->flags = data[3u];
    entry->offset = inktag_read_u32_le(data + 4u);
    entry->length = inktag_read_u32_le(data + 8u);
    entry->crc32 = inktag_read_u32_le(data + 12u);
    return entry->kind == INKTAG_GUI_ASSET_KIND_FONT &&
           entry->id < INKTAG_GUI_ASSET_FONT_COUNT &&
           entry->format == INKTAG_GUI_ASSET_FORMAT_U8G2 &&
           entry->flags == 0u && entry->length != 0u;
}

static bool read_crc32(inktag_gui_asset_read_t read, void *context,
                       uint32_t offset, uint32_t length, uint32_t expected,
                       bool package) {
    uint8_t chunk[64];
    uint32_t crc = UINT32_C(0xffffffff);
    while (length != 0u) {
        uint16_t amount = length > sizeof(chunk) ? sizeof(chunk) : (uint16_t)length;
        if (!read(context, offset, chunk, amount)) return false;
        /* The validated package is at least its 88-byte header, so the first
         * 64-byte read always contains the complete CRC field at 16..19. */
        if (package && offset == 0u) memset(chunk + 16u, 0, 4u);
        crc = inktag_crc32_update(crc, chunk, amount);
        offset += amount;
        length -= amount;
    }
    return (crc ^ UINT32_C(0xffffffff)) == expected;
}

bool inktag_gui_asset_validate(inktag_gui_asset_read_t read, void *context,
                               inktag_gui_asset_package_header_t *header,
                               inktag_gui_asset_entry_t entries[INKTAG_GUI_ASSET_FONT_COUNT]) {
    uint8_t raw_header[INKTAG_GUI_ASSET_PACKAGE_HEADER_SIZE];
    uint8_t raw_entry[INKTAG_GUI_ASSET_ENTRY_SIZE];
    bool seen[INKTAG_GUI_ASSET_FONT_COUNT] = {false, false, false, false};
    uint32_t previous_end;
    if (read == NULL || header == NULL || entries == NULL ||
        !read(context, 0u, raw_header, sizeof(raw_header)) ||
        !inktag_gui_asset_decode_header(raw_header, sizeof(raw_header), header))
        return false;
    previous_end = header->header_size;
    for (uint8_t index = 0u; index < header->entry_count; ++index) {
        uint32_t end;
        if (!read(context,
                  INKTAG_GUI_ASSET_PACKAGE_HEADER_SIZE +
                      (uint32_t)index * INKTAG_GUI_ASSET_ENTRY_SIZE,
                  raw_entry, sizeof(raw_entry)) ||
            !inktag_gui_asset_decode_entry(raw_entry, sizeof(raw_entry),
                                           &entries[index]) ||
            seen[entries[index].id] || entries[index].offset < previous_end ||
            !add_u32(entries[index].offset, entries[index].length, &end) ||
            end > header->total_size ||
            !read_crc32(read, context, entries[index].offset,
                        entries[index].length, entries[index].crc32, false))
            return false;
        seen[entries[index].id] = true;
        previous_end = end;
    }
    for (uint8_t id = 0u; id < INKTAG_GUI_ASSET_FONT_COUNT; ++id)
        if (!seen[id]) return false;
    return previous_end == header->total_size &&
           read_crc32(read, context, 0u, header->total_size,
                      header->package_crc32, true);
}
