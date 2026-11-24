/* Reuse the physical reference's strict NOR/page/region mocks and screen. */
#include <setjmp.h>
#define main reference_physical_main
#define inktag_nrf_external_flash_read reference_read
#define inktag_nrf_external_flash_program reference_program
#define inktag_nrf_external_flash_erase_sector reference_erase
#include "test_nrf_physical_image_store.c"
#undef main
#undef inktag_nrf_external_flash_read
#undef inktag_nrf_external_flash_program
#undef inktag_nrf_external_flash_erase_sector

static jmp_buf cut;
static unsigned operation, fail_at;
static unsigned read_operations;
static bool abrupt, read_error;
static uint8_t saved[TEST_FLASH_SIZE];

bool inktag_nrf_external_flash_read(uint32_t address, void *data, uint32_t length) {
    read_operations++;
    if (read_error) { read_error = false; s_last_error = NRF_ERROR_INTERNAL; return false; }
    return reference_read(address, data, length);
}
bool inktag_nrf_external_flash_program(uint32_t address, const void *data, uint16_t length) {
    if (++operation == fail_at) {
        const uint8_t *bytes = data;
        for (unsigned i = 0; i < (length + 1u) / 2u; i++) s_flash[address + i] &= bytes[i];
        s_last_error = NRF_ERROR_INTERNAL;
        if (abrupt) longjmp(cut, 1);
        return false;
    }
    return reference_program(address, data, length);
}
bool inktag_nrf_external_flash_erase_sector(uint32_t address) {
    if (++operation == fail_at) {
        memset(s_flash + address, 255, 512);
        s_last_error = NRF_ERROR_INTERNAL;
        if (abrupt) longjmp(cut, 1);
        return false;
    }
    return reference_erase(address);
}
static void reboot(void) {
    s_session_depth = 0; fail_at = 0; read_error = false;
    inktag_image_physical_test_reboot();
    assert(inktag_image_store_init());
}
static uint8_t upload_type;
static uint8_t upload_black = 1;
static inktag_image_store_result_t upload(const epd_screen_config_t *screen, const char *name) {
    uint8_t id;
    inktag_image_store_event_t event;
    inktag_image_store_result_t result = inktag_image_store_begin(screen, 1, 2, upload_black,
        upload_type, (uint32_t[2]){4000, 4000}, 8000, crc32(s_payload, 8000), 1, name, &id);
    if (result != INKTAG_IMAGE_STORE_OK) return result;
    event = wait_event(INKTAG_IMAGE_STORE_EVENT_BEGIN);
    if (event.result != INKTAG_IMAGE_STORE_OK) return event.result;
    for (unsigned offset = 0; offset < 8000;) {
        uint16_t count = 8000 - offset > 200 ? 200 : 8000 - offset;
        result = inktag_image_store_append(s_payload + offset, count);
        if (result != INKTAG_IMAGE_STORE_OK) return result;
        offset += count;
    }
    result = inktag_image_store_commit();
    if (result == INKTAG_IMAGE_STORE_OK) assert(wait_event(INKTAG_IMAGE_STORE_EVENT_COMMIT).result == result);
    return result;
}
static void protected_and_neighbor(void) {
    assert(!memcmp(s_flash, saved, TEST_IMAGE_STORE_START));
    assert(!memcmp(s_flash + TEST_IMAGE_STORE_START + 8192,
        saved + TEST_IMAGE_STORE_START + 8192, 8192));
}

static uint32_t configured_frame = 8000;
bool epd_screen_config_read(epd_screen_config_t *screen) { *screen = hema_screen(); return true; }
epd_screen_status_t epd_screen_resolve(const epd_screen_config_t *screen, epd_screen_t *out) {
    if (!screen || !out) return EPD_SCREEN_STATUS_INVALID_GEOMETRY;
    memset(out, 0, sizeof(*out)); out->config = *screen; out->frame_bytes = configured_frame;
    return EPD_SCREEN_STATUS_OK;
}
void inktag_image_physical_test_reboot(void) { }

int main(void) {
    epd_screen_config_t screen = hema_screen();
    inktag_image_store_header_t header;
    uint8_t bytes[8000], id;
    memset(s_flash, 255, sizeof(s_flash));
    memset(s_flash, 0xa5, TEST_IMAGE_STORE_START);
    memset(s_payload, 0x34, sizeof(s_payload));
    reboot();
    assert(upload(&screen, "中文图片-1234567890123456789012") == INKTAG_IMAGE_STORE_OK);
    assert(sizeof(header) == 96 && inktag_image_store_get(0, &header) == INKTAG_IMAGE_STORE_OK);
    assert(header.magic == 0x31494b49u && header.version == 1 && strlen(header.name) == 35);
    /* CRC-correct retired namespaces cannot become pictures after Full/boot. */
    inktag_image_store_header_t canonical = header;
    for (unsigned retired=0;retired<2;retired++) {
        header=canonical;
        if (retired) header.magic=0x49475449u;
        else header.version=5u;
        header.header_crc=crc32((const uint8_t *)&header+4,sizeof(header)-8);
        memcpy(s_flash+TEST_IMAGE_STORE_START,&header,sizeof(header));reboot();
        assert(inktag_image_store_get(0,&header)==INKTAG_IMAGE_STORE_NOT_FOUND);
        memcpy(s_flash+TEST_IMAGE_STORE_START,&canonical,sizeof(canonical));reboot();
        assert(inktag_image_store_get(0,&header)==INKTAG_IMAGE_STORE_OK);
    }
    assert(upload(&screen, "neighbor") == INKTAG_IMAGE_STORE_OK);
    memcpy(saved, s_flash, sizeof(saved));
    unsigned writes = operation;
    assert(!inktag_image_store_replace_slot(59));
    assert(operation == writes);
    assert(inktag_image_store_replace_slot(20));
    assert(upload(&screen, "specified-empty") == INKTAG_IMAGE_STORE_OK);
    assert(inktag_image_store_get(20, &header) == INKTAG_IMAGE_STORE_OK);
    assert(inktag_image_store_delete(20) == INKTAG_IMAGE_STORE_OK);
    assert(wait_event(INKTAG_IMAGE_STORE_EVENT_DELETE).result == INKTAG_IMAGE_STORE_OK);
    reboot();
    assert(inktag_image_store_get(20, &header) == INKTAG_IMAGE_STORE_NOT_FOUND);
    assert(inktag_image_store_get(1, &header) == INKTAG_IMAGE_STORE_OK);
    assert(inktag_image_store_read(1, 0, bytes, 8000) == INKTAG_IMAGE_STORE_OK && !memcmp(bytes, s_payload, 8000));
    writes = operation;
    assert(upload(&screen, "NEIGHBOR") == INKTAG_IMAGE_STORE_BAD_ARGUMENT);
    assert(upload(&screen, "slot-1") == INKTAG_IMAGE_STORE_BAD_ARGUMENT);
    assert(upload(&screen, "中文图片-12345678901234567890123") == INKTAG_IMAGE_STORE_BAD_ARGUMENT);
    assert(operation == writes);
    read_error = true;
    assert(inktag_image_store_get(0, &header) == INKTAG_IMAGE_STORE_IO_ERROR);
    read_error = true;
    assert(upload(&screen, "io-error") == INKTAG_IMAGE_STORE_IO_ERROR && operation == writes);
    protected_and_neighbor();
    // Cancel before erase must leave the old image valid until real mutation.
    memcpy(s_flash, saved, sizeof(saved)); reboot();
    assert(inktag_image_store_replace_slot(0));
    assert(inktag_image_store_begin(&screen, 1, 2, 1, 0,
        (uint32_t[2]){4000,4000}, 8000, crc32(s_payload,8000), 1,
        "cancel-before-erase", &id) == INKTAG_IMAGE_STORE_OK);
    inktag_image_store_abort();
    assert(wait_event(INKTAG_IMAGE_STORE_EVENT_ABORT).result == INKTAG_IMAGE_STORE_OK);
    assert(inktag_image_store_get(0, &header) == INKTAG_IMAGE_STORE_OK);
    assert(inktag_image_store_clear_all() == INKTAG_IMAGE_STORE_OK);
    assert(wait_event(INKTAG_IMAGE_STORE_EVENT_CLEAR_ALL).removed == 2);
    reboot();
    assert(inktag_image_store_get(0, &header) == INKTAG_IMAGE_STORE_NOT_FOUND);
    assert(inktag_image_store_get(1, &header) == INKTAG_IMAGE_STORE_NOT_FOUND);

    // Durable magic followed by failed readback must not escape clear-all.
    memcpy(s_flash, saved, sizeof(saved)); reboot();
    assert(inktag_image_store_replace_slot(0));
    assert(inktag_image_store_begin(&screen, 1, 2, 1, 0,
        (uint32_t[2]){4000,4000}, 8000, crc32(s_payload,8000), 1,
        "commit-read-error", &id) == INKTAG_IMAGE_STORE_OK);
    assert(wait_event(INKTAG_IMAGE_STORE_EVENT_BEGIN).result == INKTAG_IMAGE_STORE_OK);
    for (unsigned offset = 0; offset < 8000; offset += 200)
        assert(inktag_image_store_append(s_payload + offset, 200) == INKTAG_IMAGE_STORE_OK);
    read_error = true;
    assert(inktag_image_store_commit() == INKTAG_IMAGE_STORE_IO_ERROR);
    assert(wait_event(INKTAG_IMAGE_STORE_EVENT_COMMIT).result == INKTAG_IMAGE_STORE_IO_ERROR);
    assert(inktag_image_store_clear_all() == INKTAG_IMAGE_STORE_OK);
    assert(wait_event(INKTAG_IMAGE_STORE_EVENT_CLEAR_ALL).result == INKTAG_IMAGE_STORE_OK);
    reboot();
    assert(inktag_image_store_get(0, &header) == INKTAG_IMAGE_STORE_NOT_FOUND);
    assert(inktag_image_store_get(1, &header) == INKTAG_IMAGE_STORE_NOT_FOUND);

    // Same image layout and GUI-only scale changes preserve all images.
    memcpy(s_flash, saved, sizeof(saved)); reboot();
    writes = operation;
    assert(inktag_image_store_prepare_profile(&screen) == INKTAG_IMAGE_STORE_OK);
    epd_screen_config_t changed = screen;
    changed.native_ui_scale = 2;
    assert(inktag_image_store_prepare_profile(&changed) == INKTAG_IMAGE_STORE_OK);
    assert(operation == writes);
    uint8_t occupancy[16]; memset(occupancy, 255, sizeof(occupancy));
    assert(inktag_image_store_copy_occupancy(occupancy) == 8);
    assert(occupancy[0] == 3 && occupancy[7] == 0);
    for (unsigned n = 1; n < 8; n++) assert(occupancy[n] == 0);
    changed = screen; changed.transform = 1;
    s_allow_health = false;
    assert(inktag_image_store_prepare_profile(&changed) == INKTAG_IMAGE_STORE_LOW_POWER);
    assert(operation == writes && !memcmp(s_flash, saved, sizeof(saved)));
    s_allow_health = true;
    fail_at = operation + 1; abrupt = false;
    assert(inktag_image_store_prepare_profile(&changed) == INKTAG_IMAGE_STORE_IO_ERROR);
    assert(!memcmp(s_flash, saved, TEST_IMAGE_STORE_START));
    memcpy(s_flash, saved, sizeof(saved)); reboot();
    assert(inktag_image_store_prepare_profile(NULL) == INKTAG_IMAGE_STORE_BAD_ARGUMENT);
    writes = operation;
    assert(inktag_image_store_prepare_profile(&changed) == INKTAG_IMAGE_STORE_OK);
    assert(operation - writes == 2); // Only two incompatible heads, no erases.
    for (unsigned n = TEST_IMAGE_STORE_START; n < TEST_FLASH_SIZE; n++) {
        bool header_magic = n < TEST_IMAGE_STORE_START + 2u * 8192u &&
            (n - TEST_IMAGE_STORE_START) % 8192u < 4u;
        assert(s_flash[n] == (header_magic ? 0u : saved[n]));
    }
    reboot();
    assert(inktag_image_store_get(0, &header) == INKTAG_IMAGE_STORE_NOT_FOUND);
    assert(inktag_image_store_get(1, &header) == INKTAG_IMAGE_STORE_NOT_FOUND);

    // Every erase/page/magic boundary: returned failure and abrupt power cut.
    memcpy(s_flash, saved, sizeof(saved)); reboot();
    operation = 0; assert(inktag_image_store_replace_slot(0));
    memset(s_payload, 0x78, 8000); assert(upload(&screen, "replacement") == INKTAG_IMAGE_STORE_OK);
    unsigned upload_operations = operation;
    unsigned cases = 0;
    for (unsigned mode = 0; mode < 2; mode++) {
        unsigned total = upload_operations;
        for (unsigned point = 1; point <= total; point++) {
            memcpy(s_flash, saved, sizeof(saved)); reboot();
            operation = 0; fail_at = point; abrupt = mode != 0;
            if (!setjmp(cut)) {
                assert(inktag_image_store_replace_slot(0));
                assert(upload(&screen, "replacement") == INKTAG_IMAGE_STORE_IO_ERROR);
            }
            reboot(); protected_and_neighbor();
            inktag_image_store_result_t result = inktag_image_store_get(0, &header);
            assert(result == INKTAG_IMAGE_STORE_OK || result == INKTAG_IMAGE_STORE_NOT_FOUND);
            if (result == INKTAG_IMAGE_STORE_OK) {
                assert(inktag_image_store_read(0, 0, bytes, 8000) == INKTAG_IMAGE_STORE_OK);
                uint8_t value = !strcmp(header.name, "replacement") ? 0x78 : 0x34;
                for (unsigned i = 0; i < 8000; i++) assert(bytes[i] == value);
            }
            cases++;
        }
    }
    memset(s_flash + TEST_IMAGE_STORE_START, 255, TEST_FLASH_SIZE - TEST_IMAGE_STORE_START); reboot();
    for (unsigned n = 0; n < 59; n++) {
        char name[16]; snprintf(name, sizeof(name), "image-%u", n);
        upload_type = n % (INKTAG_IMAGE_CONTENT_MAX + 1u);
        assert(upload(&screen, name) == INKTAG_IMAGE_STORE_OK);
        unsigned stored_id = n;
#if !INKTAG_NRF_FIXED_IMAGE_SLOTS
        stored_id *= 2u;
#endif
        assert(inktag_image_store_get(stored_id, &header) == INKTAG_IMAGE_STORE_OK);
        assert(header.content_type == upload_type && !strcmp(header.name, name));
    }
    assert(upload(&screen, "overflow") == INKTAG_IMAGE_STORE_NO_SPACE);
    assert(inktag_image_store_replace_slot(0));
    assert(upload(&screen, "full-replace") == INKTAG_IMAGE_STORE_OK);

    assert(inktag_image_store_sector_count() == 59);
    assert(inktag_image_store_copy_occupancy(occupancy) == 8);
    for (unsigned n = 0; n < 7; n++) assert(occupancy[n] == 255);
    assert(occupancy[7] == 7);
    uint8_t next_id = 255;
    /* The mixed upload types still separate picture rotation from page cycling. */
    assert(inktag_image_store_next_compatible(0, &screen, &next_id) == INKTAG_IMAGE_STORE_OK && next_id == 6);
    assert(inktag_image_store_next_page(0, &screen, &next_id) == INKTAG_IMAGE_STORE_OK && next_id == 1);
    configured_frame = 4000;
    assert(!inktag_image_store_profile_allowed(&screen));
    unsigned reads_before_clear = read_operations;
    assert(inktag_image_store_clear_all() == INKTAG_IMAGE_STORE_OK);
    assert(wait_event(INKTAG_IMAGE_STORE_EVENT_CLEAR_ALL).removed == 59);
    assert(read_operations == reads_before_clear);
    assert(inktag_image_store_profile_allowed(&screen));
    reboot(); assert(inktag_image_store_sector_count() == 118);
    configured_frame = 8000; reboot();
    assert(inktag_image_store_get(0, &header) == INKTAG_IMAGE_STORE_NOT_FOUND);
    s_allow_health = false; writes = operation;
    assert(inktag_image_store_begin(&screen, 1, 2, 1, 0, (uint32_t[2]){4000,4000}, 8000, 1, 1, "low", &id) == INKTAG_IMAGE_STORE_LOW_POWER);
    assert(operation == writes);
    printf("PASS fixed dense arithmetic slots/names/type, specified empty/replace, holes/reboot/read, full59/replace, IO/low power; %u interrupted boundaries; neighbor/protected regions intact\n", cases);
}
