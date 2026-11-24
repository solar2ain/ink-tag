"""Run production Nordic diagnostic backend/dispatch with deterministic IO.

Only the ARM fault entry is excluded; rail, ADC, SoftDevice and record IO are
mock boundaries. No test copy of the power policy, CRC, ring or wire encoder.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PORT = ROOT / 'compat/nordic/src'
health = (PORT / 'inktag_nrf_health.c').read_text()
health = health[:health.index('/* The naked handler')]
health = re.sub(r'^#include "(?:inktag_epd_driver|nrf|nrf_error|nrf_soc)\.h"\n',
                '', health, flags=re.M)
protocol = (PORT / 'inktag_nrf_protocol_v2.inc').read_text()


def function(name):
    found = re.search(r'^static [^\n]+\b' + name +
                      r'\([^;{]*\)\s*\{[\s\S]*?\n}', protocol, re.M)
    assert found, name
    return found.group()


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "inktag_nrf_health.h"
#include "inktag_nrf_record_store.h"
#include "inktag_protocol_core.h"
#include "inktag_temperature_wire.h"
#include "inktag_board.h"
#define NRF_SUCCESS 0u
#define POWER_RESETREAS_DOG_Msk 2u
#define __DMB() ((void)0)
typedef uint32_t ret_code_t;
static struct { uint32_t CFSR, HFSR; } scb;
#define SCB (&scb)
static uint16_t voltage=3300;
static int32_t temperature=100;
static ret_code_t temp_error;
static unsigned adc_reads, record_writes;
static uint8_t disk[160];
static uint16_t disk_length;
static bool disk_present, write_ok=true, read_ok=true;
static uint16_t EPD_ReadVoltage(void) { adc_reads++; return voltage; }
static ret_code_t sd_temp_get(int32_t *value) {
    *value=temperature; return temp_error;
}
#define SSD1xxx_TSENSOR_READ 0x1bu
#define JD79xxx_TSC 0x40u
static uint8_t panel_raw[2] = {0xffu, 0xffu};
static bool panel_read_ok = true;
static unsigned panel_reads;
bool EPD_ReadCommand(uint8_t command, uint8_t *out, uint8_t length) {
    assert((command == 0x1bu || command == 0x40u) && length == 2u);
    panel_reads++;
    if (panel_read_ok) memcpy(out, panel_raw, length);
    return panel_read_ok;
}
bool inktag_nrf_record_store_init(void) { return true; }
bool inktag_nrf_record_store_read(inktag_nrf_record_kind_t kind,
        void *out, uint16_t capacity, uint16_t *length) {
    assert(kind==INKTAG_NRF_RECORD_HEALTH);
    if(!disk_present || !read_ok) return false;
    assert(disk_length<=capacity); memcpy(out,disk,disk_length);
    *length=disk_length; return true;
}
bool inktag_nrf_record_store_write(inktag_nrf_record_kind_t kind,
        const void *data, uint16_t length) {
    assert(kind==INKTAG_NRF_RECORD_HEALTH && length<=sizeof(disk));
    record_writes++;
    if(!write_ok) return false;
    memcpy(disk,data,length);disk_length=length;disk_present=true;return true;
}
bool inktag_nrf_record_store_pending(inktag_nrf_record_kind_t kind) {
    assert(kind==INKTAG_NRF_RECORD_HEALTH);return false;
}
typedef struct { int unused; } ble_epd_t;
static uint8_t response[56], response_status;
static uint16_t response_length;
static void epd_v2_send_response(ble_epd_t *p,uint8_t op,uint8_t tx,
        inktag_status_v2_t status,const uint8_t *data,uint16_t length) {
    (void)p; assert((op==INKTAG_CMD_GET_HEALTH || op==INKTAG_CMD_GET_STATUS) && tx==17);
    assert(length<=sizeof(response));response_length=length;response_status=status;
    if(length) memcpy(response,data,length);
}
#define epd_v2_reply_status(p,o,t,s) epd_v2_send_response(p,o,t,s,NULL,0u)

#define INKTAG_NRF_DEVICE_NAME_WRITABLE 0
#define INKTAG_NRF_TEST_ENABLED 1
#define INKTAG_NRF_IMAGE_STORE_ENABLED 1
#define INKTAG_NRF_ACTIVATION_ENABLED 0
#define INKTAG_NRF_INDICATOR_ENABLED 1
#define INKTAG_NRF_FIND_ENABLED 0
#define INKTAG_NRF_SIMPLE_IMAGE_METADATA 1
#define INKTAG_NRF_NATIVE_IMAGE_METADATA 0
#define INKTAG_NRF_CAPABILITY_IMAGE_METADATA UINT32_C(0x10000000)
static bool image_available=true;
static bool inktag_image_store_supported(void) { return image_available; }
static bool m_epd_operation_active, m_epd_operation_pending, m_slideshow_active;
static uint8_t m_slideshow_current_id = 3u;
static uint32_t m_slideshow_interval = 3600u;
static int16_t m_v2_timezone_minutes = 480;
static struct {
    bool active; uint32_t written; uint8_t transaction_id;
    struct { uint32_t total_size; } descriptor;
} m_v2_transfer;
static struct { uint8_t origin, v2_opcode, v2_transaction; }
    m_epd_active_operation, m_epd_pending_operation;
#define INKTAG_NRF_V2_OPERATION_BLE 1u
static uint8_t epd_v2_current_display_mode(void) { return 1u; }
'''

TEST = r'''
static void test_status_sources(void) {
    int16_t expected_panel[] = {360, -4};
    epd_screen_config_t screen = {
        .driver_id = EPD_SCREEN_DRIVER_SSD1619,
        .panel_recipe = INKTAG_RECIPE_SSD1619_HEMA_213_BWR,
        .color_id = EPD_SCREEN_COLOR_BWR, .width = 128u, .height = 250u,
    };
    inktag_nrf_health_init(0u, 0u);
    inktag_nrf_health_process(2000u);
    temperature = 100; temp_error = 0; panel_read_ok = true;
    for (unsigned i = 0; i < 2u; ++i) {
        panel_raw[0] = i ? 0xffu : 22u;
        panel_raw[1] = i ? 0xc0u : 0x80u;
        inktag_nrf_health_panel_temperature_sample(&screen);
        unsigned reads = panel_reads;
        epd_v2_send_status(NULL, INKTAG_CMD_GET_STATUS, 17u);
        assert(response_status == INKTAG_STATUS_OK && response_length == 25u);
        assert(response[0] == 1u && response[1] == 1u && response[2] == 0x0cu);
        assert((int8_t)response[7] == (i ? -1 : 22));
        assert(!memcmp(response + 16u, "ITT\1", 4u) && response[20] == 3u);
        assert(response[21] == 0x90u && response[22] == 1u); /* MCU 25 C */
        assert(response[23] == (uint8_t)expected_panel[i]);
        assert(response[24] == (uint8_t)((uint16_t)expected_panel[i] >> 8u));
        assert(panel_reads == reads); /* GET_STATUS only consumes the cache. */
    }
    inktag_nrf_health_process(s_now_ms + PANEL_TEMPERATURE_MAX_AGE_MS + 1u);
    epd_v2_send_status(NULL, INKTAG_CMD_GET_STATUS, 17u);
    assert(response[20] == 1u && (int8_t)response[7] == 25);
    assert(response[23] == 0u && response[24] == 0u);
    temp_error = 7u;
    epd_v2_send_status(NULL, INKTAG_CMD_GET_STATUS, 17u);
    assert(response[2] == 4u && (int8_t)response[7] == -127 && response[20] == 0u);
    assert(response[21] == 0u && response[22] == 0u);
    assert(response[23] == 0u && response[24] == 0u);
    temp_error = 0;
}
static void test_panel_cache(void) {
    epd_screen_config_t screen = {
        .driver_id = EPD_SCREEN_DRIVER_SSD1619,
        .panel_recipe = INKTAG_RECIPE_SSD1619_HEMA_213_BWR,
        .color_id = EPD_SCREEN_COLOR_BWR, .width = 128u, .height = 250u,
    };
    int16_t value = 999;
    int8_t effective = 99;
    unsigned before = panel_reads;
    inktag_nrf_health_panel_temperature_sample(NULL);
    assert(!inktag_nrf_health_panel_temperature(&value) && panel_reads == before);
    screen.panel_recipe = 0xfeu;
    inktag_nrf_health_panel_temperature_sample(&screen);
    assert(!inktag_nrf_health_panel_temperature(&value) && panel_reads == before);
    screen.panel_recipe = INKTAG_RECIPE_SSD1619_HEMA_213_BWR;
    inktag_nrf_health_process(UINT32_MAX - 1000u);
    panel_raw[0] = 22u; panel_raw[1] = 0x80u;
    inktag_nrf_health_panel_temperature_sample(&screen);
    assert(panel_reads == before + 1u);
    assert(inktag_nrf_health_panel_temperature(&value) && value == 360);
    temperature = 100; temp_error = 0;
    inktag_nrf_health_temperature_sample();
    assert(inktag_nrf_health_effective_temperature(&effective) && effective == 22);
    inktag_nrf_health_process(s_now_ms + PANEL_TEMPERATURE_MAX_AGE_MS);
    assert(inktag_nrf_health_panel_temperature(&value));
    inktag_nrf_health_process(s_now_ms + 1u);
    assert(!inktag_nrf_health_panel_temperature(&value));
    assert(inktag_nrf_health_effective_temperature(&effective) && effective == 25);
    inktag_nrf_health_process(s_panel_temperature_sampled_ms);
    assert(!inktag_nrf_health_panel_temperature(&value)); /* No stale revival. */
    assert(panel_reads == before + 1u); /* Queries never transact with the panel. */
    panel_raw[0] = 0xffu; panel_raw[1] = 0xc0u;
    inktag_nrf_health_panel_temperature_sample(&screen);
    assert(inktag_nrf_health_panel_temperature(&value) && value == -4);
    panel_read_ok = false;
    inktag_nrf_health_panel_temperature_sample(&screen);
    assert(!inktag_nrf_health_panel_temperature(&value));
    panel_read_ok = true;
    panel_raw[0] = 0x7fu; panel_raw[1] = 0xf0u;
    inktag_nrf_health_panel_temperature_sample(&screen);
    assert(!inktag_nrf_health_panel_temperature(&value));
    panel_raw[0] = 22u; panel_raw[1] = 1u;
    inktag_nrf_health_panel_temperature_sample(&screen);
    assert(!inktag_nrf_health_panel_temperature(&value));
    screen.driver_id = EPD_SCREEN_DRIVER_JD79665AA;
    screen.panel_recipe = INKTAG_RECIPE_JD79665AA_HUAWEI_398_A1;
    screen.color_id = EPD_SCREEN_COLOR_BWRY;
    screen.width = 768u; screen.height = 552u;
    panel_raw[0] = 23u; panel_raw[1] = 0xc0u;
    inktag_nrf_health_panel_temperature_sample(&screen);
    assert(inktag_nrf_health_panel_temperature(&value) && value == 380);
    inktag_nrf_health_display_begin();
    assert(!inktag_nrf_health_panel_temperature(&value));
    inktag_nrf_health_display_end();
}
static void boot(uint32_t reason) {
    /* Cold-boot volatile state; leave durable store and noinit mailbox. */
    memset(&s_store,0,sizeof(s_store));memset(&s_pending_crash,0,sizeof(s_pending_crash));
    s_pending_crash_valid=s_pending_crash_in_store=s_store_dirty=false;
    s_store_write_pending=s_initialized=s_stable_marked=false;
    inktag_nrf_health_init(reason,0x12345678);
}
static void query(uint8_t index) {
    epd_v2_send_health(NULL,INKTAG_CMD_GET_HEALTH,17,&index,1);
}
static uint16_t u16(const uint8_t *p) { return p[0]|(uint16_t)p[1]<<8; }
static uint32_t old_crc(const void *input,unsigned n) {
    uint32_t c=UINT32_MAX; const uint8_t *p=input;
    while(n--) { c^=*p++; for(unsigned b=0;b<8;b++)
        c=(c>>1)^(UINT32_C(0xEDB88320)&(0u-(c&1u))); }
    return c^UINT32_MAX;
}
int main(void) {
    voltage=0;assert(!s_initialized);
    assert(inktag_nrf_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,true));
    assert(inktag_nrf_health_operation_allowed(INKTAG_POWER_OPERATION_OTA,true));
    assert(!inktag_nrf_health_operation_allowed((inktag_power_operation_t)99,true));
    voltage=2399;assert(!inktag_nrf_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,true));
    voltage=3300;
    test_status_sources();
    test_panel_cache();
    assert(INKTAG_BOARD_HEALTH_QUALIFIED==0 && INKTAG_BOARD_POWER_THRESHOLDS_QUALIFIED==0);
    uint32_t caps=epd_v2_capabilities();
    assert((caps&(INKTAG_CAPABILITY_HEALTH|INKTAG_CAPABILITY_TELEMETRY))==
        (INKTAG_CAPABILITY_HEALTH|INKTAG_CAPABILITY_TELEMETRY));
    assert((caps & (INKTAG_CAPABILITY_LIFECYCLE|INKTAG_CAPABILITY_LIFECYCLE_V2)) ==
                  (INKTAG_CAPABILITY_LIFECYCLE|INKTAG_CAPABILITY_LIFECYCLE_V2));
    assert(!(caps&(INKTAG_CAPABILITY_INPUT|
                   INKTAG_CAPABILITY_ACTIVATION|INKTAG_CAPABILITY_FIND)));
    image_available=false;
    assert(!(epd_v2_capabilities()&(INKTAG_CAPABILITY_RESOURCES|INKTAG_CAPABILITY_SLIDESHOW)));
    image_available=true;boot(1);query(0);
    assert(response_status==INKTAG_STATUS_OK && response_length==56);
    assert(response[0]==1 && response[1]==INKTAG_POWER_NORMAL && response[2]==0x9f);
    assert(response[3]==1 && u16(response+4)==3300 && response[20]==0);
    assert(u16(response+6)==2400 && u16(response+8)==0 && u16(response+10)==2400);
    assert(u16(response+12)==2600 && u16(response+14)==150);
    unsigned reads=adc_reads;
    for(unsigned i=0;i<20;i++) query(0);
    assert(adc_reads==reads && record_writes==0); /* Read-only, no new poll timer. */
    query(1);assert(response_status==INKTAG_STATUS_NOT_FOUND && response_length==0);
    uint8_t bad[2]={0,0};
    epd_v2_send_health(NULL,INKTAG_CMD_GET_HEALTH,17,bad,2);
    assert(response_status==INKTAG_STATUS_INVALID_REQUEST && adc_reads==reads);
    assert(inktag_nrf_health_encode(0,NULL,56)==0);
    assert(inktag_nrf_health_encode(0,response,55)==0);
    for(unsigned mv=0;mv<=6001;mv++) {
        voltage=mv;boot(0);query(0);
        bool valid=mv>=1500 && mv<=6000;
        assert(!!(response[2]&1)==valid && u16(response+4)==(valid?mv:0));
        assert(response[2]&2);
        assert(!!(response[2]&4)==(!valid || mv>=2400));
        assert(!!(response[2]&8)==(!valid || mv>=2600));
        assert(inktag_nrf_health_voltage_valid()==valid);
        if(!valid) assert(response[1]==INKTAG_POWER_UNKNOWN);
        assert(inktag_nrf_health_operation_allowed(INKTAG_POWER_OPERATION_DISPLAY,false));
        assert(inktag_nrf_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,false)==(!valid || mv>=2400));
        assert(inktag_nrf_health_operation_allowed(INKTAG_POWER_OPERATION_OTA,false)==(!valid || mv>=2600));
        assert(response[2]&0x80);
    }
    voltage=3300;boot(0);
    inktag_nrf_health_display_begin();voltage=1900;
    assert(!inktag_nrf_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,true));
    query(0);assert(u16(response+4)==3300 && response[1]==INKTAG_POWER_NORMAL);
    voltage=0;
    assert(inktag_nrf_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,true));
    assert(inktag_nrf_health_operation_allowed(INKTAG_POWER_OPERATION_OTA,true));
    assert(!inktag_nrf_health_voltage_valid());
    inktag_nrf_health_display_end();voltage=3300;
    int16_t t=999;int8_t effective=99;
    for(int c=-161;c<=341;c++) {
        temperature=c;temp_error=0;inktag_nrf_health_temperature_sample();
        bool valid=c>=-160 && c<=340;
        assert(inktag_nrf_health_mcu_temperature(&t)==valid);
        assert(inktag_nrf_health_effective_temperature(&effective)==valid);
        if(valid) assert(t==c*4);
        assert(!inktag_nrf_health_panel_temperature(&t));
    }
    temperature=100;inktag_nrf_health_temperature_sample();
    temp_error=1;inktag_nrf_health_temperature_sample();
    assert(!inktag_nrf_health_mcu_temperature(&t));
    temp_error=0;temperature=100;
    /* CRC consolidation keeps existing store/mailbox bytes identical. */
    uint8_t block[160];uint32_t random=42;
    for(unsigned n=0;n<=sizeof(block);n++) {
        for(unsigned j=0;j<n;j++) { random=random*1664525u+1013904223u;block[j]=random>>24; }
        assert(crc32_for(block,n)==old_crc(block,n));
    }
    disk_present=false;memset((void*)&s_mailbox,0,sizeof(s_mailbox));
    for(unsigned crash=1;crash<=6;crash++) {
        boot(0);inktag_nrf_health_process(crash*100);
        inktag_nrf_health_stage(0x1000+crash,INKTAG_NRF_HEALTH_OPERATION_TRANSFER);
        inktag_nrf_health_watchdog_capture();assert(mailbox_valid());
        boot(POWER_RESETREAS_DOG_Msk);write_ok=false;
        inktag_nrf_health_process(100);inktag_nrf_health_process(200);
        assert(s_store.total_crashes==crash && mailbox_valid());
        write_ok=true;inktag_nrf_health_process(60099);
        assert(s_store_retry_pending && !s_store_write_pending);
        inktag_nrf_health_process(60100);
        inktag_nrf_health_record_complete(false);inktag_nrf_health_process(60101);
        assert(s_store_retry_pending && !s_store_write_pending);
        inktag_nrf_health_process(121100);
        assert(s_store.total_crashes==crash && mailbox_valid());
        inktag_nrf_health_record_complete(true);assert(!mailbox_valid());
        query(0);assert(response_status==INKTAG_STATUS_OK && response[23]==HEALTH_CRASH_WATCHDOG);
        assert(response[25]==INKTAG_NRF_HEALTH_OPERATION_TRANSFER);
        assert(inktag_read_u32_le(response+36)==0x1000+crash);
        assert(response[20]==(crash<4?crash:4));
        query(response[20]);assert(response_status==INKTAG_STATUS_NOT_FOUND);
    }
    for(unsigned index=0;index<4;index++) {
        query(index);assert(response_status==INKTAG_STATUS_OK);
        assert(inktag_read_u32_le(response+36)==0x1006-index);
    }
    inktag_nrf_health_process(60000);inktag_nrf_health_record_complete(true);
    boot(0);assert(s_store.total_crashes==6 && s_store.consecutive_crashes==0);
    disk[0]^=1;boot(0);query(0);assert(response[20]==0);
    puts("PASS production diagnostics capabilities, voltage/temperature validity, no rail IO, ring persistence/failure and wire errors");
}
'''

with tempfile.TemporaryDirectory() as directory:
    file = Path(directory) / 'health.c'
    binary = Path(directory) / 'health'
    file.write_text(PREFIX + health + '\n' + function('epd_v2_capabilities') +
                    '\n' + function('epd_v2_send_health') +
                    '\n' + function('epd_v2_send_status') + TEST)
    flags=['-O2'] if not os.environ.get('INKTAG_TEST_SANITIZERS') else ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=all','-no-pie']
    subprocess.run([os.environ.get('CC', 'gcc'), '-std=c99', *flags, '-Wall',
                    '-Wextra', '-Werror', '-DNRF52811_XXAA',
                    '-Ifirmware/boards', '-Ifirmware/ports/nordic/common/config', '-Ifirmware/ports/nordic/common/src',
                    '-Ifirmware/common/power', '-Ifirmware/common/protocol',
                    '-Ifirmware/common/epd', '-Ifirmware/common/status',
                    '-Ifirmware/common/health',
                    str(file), 'compat/common/power/inktag_power.c',
                    'compat/common/status/inktag_status.c',
                    'compat/common/protocol/inktag_protocol_core.c',
                    '-o', str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], check=True)
