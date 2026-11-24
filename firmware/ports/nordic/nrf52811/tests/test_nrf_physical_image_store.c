#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "inktag_nrf_external_flash.h"
#include "inktag_nrf_health.h"
#include "inktag_nrf_image_store.h"
#include "nrf_error.h"

#define TEST_FLASH_SIZE 0x80000u
#define TEST_IMAGE_STORE_START 0x0A000u
#define TEST_IMAGE_STORE_SECTORS 118u
#define TEST_SECTOR_SIZE 4096u
#define TEST_PAGE_SIZE 256u

static uint8_t s_flash[TEST_FLASH_SIZE];
static uint8_t s_payload[8000];
static uint8_t s_session_depth;
static uint32_t s_last_error;
static uint32_t s_program_calls;
static uint32_t s_erase_calls;
static uint32_t s_health_checks;
static bool s_allow_health = true;
static bool s_fail_next_program_forbidden;

bool inktag_nrf_external_flash_begin(void) {
    assert(s_session_depth != UINT8_MAX);
    s_session_depth++;
    s_last_error = NRF_SUCCESS;
    return true;
}

void inktag_nrf_external_flash_end(void) {
    assert(s_session_depth != 0u);
    s_session_depth--;
}

bool inktag_nrf_external_flash_read(uint32_t address, void *data,
                                    uint32_t length) {
    assert(s_session_depth != 0u);
    if (data == NULL || length == 0u || address > TEST_FLASH_SIZE ||
        length > TEST_FLASH_SIZE - address) {
        s_last_error = NRF_ERROR_INVALID_PARAM;
        return false;
    }
    memcpy(data, &s_flash[address], length);
    s_last_error = NRF_SUCCESS;
    return true;
}

bool inktag_nrf_external_flash_program(uint32_t address, const void *data,
                                       uint16_t length) {
    const uint8_t *input = (const uint8_t *)data;
    assert(s_session_depth != 0u);
    assert(data != NULL && length != 0u && length <= TEST_PAGE_SIZE);
    assert(address < TEST_FLASH_SIZE && length <= TEST_FLASH_SIZE - address);
    assert(address / TEST_PAGE_SIZE ==
           (address + length - 1u) / TEST_PAGE_SIZE);
    s_program_calls++;
    if (s_fail_next_program_forbidden) {
        s_fail_next_program_forbidden = false;
        s_last_error = NRF_ERROR_FORBIDDEN;
        return false;
    }
    for (uint16_t i = 0u; i < length; ++i) s_flash[address + i] &= input[i];
    s_last_error = NRF_SUCCESS;
    return true;
}

bool inktag_nrf_external_flash_erase_sector(uint32_t address) {
    assert(s_session_depth != 0u);
    assert(address < TEST_FLASH_SIZE &&
           (address & (TEST_SECTOR_SIZE - 1u)) == 0u);
    memset(&s_flash[address], 0xff, TEST_SECTOR_SIZE);
    s_erase_calls++;
    s_last_error = NRF_SUCCESS;
    return true;
}

uint32_t inktag_nrf_external_flash_last_error(void) { return s_last_error; }

bool inktag_nrf_health_operation_allowed(inktag_power_operation_t operation,
                                         bool force_sample) {
    (void)force_sample;
    assert(operation == INKTAG_POWER_OPERATION_FLASH);
    s_health_checks++;
    return s_allow_health;
}

epd_screen_status_t epd_screen_config_encode(
    const epd_screen_config_t *config, uint8_t payload[EPD_SCREEN_WIRE_SIZE]) {
    if (config == NULL || payload == NULL)
        return EPD_SCREEN_STATUS_INVALID_GEOMETRY;
    memset(payload, 0, EPD_SCREEN_WIRE_SIZE);
    payload[0] = config->schema_version;
    payload[1] = config->driver_id;
    payload[2] = config->panel_recipe;
    payload[3] = config->color_id;
    payload[4] = config->transform;
    payload[5] = (uint8_t)config->width;
    payload[6] = (uint8_t)(config->width >> 8);
    payload[7] = (uint8_t)config->height;
    payload[8] = (uint8_t)(config->height >> 8);
    payload[9] = (uint8_t)config->visible_width;
    payload[10] = (uint8_t)(config->visible_width >> 8);
    payload[11] = (uint8_t)config->visible_height;
    payload[12] = (uint8_t)(config->visible_height >> 8);
    return EPD_SCREEN_STATUS_OK;
}

epd_screen_status_t epd_screen_config_decode(
    const uint8_t *payload, size_t length, epd_screen_config_t *config) {
    if (payload == NULL || config == NULL || length != EPD_SCREEN_WIRE_SIZE)
        return EPD_SCREEN_STATUS_INVALID_GEOMETRY;
    memset(config, 0, sizeof(*config));
    config->schema_version = payload[0];
    config->driver_id = payload[1];
    config->panel_recipe = payload[2];
    config->color_id = payload[3];
    config->transform = payload[4];
    config->width = (uint16_t)payload[5] | ((uint16_t)payload[6] << 8);
    config->height = (uint16_t)payload[7] | ((uint16_t)payload[8] << 8);
    config->visible_width =
        (uint16_t)payload[9] | ((uint16_t)payload[10] << 8);
    config->visible_height =
        (uint16_t)payload[11] | ((uint16_t)payload[12] << 8);
    return EPD_SCREEN_STATUS_OK;
}

bool epd_screen_wire_image_compatible(
    const uint8_t left[EPD_SCREEN_WIRE_SIZE],
    const uint8_t right[EPD_SCREEN_WIRE_SIZE]) {
    return memcmp(left, right, EPD_SCREEN_WIRE_SIZE) == 0;
}

/* This NOR fixture deliberately uses synthetic geometries. The real strict
 * screen codec is exercised separately by screen/native-configuration tests. */
epd_screen_status_t epd_screen_config_decode_exact(
    const uint8_t *payload, size_t length, epd_screen_config_t *config) {
    return epd_screen_config_decode(payload,length,config);
}

static uint32_t crc32(const uint8_t *data, uint32_t length) {
    uint32_t crc = 0xffffffffu;
    while (length-- != 0u) {
        crc ^= *data++;
        for (uint8_t bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xffffffffu;
}

static epd_screen_config_t hema_screen(void) {
    epd_screen_config_t screen = {0};
    screen.schema_version = EPD_SCREEN_SCHEMA_VERSION;
    screen.driver_id = EPD_SCREEN_DRIVER_SSD1619;
    screen.panel_recipe = 4u;
    screen.color_id = EPD_SCREEN_COLOR_BWR;
    screen.width = 128u;
    screen.height = 250u;
    screen.visible_width = 122u;
    screen.visible_height = 250u;
    return screen;
}

static inktag_image_store_event_t wait_event(
    inktag_image_store_event_type_t expected_type) {
    inktag_image_store_event_t event = {0};
    for (uint16_t attempt = 0u; attempt < 512u; ++attempt) {
        inktag_image_store_process();
        if (inktag_image_store_take_event(&event)) {
            assert(event.type == expected_type);
            return event;
        }
    }
    assert(!"timed out waiting for image-store event");
    return event;
}

void inktag_image_physical_test_reboot(void);

static uint8_t write_typed_record(const epd_screen_config_t *screen,
                                 const char *name, uint8_t type) {
    const uint32_t segments[2] = {4000u, 4000u};
    inktag_image_store_event_t event;
    uint8_t id = UINT8_MAX;
    assert(inktag_image_store_begin(
               screen, 1u, 2u, 1u, type, segments,
               sizeof(s_payload), crc32(s_payload, sizeof(s_payload)), 7u,
               name, &id) == INKTAG_IMAGE_STORE_OK);
    event = wait_event(INKTAG_IMAGE_STORE_EVENT_BEGIN);
    assert(event.result == INKTAG_IMAGE_STORE_OK && event.id == id);
    for (uint32_t offset = 0u; offset < sizeof(s_payload);) {
        uint16_t length = (uint16_t)(sizeof(s_payload) - offset > 220u ?
                                         220u : sizeof(s_payload) - offset);
        inktag_image_store_result_t result;
        do {
            result = inktag_image_store_append(&s_payload[offset], length);
            if (result == INKTAG_IMAGE_STORE_BUSY) inktag_image_store_process();
        } while (result == INKTAG_IMAGE_STORE_BUSY);
        assert(result == INKTAG_IMAGE_STORE_OK);
        offset += length;
        inktag_image_store_process();
        inktag_image_store_process();
    }
    assert(inktag_image_store_commit() == INKTAG_IMAGE_STORE_OK);
    event = wait_event(INKTAG_IMAGE_STORE_EVENT_COMMIT);
    assert(event.result == INKTAG_IMAGE_STORE_OK && event.id == id);
    return id;
}

static uint8_t write_hema_record(const epd_screen_config_t *screen) {
    return write_typed_record(screen, "hema-bwr", INKTAG_IMAGE_CONTENT_IMAGE);
}

int main(void) {
    epd_screen_config_t screen = hema_screen();
    inktag_image_store_header_t header;
    inktag_image_store_status_t status;
    inktag_image_store_event_t event;
    uint8_t readback[sizeof(s_payload)];
    uint8_t id;

    /* Simulate the no-longer-preserved factory contents. Invalid old bytes
     * are never advertised, and only sectors allocated to a new record erase. */
    memset(s_flash, 0xa5, sizeof(s_flash));
    for (uint32_t i = 0u; i < sizeof(s_payload); ++i)
        s_payload[i] = (uint8_t)(i * 29u + 11u);

    assert(inktag_image_store_init());
    assert(s_session_depth == 0u);
    assert(inktag_image_store_sector_count() == TEST_IMAGE_STORE_SECTORS);
    assert(inktag_image_store_status(&screen, sizeof(s_payload), &status) ==
           INKTAG_IMAGE_STORE_OK);
    assert(status.total_sectors == TEST_IMAGE_STORE_SECTORS &&
           status.required_sectors == 2u);
    assert(status.saved_count == 0u &&
           status.dirty_sectors == TEST_IMAGE_STORE_SECTORS);
    assert(status.free_sectors == TEST_IMAGE_STORE_SECTORS &&
           status.largest_free_run == TEST_IMAGE_STORE_SECTORS);

    id = write_hema_record(&screen);
    assert(id == 0u && s_erase_calls == 2u);
    for (uint32_t address = 0u; address < TEST_IMAGE_STORE_START; ++address)
        assert(s_flash[address] == 0xa5u);
    assert(s_program_calls > sizeof(s_payload) / TEST_PAGE_SIZE);
    assert(inktag_image_store_get(id, &header) == INKTAG_IMAGE_STORE_OK);
    assert(header.record_sectors == 2u && header.payload_size == sizeof(s_payload));
    assert(inktag_image_store_read(id, 0u, readback, sizeof(readback)) ==
           INKTAG_IMAGE_STORE_OK);
    assert(memcmp(readback, s_payload, sizeof(s_payload)) == 0);
    assert(inktag_image_store_status(&screen, sizeof(s_payload), &status) ==
           INKTAG_IMAGE_STORE_OK);
    assert(status.saved_count == 1u &&
           status.dirty_sectors == TEST_IMAGE_STORE_SECTORS - 2u);

    assert(inktag_image_store_delete(id) == INKTAG_IMAGE_STORE_OK);
    event = wait_event(INKTAG_IMAGE_STORE_EVENT_DELETE);
    assert(event.result == INKTAG_IMAGE_STORE_OK && event.removed == 1u);
    assert(inktag_image_store_get(id, &header) == INKTAG_IMAGE_STORE_NOT_FOUND);

    /* Exercise cleanup IDs above the old 64-sector ceiling.  The backend is
     * deliberately tested below the product quota layer. */
    for (uint8_t record = 0u; record < 36u; ++record) {
        id = write_hema_record(&screen);
        assert(id == (uint8_t)(record * 2u));
    }
    assert(id == 70u);
    assert(inktag_image_store_delete(id) == INKTAG_IMAGE_STORE_OK);
    event = wait_event(INKTAG_IMAGE_STORE_EVENT_DELETE);
    assert(event.result == INKTAG_IMAGE_STORE_OK && event.removed == 1u);
    assert(inktag_image_store_get(70u, &header) ==
           INKTAG_IMAGE_STORE_NOT_FOUND);

    /* Sparse IDs remain stable across a middle deletion and a cold init.
     * Startup must not scan/erase/rewrite image payloads to compact numbers. */
    assert(inktag_image_store_delete(4u) == INKTAG_IMAGE_STORE_OK);
    event = wait_event(INKTAG_IMAGE_STORE_EVENT_DELETE);
    assert(event.result == INKTAG_IMAGE_STORE_OK);
    uint32_t erased = s_erase_calls, programmed = s_program_calls;
    inktag_image_physical_test_reboot();
    assert(inktag_image_store_init());
    assert(s_erase_calls == erased && s_program_calls == programmed);
    assert(inktag_image_store_get(4u, &header) == INKTAG_IMAGE_STORE_NOT_FOUND);
    assert(inktag_image_store_get(6u, &header) == INKTAG_IMAGE_STORE_OK);
    assert(strcmp(header.name, "hema-bwr") == 0);
    assert(header.content_type == INKTAG_IMAGE_CONTENT_IMAGE);
    assert(inktag_image_store_read(6u, 0u, readback, sizeof(readback)) == INKTAG_IMAGE_STORE_OK);
    assert(memcmp(readback, s_payload, sizeof(s_payload)) == 0);
    id = write_typed_record(&screen, "", INKTAG_IMAGE_CONTENT_PAGE);
    assert(id == 4u);
    assert(inktag_image_store_get(id, &header) == INKTAG_IMAGE_STORE_OK);
    assert(header.name[0] == '\0' && header.content_type == INKTAG_IMAGE_CONTENT_PAGE);
    assert(inktag_image_store_delete(id) == INKTAG_IMAGE_STORE_OK);
    event = wait_event(INKTAG_IMAGE_STORE_EVENT_DELETE);
    assert(event.result == INKTAG_IMAGE_STORE_OK);
    id = write_typed_record(&screen, "new", INKTAG_IMAGE_CONTENT_IMAGE);
    assert(id == 4u);
    inktag_image_physical_test_reboot();
    assert(inktag_image_store_init());
    assert(inktag_image_store_get(id, &header) == INKTAG_IMAGE_STORE_OK);
    assert(strcmp(header.name, "new") == 0 && header.content_type == INKTAG_IMAGE_CONTENT_IMAGE);

    assert(inktag_image_store_clear_all() == INKTAG_IMAGE_STORE_OK);
    event = wait_event(INKTAG_IMAGE_STORE_EVENT_CLEAR_ALL);
    assert(event.result == INKTAG_IMAGE_STORE_OK && event.removed == 35u);
    assert(inktag_image_store_status(&screen, sizeof(s_payload), &status) ==
           INKTAG_IMAGE_STORE_OK);
    assert(status.saved_count == 0u && status.dirty_sectors == 0u);
    assert(status.free_sectors == TEST_IMAGE_STORE_SECTORS &&
           status.largest_free_run == TEST_IMAGE_STORE_SECTORS);
    for (uint32_t address = 0u; address < TEST_IMAGE_STORE_START; ++address)
        assert(s_flash[address] == 0xa5u);

    s_allow_health = false;
    assert(inktag_image_store_begin(
               &screen, 1u, 2u, 1u, INKTAG_IMAGE_CONTENT_IMAGE,
               (uint32_t[2]){4000u, 4000u}, sizeof(s_payload),
               crc32(s_payload, sizeof(s_payload)), 8u, "low", &id) ==
           INKTAG_IMAGE_STORE_LOW_POWER);
    s_allow_health = true;

    assert(inktag_image_store_begin(
               &screen, 1u, 2u, 1u, INKTAG_IMAGE_CONTENT_IMAGE,
               (uint32_t[2]){4000u, 4000u}, sizeof(s_payload),
               crc32(s_payload, sizeof(s_payload)), 9u, "drop", &id) ==
           INKTAG_IMAGE_STORE_OK);
    event = wait_event(INKTAG_IMAGE_STORE_EVENT_BEGIN);
    assert(event.result == INKTAG_IMAGE_STORE_OK);
    s_fail_next_program_forbidden = true;
    assert(inktag_image_store_append(s_payload, TEST_PAGE_SIZE) ==
           INKTAG_IMAGE_STORE_LOW_POWER);
    assert(inktag_image_store_capture_active());
    inktag_image_store_abort();
    event = wait_event(INKTAG_IMAGE_STORE_EVENT_ABORT);
    assert(event.result == INKTAG_IMAGE_STORE_OK);
    assert(!inktag_image_store_capture_active());
    assert(inktag_image_store_get(id, &header) == INKTAG_IMAGE_STORE_NOT_FOUND);

    assert(s_health_checks != 0u && s_session_depth == 0u);
    puts("nRF52811 external 472 KiB typed image-store tests passed");
    return 0;
}
