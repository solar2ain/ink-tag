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


extern const epd_driver_t epd_drv_ssd1xxx;
void EPD_FillRAM(uint8_t cmd,uint8_t value,uint32_t bytes) {EPD_WriteCmd(cmd);event('Z',bytes);event('z',value);}
static void exercise(void) {
 const uint8_t chips[]={DRV_IC_SSD1619,DRV_IC_SSD1680,DRV_IC_SSD1677};
 const uint8_t ids[]={EPD_SCREEN_DRIVER_SSD1619,EPD_SCREEN_DRIVER_SSD1680,EPD_SCREEN_DRIVER_SSD1677};
 const uint16_t sizes[][2]={{128,250},{152,152},{400,300},{800,480}};
 uint8_t black[4]={0x81,0x42,0x24,0x18},red[4]={0x18,0x24,0x42,0x81};
 for(unsigned c=0;c<3;c++)for(unsigned color=COLOR_BW;color<=COLOR_BWR;color++)
 for(unsigned n=0;n<4;n++)for(unsigned variant=0;variant<4;variant++) {
  epd_model_t p={0};p.ic=chips[c];p.driver_id=ids[c];p.color=color;
  p.width=p.visible_width=sizes[n][0];p.height=p.visible_height=sizes[n][1];p.drv=&epd_drv_ssd1xxx;
  if(c==0&&n==0)p.panel_recipe=INKTAG_RECIPE_SSD1619_HEMA_213_BWR;
  p.drv->init(&p);p.drv->clear(&p,false);
  p.drv->write_image(&p,variant&1?NULL:black,variant&2?NULL:red,0,0,16,2);
  p.drv->write_ram(&p,true,true,black,4);p.drv->write_ram(&p,true,false,red,4);
  (void)p.drv->read_temp(&p);p.drv->refresh(&p);p.drv->sleep(&p);
 }
}
int main(void){s_hash=2166136261u;s_events=0;exercise();printf("%08X %u\n",s_hash,s_events);
#define EXPECT_HASH 0xA1D78F75u
#define EXPECT_EVENTS 12076u
#ifdef EXPECT_HASH
 assert(s_hash==EXPECT_HASH && s_events==EXPECT_EVENTS);
#endif
return 0;}
