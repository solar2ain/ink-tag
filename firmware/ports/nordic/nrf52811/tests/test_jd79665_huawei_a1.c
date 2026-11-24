#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "inktag_epd_driver.h"

extern const epd_driver_t epd_drv_jd79xxx;

typedef struct {
    uint8_t value;
    bool is_data;
} bus_byte_t;

typedef struct {
    bool status;
    uint16_t assert_ms;
    uint16_t release_ms;
} busy_edge_t;

static bus_byte_t s_bus[768u * 552u / 4u + 512u];
static busy_edge_t s_busy_edges[8];
static uint32_t s_bus_count;
static uint8_t s_busy_edge_count;
static uint16_t s_reset_high_before;
static uint16_t s_reset_low;
static uint16_t s_reset_high_after;
static uint8_t s_temperature_reads;

static void bus_push(bool is_data, uint8_t value) {
    assert(s_bus_count < sizeof(s_bus) / sizeof(s_bus[0]));
    s_bus[s_bus_count++] = (bus_byte_t){value, is_data};
}

static void reset_capture(void) {
    s_bus_count = 0u;
    s_busy_edge_count = 0u;
    s_reset_high_before = 0u;
    s_reset_low = 0u;
    s_reset_high_after = 0u;
    s_temperature_reads = 0u;
}

void EPD_WriteCmd(uint8_t command) { bus_push(false, command); }
void EPD_WriteData(uint8_t *data, uint8_t length) {
    uint8_t index;
    for (index = 0u; index < length; ++index) bus_push(true, data[index]);
}
void EPD_WriteByte(uint8_t value) { bus_push(true, value); }
void EPD_ReadData(uint8_t *data, uint8_t length) {
    if (length > 0u) data[0] = 0u;
}
bool EPD_ReadCommand(uint8_t command, uint8_t *data, uint8_t length) {
    assert(command == 0x40u);
    assert(data != NULL && length == 2u);
    s_temperature_reads++;
    data[0] = 0x1Au;
    data[1] = 0x80u;
    return true;
}
uint8_t EPD_ReadByte(void) { return 0u; }
void EPD_Reset(bool status, uint16_t duration) {
    (void)status;
    (void)duration;
    assert(false && "Huawei A1 must use its 20/2/20 ms reset pulse");
}
void EPD_ResetPulse(uint16_t high_before_ms, uint16_t low_ms,
                    uint16_t high_after_ms) {
    s_reset_high_before = high_before_ms;
    s_reset_low = low_ms;
    s_reset_high_after = high_after_ms;
}
void EPD_ResetDualPulse(uint16_t duration) {
    (void)duration;
    assert(false && "Huawei A1 must use one reset pulse");
}
bool EPD_ReadBusy(void) { return true; }
void EPD_WaitBusy(bool status, uint16_t timeout) {
    (void)status;
    (void)timeout;
}
bool EPD_WaitBusyEdge(bool status, uint16_t assert_timeout,
                      uint16_t release_timeout) {
    assert(s_busy_edge_count < sizeof(s_busy_edges) / sizeof(s_busy_edges[0]));
    s_busy_edges[s_busy_edge_count++] =
        (busy_edge_t){status, assert_timeout, release_timeout};
    return true;
}
bool EPD_AbortRequested(void) { return false; }
void EPD_BusyDiagReset(void) {}
void delay(uint32_t ms) { (void)ms; }

static void expect_byte(uint32_t *index, bool is_data, uint8_t value) {
    assert(*index < s_bus_count);
    assert(s_bus[*index].is_data == is_data);
    assert(s_bus[*index].value == value);
    (*index)++;
}

static void expect_command(uint32_t *index, uint8_t command,
                           const uint8_t *data, uint8_t length) {
    uint8_t offset;
    expect_byte(index, false, command);
    for (offset = 0u; offset < length; ++offset)
        expect_byte(index, true, data[offset]);
}

static epd_model_t huawei_a1_model(void) {
    epd_model_t epd;
    memset(&epd, 0, sizeof(epd));
    epd.driver_id = EPD_SCREEN_DRIVER_JD79665AA;
    epd.panel_recipe = INKTAG_RECIPE_JD79665AA_HUAWEI_398_A1;
    epd.color = COLOR_BWRY;
    epd.drv = &epd_drv_jd79xxx;
    epd.ic = DRV_IC_JD79668;
    epd.transfer_format = EPD_SCREEN_TRANSFER_INDEXED_2BPP;
    epd.width = epd.visible_width = 768u;
    epd.height = epd.visible_height = 552u;
    return epd;
}

static void test_a1_validated_full_refresh_transcript(void) {
    static const uint8_t aa[] = {0x49, 0x55, 0x20, 0x08, 0x09, 0x18};
    static const uint8_t power[] = {0x3F};
    static const uint8_t psr[] = {0x4B, 0x69};
    static const uint8_t btst1[] = {0x40, 0x1F, 0x1F, 0x2C};
    static const uint8_t btst2[] = {0x6F, 0x1F, 0x1F, 0x22};
    static const uint8_t btst[] = {0x6F, 0x1F, 0x14, 0x14};
    static const uint8_t pfs[] = {0x00, 0x54, 0x00, 0x44};
    static const uint8_t tcon[] = {0x02, 0x00};
    static const uint8_t pll[] = {0x08};
    static const uint8_t cdi[] = {0x3F};
    static const uint8_t tres[] = {0x03, 0x00, 0x02, 0x28};
    static const uint8_t gsst[] = {0x10, 0x00, 0x20, 0x00};
    static const uint8_t pws[] = {0x2F};
    static const uint8_t one[] = {0x01};
    static const uint8_t zero[] = {0x00};
    static const uint8_t window[] = {
        0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x02, 0x27, 0x01};
    static const uint8_t pixels[] = {0x1B, 0xE4};
    epd_model_t epd = huawei_a1_model();
    uint32_t index = 0u;

    reset_capture();
    epd.drv->init(&epd);
    assert(s_reset_high_before == 20u && s_reset_low == 2u &&
           s_reset_high_after == 20u);
    expect_command(&index, 0xAA, aa, sizeof(aa));
    expect_command(&index, 0x01, power, sizeof(power));
    expect_command(&index, 0x00, psr, sizeof(psr));
    expect_command(&index, 0x05, btst1, sizeof(btst1));
    expect_command(&index, 0x08, btst2, sizeof(btst2));
    expect_command(&index, 0x06, btst, sizeof(btst));
    expect_command(&index, 0x03, pfs, sizeof(pfs));
    expect_command(&index, 0x60, tcon, sizeof(tcon));
    expect_command(&index, 0x30, pll, sizeof(pll));
    expect_command(&index, 0x50, cdi, sizeof(cdi));
    expect_command(&index, 0x61, tres, sizeof(tres));
    expect_command(&index, 0x65, gsst, sizeof(gsst));
    expect_command(&index, 0xE3, pws, sizeof(pws));
    expect_command(&index, 0x84, one, sizeof(one));
    expect_command(&index, 0x83, window, sizeof(window)); /* pre-PON */
    expect_command(&index, 0x04, NULL, 0u);
    expect_command(&index, 0x83, window, sizeof(window)); /* post-PON */
    assert(index == s_bus_count);
    assert(s_temperature_reads == 1u);
    assert(s_busy_edge_count == 1u);
    assert(!s_busy_edges[0].status && s_busy_edges[0].assert_ms == 500u &&
           s_busy_edges[0].release_ms == 5000u);

    reset_capture();
    assert(EPD_DriverWriteRam(
        &epd, true, true, (uint8_t *)pixels, sizeof(pixels)));
    index = 0u;
    expect_command(&index, 0x10, pixels, sizeof(pixels));
    assert(index == s_bus_count); /* no duplicate PTL in the DTM stream */

    reset_capture();
    epd.drv->refresh(&epd);
    index = 0u;
    expect_command(&index, 0x83, window, sizeof(window)); /* pre-DRF */
    expect_command(&index, 0x12, zero, sizeof(zero));
    assert(index == s_bus_count);
    /* Dialog owns the asynchronous DRF BUSY cycle after the driver returns;
     * the common driver must not consume it and make the port wait twice. */
#if defined(INKTAG_DIALOG)
    assert(s_busy_edge_count == 0u);
#else
    assert(s_busy_edge_count == 1u);
    assert(!s_busy_edges[0].status && s_busy_edges[0].assert_ms == 500u &&
           s_busy_edges[0].release_ms == 60000u);
#endif

    reset_capture();
    epd.drv->sleep(&epd);
    index = 0u;
    expect_command(&index, 0x02, zero, sizeof(zero));
    expect_command(&index, 0x07, (const uint8_t[]){0xA5}, 1u);
    assert(index == s_bus_count);
    assert(s_busy_edge_count == 1u);
    assert(s_busy_edges[0].release_ms == 5000u);
}

static void test_a1_temperature_decodes_quarter_degree_response(void) {
    epd_model_t epd = huawei_a1_model();
    int8_t temperature = 0;
    assert(JD79xxx_ReadTemperature(&epd, &temperature));
    assert(temperature == 26);
}

static void test_a1_clear_streams_entire_frame_and_paged_gui_keeps_one_dtm(void) {
    epd_model_t epd = huawei_a1_model();
    const uint32_t frame_bytes = 768u * 552u / 4u;
    static const uint8_t window[] = {
        0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x02, 0x27, 0x01};
    reset_capture();
    epd.drv->clear(&epd, true);
    uint32_t index = 0u;
    expect_byte(&index, false, 0x10u);
    for (uint32_t i = 0u; i < frame_bytes; ++i)
        expect_byte(&index, true, 0x55u);
    expect_command(&index, 0x83u, window, sizeof(window));
    expect_command(&index, 0x12u, (const uint8_t[]){0x00u}, 1u);
    assert(index == s_bus_count);

    /* Full-width strips work even when a 106 KiB frame cannot fit SRAM.
     * The last strip is shorter; never resend DTM/PTL between GUI pages. */
    uint8_t row[192];
    memset(row, 0x1Bu, sizeof(row));
    reset_capture();
    for (uint16_t y = 0u; y < 552u; ++y)
        epd.drv->write_image(&epd, row, NULL, 0u, y, 768u, 1u);
    index = 0u;
    expect_byte(&index, false, 0x10u);
    for (uint32_t i = 0u; i < frame_bytes; ++i)
        expect_byte(&index, true, 0x1Bu);
    assert(index == s_bus_count);
}

int main(void) {
    test_a1_validated_full_refresh_transcript();
    test_a1_temperature_decodes_quarter_degree_response();
    test_a1_clear_streams_entire_frame_and_paged_gui_keeps_one_dtm();
    puts("JD79665AA Huawei A1 driver tests passed");
    return 0;
}
