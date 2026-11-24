#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "inktag_epd_driver.h"

/* DA compact backport: deterministic init/sleep bus traces, including data
 * call lengths, reset/wait/read/delay events. Expected digest is frozen from
 * DA f03f960, not regenerated from the optimized implementation. This is a
 * host recipe regression, not physical panel qualification. */
static uint32_t s_hash;
static uint32_t s_events;

static void event(uint8_t kind, uint32_t value) {
    uint8_t i;
    s_hash = (s_hash ^ kind) * 16777619u;
    for (i = 0; i < 4u; ++i) {
        s_hash = (s_hash ^ (uint8_t)value) * 16777619u;
        value >>= 8u;
    }
    ++s_events;
}

void EPD_WriteCmd(uint8_t value) { event('C', value); }
void EPD_WriteByte(uint8_t value) { event('D', value); }
void EPD_WriteData(uint8_t *data, uint8_t length) {
    uint8_t i;
    event('L', length);
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
static bool test_busy;
bool EPD_ReadBusy(void) { event('b', test_busy); return test_busy; }
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


extern const epd_driver_t epd_drv_uc8xxx;
void app_epd_trace(const char *tag) {
    while (*tag) event('s', (uint8_t)*tag++);
}
void app_feed_wdt(void) { event('f', 0); }
void EPD_FillRAM(uint8_t command,uint8_t value,uint32_t bytes) {
    EPD_WriteCmd(command); event('Z', bytes); event('z', value);
}
int main(void) {
    uint32_t digest = 2166136261u;
    unsigned cases = 0;
    const uint8_t chips[]={DRV_IC_UC8151,DRV_IC_UC8176,DRV_IC_UC8179,DRV_IC_UC8159};
    const uint16_t sizes[][2]={{128,250},{400,300},{800,480},{296,128},{760,568}};
    for(unsigned chip=0;chip<sizeof(chips);++chip)
    for(unsigned color=COLOR_BW;color<=COLOR_BWRY;++color)
    for(unsigned size=0;size<5;++size)
    for(unsigned recipe=0;recipe<8;++recipe)
    for(unsigned busy=0;busy<2;++busy) {
        epd_model_t epd={0}; epd.ic=chips[chip]; epd.color=color;
        epd.width=epd.visible_width=sizes[size][0];
        epd.height=epd.visible_height=sizes[size][1];
        epd.panel_recipe=recipe; epd.driver_id=EPD_SCREEN_DRIVER_UC8176;
        epd.drv=&epd_drv_uc8xxx; s_hash=2166136261u; s_events=0; test_busy=busy!=0;
        epd.drv->init(&epd); epd.drv->sleep(&epd);
        digest = (digest ^ s_hash) * 16777619u;
        digest = (digest ^ s_events) * 16777619u;
        ++cases;
    }
    printf("UC init/sleep cases=%u digest=%08X\n", cases, digest);
    assert(cases == 960u);
    assert(digest == 0x5B67F5C5u);
    return 0;
}
