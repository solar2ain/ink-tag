#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define INKTAG_DIALOG 1
#include "inktag_epd_driver.h"

extern const epd_driver_t epd_drv_jd79xxx;

typedef struct {
    uint8_t data;
    bool is_data;
} bus_byte_t;

static bus_byte_t s_bus[31000];
static uint32_t s_bus_count;
static uint8_t s_read_length;
static bool s_read_ok = true;
static uint8_t s_read_raw[2] = {0x1Bu, 0x80u};
static uint16_t s_reset_duration;
static uint8_t s_busy_edge_calls;
static bool s_busy_edge_status[4];
static uint16_t s_busy_edge_assert_ms[4];
static uint16_t s_busy_edge_release_ms[4];

static void bus_push(bool is_data, uint8_t data) {
    assert(s_bus_count < sizeof(s_bus) / sizeof(s_bus[0]));
    s_bus[s_bus_count].is_data = is_data;
    s_bus[s_bus_count].data = data;
    s_bus_count++;
}

static void reset_capture(void) {
    memset(s_bus, 0, sizeof(s_bus));
    s_bus_count = 0u;
    s_read_length = 0u;
    s_busy_edge_calls = 0u;
    memset(s_busy_edge_status, 0, sizeof(s_busy_edge_status));
    memset(s_busy_edge_assert_ms, 0, sizeof(s_busy_edge_assert_ms));
    memset(s_busy_edge_release_ms, 0, sizeof(s_busy_edge_release_ms));
}

void EPD_WriteCmd(uint8_t cmd) { bus_push(false, cmd); }
void EPD_WriteData(uint8_t *value, uint8_t len) {
    uint8_t index;
    for (index = 0u; index < len; ++index) bus_push(true, value[index]);
}
void EPD_ReadData(uint8_t *value, uint8_t len) {
    assert(value != NULL);
    s_read_length = len;
    if (len > 0u) value[0] = s_read_raw[0];
    if (len > 1u) value[1] = s_read_raw[1];
}
bool EPD_ReadCommand(uint8_t cmd, uint8_t *value, uint8_t len) {
    EPD_WriteCmd(cmd);
    EPD_ReadData(value, len);
    return s_read_ok;
}
void EPD_WriteByte(uint8_t value) { bus_push(true, value); }
uint8_t EPD_ReadByte(void) { return 0x1Bu; }
void EPD_Reset(bool status, uint16_t duration) {
    assert(status);
    s_reset_duration = duration;
}
void EPD_ResetPulse(uint16_t high_before_ms, uint16_t low_ms,
                    uint16_t high_after_ms) {
    (void)high_before_ms;
    (void)low_ms;
    (void)high_after_ms;
    assert(false && "Hanshow recipe must use its existing reset");
}
void EPD_ResetDualPulse(uint16_t duration) { s_reset_duration = (uint8_t)duration; }
bool EPD_ReadBusy(void) { return true; }
bool EPD_AbortRequested(void) { return false; }
void EPD_BusyDiagReset(void) {}
void EPD_WaitBusy(bool status, uint16_t timeout) {
    (void)status;
    (void)timeout;
}
bool EPD_WaitBusyEdge(bool status, uint16_t assert_timeout,
                      uint16_t release_timeout) {
    assert(s_busy_edge_calls < 4u);
    s_busy_edge_status[s_busy_edge_calls] = status;
    s_busy_edge_assert_ms[s_busy_edge_calls] = assert_timeout;
    s_busy_edge_release_ms[s_busy_edge_calls] = release_timeout;
    s_busy_edge_calls++;
    return true;
}
void delay(uint32_t ms) { (void)ms; }

static void expect_byte(uint32_t *index, bool is_data, uint8_t value) {
    assert(*index < s_bus_count);
    assert(s_bus[*index].is_data == is_data);
    assert(s_bus[*index].data == value);
    (*index)++;
}

static void expect_command(uint32_t *index, uint8_t command,
                           const uint8_t *data, uint8_t length) {
    uint8_t offset;
    expect_byte(index, false, command);
    for (offset = 0u; offset < length; ++offset)
        expect_byte(index, true, data[offset]);
}

static epd_model_t hanshow_model(void) {
    epd_model_t epd;
    memset(&epd, 0, sizeof(epd));
    epd.driver_id = EPD_SCREEN_DRIVER_JD79668AA;
    epd.panel_recipe = INKTAG_RECIPE_JD79668AA_HANSHOW_NEBULAR_420R_N;
    epd.color = COLOR_BWRY;
    epd.drv = &epd_drv_jd79xxx;
    epd.ic = DRV_IC_JD79668;
    epd.transfer_format = EPD_SCREEN_TRANSFER_INDEXED_2BPP;
    epd.width = epd.visible_width = 400u;
    epd.height = epd.visible_height = 300u;
    return epd;
}

static void test_qualified_init_transcript(void) {
    static const uint8_t d4d[] = {0x78};
    static const uint8_t dae[] = {0x8F};
    static const uint8_t dbd[] = {0x80};
    static const uint8_t db0[] = {0x13};
    static const uint8_t dad[] = {0x17};
    static const uint8_t db6[] = {0x0F};
    static const uint8_t d00[] = {0x07, 0x29};
    static const uint8_t d01[] = {0x07, 0x00};
    static const uint8_t d03[] = {0x10, 0x54, 0x44};
    static const uint8_t d06[] = {0x0F, 0x00, 0x3F, 0x0A, 0x25, 0x2D, 0x18};
    static const uint8_t d30[] = {0x08};
    static const uint8_t d50[] = {0x37};
    static const uint8_t df0[] = {0x7D};
    static const uint8_t d61[] = {0x01, 0x90, 0x01, 0x2C};
    static const uint8_t d65[] = {0x00, 0x00, 0x00, 0x00};
    static const uint8_t de3[] = {0x22};
    static const uint8_t de7[] = {0x1C};
    static const uint8_t de9[] = {0x01};
    epd_model_t epd = hanshow_model();
    uint32_t index = 0u;

    reset_capture();
    epd.drv->init(&epd);
    assert(s_reset_duration == 200u);
    expect_command(&index, 0x4D, d4d, sizeof(d4d));
    expect_command(&index, 0xAE, dae, sizeof(dae));
    expect_command(&index, 0xBD, dbd, sizeof(dbd));
    expect_command(&index, 0xB0, db0, sizeof(db0));
    expect_command(&index, 0xAD, dad, sizeof(dad));
    expect_command(&index, 0xB6, db6, sizeof(db6));
    expect_command(&index, 0x00, d00, sizeof(d00));
    expect_command(&index, 0x01, d01, sizeof(d01));
    expect_command(&index, 0x03, d03, sizeof(d03));
    expect_command(&index, 0x06, d06, sizeof(d06));
    expect_command(&index, 0x30, d30, sizeof(d30));
    expect_command(&index, 0x50, d50, sizeof(d50));
    expect_command(&index, 0xF0, df0, sizeof(df0));
    expect_command(&index, 0x61, d61, sizeof(d61));
    expect_command(&index, 0x65, d65, sizeof(d65));
    expect_command(&index, 0xE3, de3, sizeof(de3));
    expect_command(&index, 0xE7, de7, sizeof(de7));
    expect_command(&index, 0xE9, de9, sizeof(de9));
    expect_command(&index, 0x83,
                   (const uint8_t[]){0x00, 0x00, 0x01, 0x8F,
                                     0x00, 0x00, 0x01, 0x2B, 0x01}, 9u);
    expect_byte(&index, false, 0x40);
    expect_byte(&index, false, 0x04);
    expect_command(&index, 0x83,
                   (const uint8_t[]){0x00, 0x00, 0x01, 0x8F,
                                     0x00, 0x00, 0x01, 0x2B, 0x01}, 9u);
    assert(index == s_bus_count);
    assert(s_read_length == 2u);
    assert(s_busy_edge_calls == 2u);
    assert(!s_busy_edge_status[0] && !s_busy_edge_status[1]);
    assert(s_busy_edge_assert_ms[0] == 100u);
    assert(s_busy_edge_release_ms[0] == 5000u);
    assert(s_busy_edge_assert_ms[1] == 500u);
    assert(s_busy_edge_release_ms[1] == 5000u);
}

static void test_full_frame_and_manual_refresh_contract(void) {
    epd_model_t epd = hanshow_model();
    uint8_t data[] = {0x00, 0x55, 0xC0, 0xFF};
    uint32_t index;

    reset_capture();
    epd.drv->clear(&epd, false);
    assert(s_bus_count == 30001u);
    assert(!s_bus[0].is_data && s_bus[0].data == 0x10u);
    for (index = 1u; index < s_bus_count; ++index) {
        assert(s_bus[index].is_data);
        assert(s_bus[index].data == 0x55u);
    }

    reset_capture();
    assert(EPD_DriverWriteRam(&epd, true, true, data, sizeof(data)));
    assert(s_bus_count == 5u);
    assert(!s_bus[0].is_data && s_bus[0].data == 0x10u);
    for (index = 0u; index < sizeof(data); ++index)
        assert(s_bus[index + 1u].is_data && s_bus[index + 1u].data == data[index]);

    reset_capture();
    epd.drv->refresh(&epd);
    index = 0u;
    expect_command(&index, 0x83,
                   (const uint8_t[]){0x00, 0x00, 0x01, 0x8F,
                                     0x00, 0x00, 0x01, 0x2B, 0x01}, 9u);
    expect_command(&index, 0x12, (const uint8_t[]){0x00}, 1u);
    expect_command(&index, 0x02, (const uint8_t[]){0x00}, 1u);
    assert(index == s_bus_count);
    assert(s_busy_edge_calls == 2u);
    assert(!s_busy_edge_status[0] && !s_busy_edge_status[1]);
    assert(s_busy_edge_assert_ms[0] == 500u);
    assert(s_busy_edge_release_ms[0] == 60000u);
    assert(s_busy_edge_assert_ms[1] == 500u);
    assert(s_busy_edge_release_ms[1] == 5000u);

    reset_capture();
    epd.drv->sleep(&epd);
    assert(s_bus_count == 2u);
    assert(!s_bus[0].is_data && s_bus[0].data == 0x07u);
    assert(s_bus[1].is_data && s_bus[1].data == 0xA5u);
}

static uint32_t find_command(uint8_t command, uint32_t start) {
    uint32_t index;
    for (index = start; index < s_bus_count; ++index) {
        if (!s_bus[index].is_data && s_bus[index].data == command)
            return index;
    }
    assert(false && "expected command missing from Hanshow pass");
    return 0u;
}

static void test_each_hanshow_clear_pass_rearms_before_ram_and_refresh(void) {
    epd_model_t epd = hanshow_model();
    uint8_t plane[64];
    uint8_t pass;

    for (pass = 0u; pass < 3u; ++pass) {
        uint32_t pon, dtm1, drf, pof;
        memset(plane, pass == 1u ? 0x00 : 0x55, sizeof(plane));
        reset_capture();
        epd.drv->init(&epd);
        assert(EPD_DriverWriteRam(&epd, true, true, plane, sizeof(plane)));
        epd.drv->refresh(&epd);
        pon = find_command(0x04u, 0u);
        dtm1 = find_command(0x10u, pon + 1u);
        drf = find_command(0x12u, dtm1 + 1u);
        pof = find_command(0x02u, drf + 1u);
        assert(pon < dtm1 && dtm1 < drf && drf < pof);
        assert(s_busy_edge_calls == 4u); /* init reset/PON, then DRF/POF */
        assert(!s_busy_edge_status[2] && !s_busy_edge_status[3]);
    }
}

static void test_nebular213_bwr_bwry_identical_init_and_packed_write(void) {
    static const uint8_t init_bytes[] = {
        0x00, 0x07, 0x29, 0x06, 0x0F, 0x8B, 0x9C, 0x96,
        0x30, 0x08, 0x50, 0x37, 0x61, 0x00, 0x80, 0x00, 0xFA,
        0x65, 0x00, 0x00, 0x00, 0x00, 0xE7, 0x96, 0xE9, 0x01, 0x40
    };
    static const uint8_t command_offsets[] = {0, 3, 8, 10, 12, 17, 22, 24, 26};
    /* B=00 W=01 Y=10 R=11. A row repeats all four device-ready codes. */
    uint8_t row[32];
    epd_model_t epd = hanshow_model();
    uint8_t pass;
    memset(row, 0x1B, sizeof(row));
    epd.driver_id = EPD_SCREEN_DRIVER_JD79XXX;
    epd.panel_recipe = INKTAG_RECIPE_JD79XXX_HANSHOW_NEBULAR_213R_N;
    epd.ic = DRV_IC_JD79XXX_ZKC;
    epd.width = 128u; epd.height = 250u;
    epd.visible_width = 122u; epd.visible_height = 250u;
    for (pass = 0u; pass < 2u; ++pass) {
        uint32_t i, command_index = 0u;
        uint16_t y;
        epd.color = pass == 0u ? COLOR_BWR : COLOR_BWRY;
        reset_capture();
        epd.drv->init(&epd);
        assert(s_reset_duration == 50u);
        assert(s_bus_count == sizeof(init_bytes));
        assert(s_read_length == 2u); /* Checked/cacheable R40 before drawing. */
        assert(epd.drv->read_temp(&epd) == 27);
        /* The independent read above appends one command; init stays exact. */
        assert(s_bus_count == sizeof(init_bytes) + 1u && s_read_length == 2u);
        for (i = 0u; i < sizeof(init_bytes); ++i) {
            bool command = command_index < sizeof(command_offsets) &&
                           i == command_offsets[command_index];
            assert(s_bus[i].is_data == !command);
            assert(s_bus[i].data == init_bytes[i]);
            if (command) ++command_index;
        }
        reset_capture();
        for (y = 0u; y < 250u; ++y)
            epd.drv->write_image(&epd, row, NULL, 0u, y, 128u, 1u);
        assert(s_bus_count == 8001u);
        assert(!s_bus[0].is_data && s_bus[0].data == 0x10u);
        for (i = 1u; i < s_bus_count; ++i)
            assert(s_bus[i].is_data && s_bus[i].data == 0x1Bu);
    }
}

static void test_nebular213_temperature_rejects_failed_or_invalid_read(void) {
    epd_model_t epd = hanshow_model();
    int8_t temperature = 42;
    epd.driver_id = EPD_SCREEN_DRIVER_JD79XXX;
    epd.panel_recipe = INKTAG_RECIPE_JD79XXX_HANSHOW_NEBULAR_213R_N;
    reset_capture();
    assert(JD79xxx_ReadTemperature(&epd, &temperature) && temperature == 27);
    s_read_ok = false;
    assert(!JD79xxx_ReadTemperature(&epd, &temperature));
    assert(epd.drv->read_temp(&epd) == INT8_MIN);
    s_read_ok = true;
    s_read_raw[0] = s_read_raw[1] = 0xFFu;
    assert(!JD79xxx_ReadTemperature(&epd, &temperature));
    s_read_raw[0] = 86u; s_read_raw[1] = 0u;
    assert(!JD79xxx_ReadTemperature(&epd, &temperature));
    s_read_raw[0] = 0x1Bu; s_read_raw[1] = 0x80u;
}

int main(void) {
    test_qualified_init_transcript();
    test_full_frame_and_manual_refresh_contract();
    test_each_hanshow_clear_pass_rearms_before_ram_and_refresh();
    test_nebular213_bwr_bwry_identical_init_and_packed_write();
    test_nebular213_temperature_rejects_failed_or_invalid_read();
    puts("JD79668 Hanshow driver tests passed");
    return 0;
}
