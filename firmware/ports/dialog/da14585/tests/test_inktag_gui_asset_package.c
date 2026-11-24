#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "inktag_gui_asset_package.h"

static uint8_t package_bytes[256];
static uint32_t package_length;

static uint32_t crc32(const uint8_t *data, uint16_t length) {
    return inktag_crc32_update(UINT32_C(0xffffffff), data, length) ^
           UINT32_C(0xffffffff);
}

static bool read_bytes(void *context, uint32_t offset, uint8_t *output,
                       uint16_t length) {
    (void)context;
    if (offset + length > package_length) return false;
    memcpy(output, package_bytes + offset, length);
    return true;
}

static void make_package(void) {
    const uint16_t header_size = INKTAG_GUI_ASSET_PACKAGE_HEADER_SIZE +
                                 4u * INKTAG_GUI_ASSET_ENTRY_SIZE;
    uint32_t offset = header_size;
    memset(package_bytes, 0, sizeof(package_bytes));
    memcpy(package_bytes, "ITFA", 4u);
    inktag_write_u16_le(package_bytes + 4u, INKTAG_GUI_ASSET_PACKAGE_SCHEMA);
    inktag_write_u16_le(package_bytes + 6u, header_size);
    inktag_write_u32_le(package_bytes + 8u, 7u);
    package_bytes[20u] = 4u;
    for (uint8_t id = 0u; id < 4u; ++id) {
        uint8_t *entry = package_bytes + INKTAG_GUI_ASSET_PACKAGE_HEADER_SIZE +
                         id * INKTAG_GUI_ASSET_ENTRY_SIZE;
        uint32_t length = 20u + id;
        entry[0] = INKTAG_GUI_ASSET_KIND_FONT;
        entry[1] = id;
        entry[2] = INKTAG_GUI_ASSET_FORMAT_U8G2;
        inktag_write_u32_le(entry + 4u, offset);
        inktag_write_u32_le(entry + 8u, length);
        memset(package_bytes + offset, (int)(id + 1u), length);
        inktag_write_u32_le(entry + 12u,
                            crc32(package_bytes + offset, (uint16_t)length));
        offset += length;
    }
    package_length = offset;
    inktag_write_u32_le(package_bytes + 12u, package_length);
    inktag_write_u32_le(package_bytes + 16u,
                        crc32(package_bytes, (uint16_t)package_length));
}

static void test_descriptor(void) {
    inktag_transfer_descriptor_t descriptor = {
        INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE,
        INKTAG_GUI_ASSET_PACKAGE_SCHEMA,
        1u, 0u, 1024u, 0u, {1024u, 0u}};
    assert(inktag_gui_asset_transfer_valid(&descriptor));
    descriptor.object_type = INKTAG_TRANSFER_OBJECT_STORED_IMAGE;
    assert(!inktag_gui_asset_transfer_valid(&descriptor));
    descriptor.object_type = INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE;
    descriptor.total_size = INKTAG_GUI_ASSET_PACKAGE_MAX_BYTES + 1u;
    descriptor.segment_size[0] = descriptor.total_size;
    assert(!inktag_gui_asset_transfer_valid(&descriptor));
}

static void test_package(void) {
    inktag_gui_asset_package_header_t header;
    inktag_gui_asset_entry_t entries[4];
    make_package();
    assert(inktag_gui_asset_validate(read_bytes, NULL, &header, entries));
    assert(header.generation == 7u);
    assert(entries[3].id == 3u);

    package_bytes[package_length - 1u] ^= 1u;
    assert(!inktag_gui_asset_validate(read_bytes, NULL, &header, entries));
    package_bytes[package_length - 1u] ^= 1u;

    package_bytes[8u] ^= 1u;
    assert(!inktag_gui_asset_validate(read_bytes, NULL, &header, entries));
    package_bytes[8u] ^= 1u;

    package_bytes[INKTAG_GUI_ASSET_PACKAGE_HEADER_SIZE +
                  INKTAG_GUI_ASSET_ENTRY_SIZE + 1u] = 0u;
    assert(!inktag_gui_asset_validate(read_bytes, NULL, &header, entries));
}

int main(void) {
    test_descriptor();
    test_package();
    puts("gui asset package tests passed");
    return 0;
}
