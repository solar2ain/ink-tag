#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "inktag_epd_driver.h"

/* Regression fingerprints are frozen from pre-Nebular213 integration
 * d3672f88, using identical deterministic bus inputs. They cover command/data,
 * resets, BUSY waits, reads and delays, not physical panel qualification.
 * Test both Dialog's asynchronous Huawei path and the synchronous path used
 * by the other vendors. Do not regenerate fingerprints to bless a change. */
static uint32_t s_hash;
static uint32_t s_events;
static uint8_t s_command, s_first_data[256], s_data_count[256];

static void event(uint8_t kind, uint32_t value) {
    uint8_t i;
    s_hash = (s_hash ^ kind) * 16777619u;
    for (i = 0; i < 4u; ++i) {
        s_hash = (s_hash ^ (uint8_t)value) * 16777619u;
        value >>= 8u;
    }
    ++s_events;
}

void EPD_WriteCmd(uint8_t value) {
    s_command = value; s_data_count[value] = 0u; event('C', value);
}
void EPD_WriteByte(uint8_t value) {
    if (s_data_count[s_command]++ == 0u) s_first_data[s_command] = value;
    event('D', value);
}
void EPD_WriteData(uint8_t *data, uint8_t length) {
    uint8_t i;
    for (i = 0; i < length; ++i) EPD_WriteByte(data[i]);
}
void EPD_ReadData(uint8_t *data, uint8_t length) {
    uint8_t i;
    event('R', length);
    for (i = 0; i < length; ++i) data[i] = i == 0u ? 0x1Au : 0x80u;
}
bool EPD_ReadCommand(uint8_t command, uint8_t *data, uint8_t length) {
    EPD_WriteCmd(command);
    EPD_ReadData(data, length);
    return true;
}
uint8_t EPD_ReadByte(void) { event('r', 0x1Au); return 0x1Au; }
void EPD_Reset(bool status, uint16_t duration) {
    event('H', status); event('h', duration);
}
void EPD_ResetPulse(uint16_t before, uint16_t low, uint16_t after) {
    event('A', before); event('B', low); event('a', after);
}
void EPD_ResetDualPulse(uint16_t duration) { event('X', duration); }
bool EPD_ReadBusy(void) { event('b', 1u); return true; }
void EPD_WaitBusy(bool status, uint16_t timeout) {
    event('W', status); event('w', timeout);
}
bool EPD_WaitBusyEdge(bool status, uint16_t assert_ms, uint16_t release_ms) {
    event('E', status); event('e', assert_ms); event('F', release_ms);
    return true;
}
bool EPD_AbortRequested(void) { return false; }
void EPD_BusyDiagReset(void) { event('!', 0u); }
void delay(uint32_t ms) { event('T', ms); }

typedef struct {
    const char *name;
    uint8_t driver, recipe, ic, color;
    uint16_t width, height;
    uint32_t dialog_hash, sync_hash;
} recipe_case_t;

static const recipe_case_t s_cases[] = {
    {"JD79661 standard", EPD_SCREEN_DRIVER_JD79661, 0, DRV_IC_JD79XXX_ZKC,
     COLOR_BWRY, 128, 250, 0x992E2FC1u, 0x992E2FC1u},
    {"ZKC21B B2", EPD_SCREEN_DRIVER_JD79661,
     INKTAG_RECIPE_JD79661_ZKC21B_FACTORY_B2, DRV_IC_JD79XXX_ZKC,
     COLOR_BWRY, 128, 250, 0xFF2B974Eu, 0xFF2B974Eu},
    {"JD79665AA standard", EPD_SCREEN_DRIVER_JD79665AA, 0,
     DRV_IC_JD79665, COLOR_BWRY, 768, 552, 0x73143848u, 0x73143848u},
    {"Huawei A0", EPD_SCREEN_DRIVER_JD79665AA,
     INKTAG_RECIPE_JD79665AA_HUAWEI_398_A0, DRV_IC_JD79665,
     COLOR_BWRY, 768, 552, 0x1DE74B00u, 0x85F7F716u},
    {"Huawei A1", EPD_SCREEN_DRIVER_JD79665AA,
     INKTAG_RECIPE_JD79665AA_HUAWEI_398_A1, DRV_IC_JD79665,
     COLOR_BWRY, 768, 552, 0x965F4F8Eu, 0x5BC61DCFu},
#if INKTAG_EPD_ENABLE_JD79665CA
    {"Lenmory E6", EPD_SCREEN_DRIVER_JD79665CA,
     INKTAG_RECIPE_JD79665CA_LENMORY_H364F067_E6, DRV_IC_JD79668,
     COLOR_BWRYGB, 760, 568, 0xD4F784A9u, 0xD4F784A9u},
#endif
    {"JD79668AA standard BWR", EPD_SCREEN_DRIVER_JD79668AA, 0,
     DRV_IC_JD79668, COLOR_BWR, 400, 300, 0x9AF692A7u, 0x9AF692A7u},
    {"JD79668AA standard BWRY", EPD_SCREEN_DRIVER_JD79668AA, 0,
     DRV_IC_JD79668, COLOR_BWRY, 400, 300, 0x9AF692A7u, 0x9AF692A7u},
    {"Hanshow 420 BWR", EPD_SCREEN_DRIVER_JD79668AA,
     INKTAG_RECIPE_JD79668AA_HANSHOW_NEBULAR_420R_N, DRV_IC_JD79668,
     COLOR_BWR, 400, 300, 0xC64B62A5u, 0xC64B62A5u},
    {"Hanshow 420 BWRY", EPD_SCREEN_DRIVER_JD79668AA,
     INKTAG_RECIPE_JD79668AA_HANSHOW_NEBULAR_420R_N, DRV_IC_JD79668,
     COLOR_BWRY, 400, 300, 0xC64B62A5u, 0xC64B62A5u},
    {"ZKC42B B3", EPD_SCREEN_DRIVER_JD79668AA,
     INKTAG_RECIPE_JD79668AA_ZKC42B_FACTORY_B3, DRV_IC_JD79668,
     COLOR_BWRY, 400, 300, 0x4770713Du, 0x4770713Du},
    {"JD79XXX standard BWR", EPD_SCREEN_DRIVER_JD79XXX, 0,
     DRV_IC_JD79XXX_ZKC, COLOR_BWR, 128, 250, 0x992E2FC1u, 0x992E2FC1u},
    {"JD79XXX standard BWRY", EPD_SCREEN_DRIVER_JD79XXX, 0,
     DRV_IC_JD79XXX_ZKC, COLOR_BWRY, 128, 250, 0x992E2FC1u, 0x992E2FC1u},
};

static void exercise(const recipe_case_t *item) {
    epd_model_t epd;
    uint8_t strip[768]; /* Four complete rows at the widest tested pitch. */
    uint16_t i, pitch = (item->width + 3u) / 4u;
    uint32_t offset;
    memset(&epd, 0, sizeof(epd));
    epd.driver_id = item->driver; epd.panel_recipe = item->recipe;
    epd.ic = item->ic; epd.color = item->color;
    epd.drv = &epd_drv_jd79xxx;
    epd.width = epd.visible_width = item->width;
    epd.height = epd.visible_height = item->height;
    epd.transfer_format = item->color == COLOR_BWRYGB ?
        EPD_SCREEN_TRANSFER_INDEXED_2X2BPP : EPD_SCREEN_TRANSFER_INDEXED_2BPP;
    for (i = 0; i < sizeof(strip); ++i) strip[i] = (uint8_t)(i * 37u + 11u);
    s_hash = 2166136261u; s_events = 0u;
    epd.drv->init(&epd);
    epd.drv->clear(&epd, false);
    epd.drv->write_image(&epd, strip, NULL, 0u, 0u, epd.width, 4u);
    /* Odd transport chunks cross row boundaries and cover A0's two halves. */
    for (offset = 0u; offset < (uint32_t)pitch * epd.height;) {
        uint32_t remaining = (uint32_t)pitch * epd.height - offset;
        uint8_t chunk = remaining > 127u ? 127u : (uint8_t)remaining;
        assert(EPD_DriverWriteRam(&epd, offset == 0u, true, strip, chunk));
        offset += chunk;
    }
    if (item->color == COLOR_BWRYGB) {
        assert(EPD_DriverWriteRam(&epd, true, false, strip, 127u));
    }
    epd.drv->refresh(&epd);
    assert(EPD_DriverOperationOK(&epd));
    (void)epd.drv->read_temp(&epd);
    epd.drv->sleep(&epd);
    printf("%s %08X %u\n", item->name, s_hash, s_events);
#ifndef INKTAG_JD_MATRIX_CAPTURE_BASELINE
#if defined(INKTAG_DIALOG)
    assert(s_hash == item->dialog_hash);
#else
    assert(s_hash == item->sync_hash);
#endif
#endif
}

int main(void) {
    size_t i;
    for (i = 0; i < sizeof(s_cases) / sizeof(s_cases[0]); ++i)
        exercise(&s_cases[i]);
    /* The 800x480 GSST exception changes only GSST. Frozen original recipe
     * (pre-compaction cd83ed4a^): E7/PWS/E9/PLL must follow either branch. */
    epd_model_t epd;
    memset(&epd, 0, sizeof(epd));
    memset(s_data_count, 0, sizeof(s_data_count));
    epd.driver_id = EPD_SCREEN_DRIVER_JD79665AA;
    epd.ic = DRV_IC_JD79665; epd.color = COLOR_BWRY;
    epd.width = epd.visible_width = 800u;
    epd.height = epd.visible_height = 480u;
    epd.drv = &epd_drv_jd79xxx;
    epd.drv->init(&epd);
    assert(s_data_count[0xE7] == 1u && s_first_data[0xE7] == 0x1Cu);
    assert(s_data_count[0xE3] == 1u && s_first_data[0xE3] == 0u);
    assert(s_data_count[0xE9] == 1u && s_first_data[0xE9] == 1u);
    assert(s_data_count[0x30] == 1u && s_first_data[0x30] == 8u);
    puts("JD79xxx existing-recipe matrix passed");
    return 0;
}
