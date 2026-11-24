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
static bool abrupt, read_error;
static uint8_t saved[TEST_FLASH_SIZE];

bool inktag_nrf_external_flash_read(uint32_t address, void *data, uint32_t length) {
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
static inktag_image_store_result_t upload(const epd_screen_config_t *screen, const char *name) {
    uint8_t id;
    inktag_image_store_event_t event;
    inktag_image_store_result_t result = inktag_image_store_begin(screen, 1, 2, 1,
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
    assert(header.version == 4 && strlen(header.name) == 35);
    assert(upload(&screen, "neighbor") == INKTAG_IMAGE_STORE_OK);
    memcpy(saved, s_flash, sizeof(saved));
    unsigned writes = operation;
    assert(inktag_image_store_replace_slot(1));
    assert(upload(&screen, "inside") == INKTAG_IMAGE_STORE_NO_SPACE);
    assert(operation == writes);
    assert(inktag_image_store_replace_slot(20));
    assert(upload(&screen, "specified-empty") == INKTAG_IMAGE_STORE_OK);
    assert(inktag_image_store_get(20, &header) == INKTAG_IMAGE_STORE_OK);
    assert(inktag_image_store_delete(20) == INKTAG_IMAGE_STORE_OK);
    assert(wait_event(INKTAG_IMAGE_STORE_EVENT_DELETE).result == INKTAG_IMAGE_STORE_OK);
    reboot();
    assert(inktag_image_store_get(20, &header) == INKTAG_IMAGE_STORE_NOT_FOUND);
    assert(inktag_image_store_get(2, &header) == INKTAG_IMAGE_STORE_OK);
    assert(inktag_image_store_read(2, 0, bytes, 8000) == INKTAG_IMAGE_STORE_OK && !memcmp(bytes, s_payload, 8000));
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
    s_allow_health = false; writes = operation;
    assert(inktag_image_store_begin(&screen, 1, 2, 1, 0, (uint32_t[2]){4000,4000}, 8000, 1, 1, "low", &id) == INKTAG_IMAGE_STORE_LOW_POWER);
    assert(operation == writes);
    printf("PASS simple physical names/type, specified empty/replace, holes/reboot/read, full59/replace, IO/low power; %u interrupted boundaries; neighbor/protected regions intact\n", cases);
}
