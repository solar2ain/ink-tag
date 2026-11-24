"""Run full shipping health, real record-store, and the actual fresh program gate.

Only the ARM fault entry, config persistence and HAL are host boundaries.
This is software retry/ACK evidence, not a physical NOR or watchdog test.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PORT = ROOT / 'compat/nordic/src'
health = (PORT / 'inktag_nrf_health.c').read_text(encoding='utf-8')
health = health[:health.index('/* The naked handler')]
health = re.sub(r'^#include "(?:inktag_epd_driver|nrf|nrf_error|nrf_soc)\.h"\n', '', health, flags=re.M)
flash = (PORT / 'inktag_nrf_external_flash.c').read_text(encoding='utf-8')
config = (PORT / 'inktag_nrf_config_store.c').read_text(encoding='utf-8')
record_gate = re.search(r'static bool current_record_type_valid\([^;{]*\)\s*\{[\s\S]*?\n}', config).group()

def function(name):
    match = re.search(r'^(?:static )?(?:bool|uint32_t) ' + name + r'\([^;{]*\)\s*\{[\s\S]*?\n}', flash, re.M)
    assert match, name
    return match.group()

PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "inktag_nrf_health.h"
#include "inktag_nrf_record_store.h"
#include "inktag_nrf_config_store.h"
#include "inktag_nrf_external_flash.h"
#include "inktag_protocol_core.h"
#include "inktag_board.h"
#include "nrf_error.h"
#define SSD1xxx_TSENSOR_READ 0x1bu
#define JD79xxx_TSC 0x40u
bool EPD_ReadCommand(uint8_t command, uint8_t *out, uint8_t length) {
    (void)command; (void)out; (void)length; return false;
}
#define POWER_RESETREAS_DOG_Msk 2u
#define __DMB() ((void)0)
#define FLASH_PAGE_SIZE 256u
#define FLASH_PROGRAM_POLL_LIMIT 2000u
#define FLASH_SECTOR_SIZE 4096u
#define FLASH_ERASE_POLL_LIMIT 6000u
typedef uint32_t ret_code_t;
static struct { uint32_t CFSR, HFSR; } scb;
#define SCB (&scb)
static uint16_t voltage, fresh_voltage;
static unsigned adc_reads, config_attempts, commands, erases, waits;
static unsigned failure;
static uint8_t spi_payload[160], disk[160], s_session_depth;
static uint16_t spi_length, disk_length;
static bool disk_present;
static uint32_t s_last_error, config_error;
static uint16_t EPD_ReadVoltage(void) { adc_reads++; return voltage; }
static ret_code_t sd_temp_get(int32_t *value) { *value=100; return NRF_SUCCESS; }
static bool wait_ready(uint32_t limit) {
    assert(limit==2000u || limit==6000u); waits++;
    if ((failure==1 && waits==1) || (failure==3 && waits==2)) {
        s_last_error=NRF_ERROR_INTERNAL; return false;
    }
    return true;
}
static bool write_enable(void) {
    if(failure==2) { s_last_error=NRF_ERROR_INTERNAL;return false; }
    return true;
}
static void chip_select(bool active) { (void)active; }
static void send_address(uint8_t command,uint32_t address) {
    assert((command==0x02 || command==0x20) && address==0x2000);
    if(command==0x02) commands++; else erases++;
    spi_length=0;
}
static uint8_t spi_byte(uint8_t value) {
    assert(spi_length<sizeof(spi_payload));spi_payload[spi_length++]=value;return 0;
}
'''

BOUNDARY = r'''
bool inktag_nrf_config_store_init(void) { config_error=NRF_SUCCESS;return true; }
bool inktag_nrf_config_store_read(inktag_nrf_config_record_type_t type,
        void *data,uint16_t capacity,uint16_t *length) {
    assert(type==INKTAG_NRF_CONFIG_HEALTH);
    if(!disk_present) { config_error=NRF_ERROR_NOT_FOUND;return false; }
    assert(disk_length<=capacity);memcpy(data,disk,disk_length);*length=disk_length;return true;
}
bool inktag_nrf_config_store_write(inktag_nrf_config_record_type_t type,
        const void *data,uint16_t length) {
    assert(type==INKTAG_NRF_CONFIG_HEALTH && length==148u);config_attempts++;
    waits=0;voltage=fresh_voltage;s_session_depth=1;
    bool ok=inktag_nrf_external_flash_program(0x2000,data,length);
    s_session_depth=0;config_error=s_last_error;
    if(ok) { assert(spi_length==length);memcpy(disk,spi_payload,length);disk_length=length;disk_present=true; }
    return ok;
}
bool inktag_nrf_config_store_delete(inktag_nrf_config_record_type_t type) { (void)type;assert(!"no delete in retry");return false; }
uint32_t inktag_nrf_config_store_last_error(void) { return config_error; }
static void prepare(bool crash) {
    inktag_nrf_record_event_t event;
    assert(!inktag_nrf_record_store_take_event(&event));
    disk_present=false;voltage=fresh_voltage=3300;failure=0;
    memset(&s_store,0,sizeof(s_store));memset(&s_pending_crash,0,sizeof(s_pending_crash));
    memset((void *)&s_mailbox,0,sizeof(s_mailbox));
    s_pending_crash_valid=s_pending_crash_in_store=s_store_dirty=false;
    s_store_write_pending=s_initialized=s_stable_marked=false;
    /* Make a real checksummed retained crash, then cold-boot the health adapter. */
    if(crash) { inktag_nrf_health_init(0,0x1111);inktag_nrf_health_stage(0x12345678,INKTAG_NRF_HEALTH_OPERATION_TRANSFER);inktag_nrf_health_watchdog_capture();assert(mailbox_valid()); }
    inktag_nrf_health_init(crash?POWER_RESETREAS_DOG_Msk:0,0x2222);
    assert(!s_store_retry_pending && s_last_store_attempt_ms==0);
    /* Keep rare-event bytes fixed while separately checking stable-up coalescing. */
    s_stable_marked=true;
    if(!crash) s_store_dirty=true;
    config_attempts=commands=adc_reads=0;
}
static void pending_same(const health_store_t *staged,bool crash) {
    assert(s_store_dirty && s_store_retry_pending && !s_store_write_pending);
    assert(s_pending_crash_valid==crash && s_pending_crash_in_store==crash);
    assert(mailbox_valid()==crash);
    assert(!memcmp(&s_store,staged,sizeof(*staged)));
    assert(!inktag_nrf_record_store_pending(INKTAG_NRF_RECORD_HEALTH));
}
static void ack(void) {
    inktag_nrf_record_event_t event;
    assert(inktag_nrf_record_store_take_event(&event));
    assert(event.kind==INKTAG_NRF_RECORD_HEALTH && event.success && !event.deleted);
    inktag_nrf_health_record_complete(event.success);
}
int main(void) {
    /* Absent record kinds must never fall through to the health backend. */
    assert(inktag_nrf_record_store_init());
    for (unsigned kind = 0u; kind <= 255u; ++kind) {
        if (kind == INKTAG_NRF_RECORD_HEALTH) continue;
        uint8_t data[160] = {0x5a}; uint16_t length = 123u;
        assert(!inktag_nrf_record_store_read((inktag_nrf_record_kind_t)kind,
                                            data, sizeof(data), &length));
        assert(length == 123u && data[0] == 0x5a);
        assert(!inktag_nrf_record_store_write((inktag_nrf_record_kind_t)kind,
                                             data, sizeof(data)));
        assert(!inktag_nrf_record_store_delete((inktag_nrf_record_kind_t)kind));
        assert(!inktag_nrf_record_store_pending((inktag_nrf_record_kind_t)kind));
        assert(inktag_nrf_record_store_last_error() == NRF_ERROR_INVALID_PARAM);
    }
    for (unsigned type = 0u; type <= UINT16_MAX; ++type)
        assert(current_record_type_valid(type) ==
               ((type >= 1u && type <= 7u) ||
                (type >= 10u && type <= 12u) || type == 32u));
    assert(!config_attempts);
    assert(sizeof(health_store_t)==148 && INKTAG_HEALTH_STORE_RETRY_INTERVAL_MS==60000u);
    for(unsigned fail=1;fail<=3;fail++) for(unsigned crash=0;crash<2;crash++) {
        prepare(crash);failure=fail;
        inktag_nrf_health_process(1000);
        assert(config_attempts==1 && commands==(fail==3));
        assert(s_store.total_crashes==crash && s_store.record_count==crash);
        health_store_t staged=s_store;pending_same(&staged,crash);
        for(uint32_t now=1001;now<61000;now++) inktag_nrf_health_process(now);
        assert(config_attempts==1);pending_same(&staged,crash);
        inktag_nrf_health_process(61000);assert(config_attempts==2);pending_same(&staged,crash);
        failure=0;
        inktag_nrf_health_process(120999);assert(config_attempts==2);pending_same(&staged,crash);
        inktag_nrf_health_process(121000);assert(config_attempts==3 && s_store_write_pending && !s_store_dirty);
        assert(s_pending_crash_valid==crash && mailbox_valid()==crash && s_store_retry_pending);
        assert(inktag_nrf_record_store_pending(INKTAG_NRF_RECORD_HEALTH));
        /* Commit event not yet consumed: no repeat even after the interval. */
        inktag_nrf_health_process(250000);assert(config_attempts==3 && mailbox_valid()==crash);
        ack();assert(!s_store_retry_pending && !s_pending_crash_valid && !s_pending_crash_in_store && !mailbox_valid());
        assert(disk_length==sizeof(staged) && !memcmp(disk,&staged,sizeof(staged)));
        for(uint32_t now=250001;now<310001;now++) inktag_nrf_health_process(now);
        assert(config_attempts==3);
    }
    /* 32-bit monotonic wrap, exact floor and one late service opportunity. */
    prepare(true);failure=3;uint32_t start=UINT32_MAX-15u;
    inktag_nrf_health_process(start);health_store_t staged=s_store;
    failure=0;inktag_nrf_health_process(start+59999u);assert(config_attempts==1);pending_same(&staged,true);
    inktag_nrf_health_process(start+60000u);assert(config_attempts==2);ack();
    prepare(true);failure=1;inktag_nrf_health_process(1000);
    failure=0;assert(config_attempts==1); /* no autonomous expiry work */
    inktag_nrf_health_process(500000);assert(config_attempts==2);ack();
    /* Valid low voltage still refuses at cached and fresh physical gates. */
    const uint16_t rejected[]={1500,1999,2300,2399};
    for(unsigned n=0;n<sizeof(rejected)/sizeof(rejected[0]);n++) {
        prepare(true);voltage=rejected[n];(void)inktag_nrf_health_voltage_mv(true);
        inktag_nrf_health_process(1000);assert(config_attempts==0 && commands==0 && mailbox_valid());
        assert(s_store.total_crashes==1 && s_pending_crash_in_store);
        voltage=3300;(void)inktag_nrf_health_voltage_mv(true);
        inktag_nrf_health_process(1001);assert(config_attempts==1);ack();
        /* Cached healthy, rail falls at the actual production Page Program gate. */
        prepare(true);fresh_voltage=rejected[n];
        inktag_nrf_health_process(1000);assert(config_attempts==1 && commands==0 && waits==0);
        assert(inktag_nrf_record_store_last_error()==NRF_ERROR_FORBIDDEN);
        staged=s_store;pending_same(&staged,true);
        voltage=fresh_voltage=3300;
        inktag_nrf_health_process(60999);assert(config_attempts==1);pending_same(&staged,true);
        inktag_nrf_health_process(61000);assert(config_attempts==2 && commands==1);ack();
    }
    /* Unavailable ADC is UNKNOWN, not a write veto at admission or at the
     * real Page Program. No recovery wait/retry loop is required. */
    const uint16_t unavailable[]={0,1499,6001};
    for(unsigned n=0;n<sizeof(unavailable)/sizeof(unavailable[0]);n++) {
        prepare(true);voltage=fresh_voltage=unavailable[n];
        (void)inktag_nrf_health_voltage_mv(true);
        inktag_nrf_health_process(1000);
        assert(config_attempts==1 && commands==1 && s_store_write_pending);
        assert(!inktag_nrf_health_voltage_valid() && inktag_nrf_health_power_state()==INKTAG_POWER_UNKNOWN);
        ack();
        inktag_nrf_health_process(1001);assert(config_attempts==1 && commands==1);
        prepare(true);fresh_voltage=unavailable[n];
        inktag_nrf_health_process(1000);
        assert(config_attempts==1 && commands==1 && s_store_write_pending);
        assert(!inktag_nrf_health_voltage_valid());
        assert(inktag_nrf_record_store_last_error()==NRF_SUCCESS);
        ack();
        inktag_nrf_health_process(61000);assert(config_attempts==1 && commands==1);
    }
    /* Both actual NOR opcodes retain valid 2.40V thresholds, while an
     * unavailable ADC neither denies the write nor becomes NORMAL. */
    prepare(false);uint8_t payload[4]={1,2,3,4};s_session_depth=1;failure=0;
    voltage=2399;unsigned old_programs=commands,old_erases=erases;
    assert(!inktag_nrf_external_flash_program(0x2000,payload,sizeof(payload)));
    assert(!inktag_nrf_external_flash_erase_sector(0x2000));
    assert(commands==old_programs && erases==old_erases);
    voltage=2400;assert(inktag_nrf_external_flash_program(0x2000,payload,sizeof(payload)));
    assert(inktag_nrf_external_flash_erase_sector(0x2000));
    old_programs=commands;old_erases=erases;
    voltage=0;assert(inktag_nrf_external_flash_program(0x2000,payload,sizeof(payload)));
    assert(inktag_nrf_external_flash_erase_sector(0x2000));
    assert(commands==old_programs+1 && erases==old_erases+1);
    assert(!inktag_nrf_health_voltage_valid() && inktag_nrf_health_power_state()==INKTAG_POWER_UNKNOWN);
    assert(!inktag_nrf_external_flash_erase_sector(0x2001));s_session_depth=0;
    assert(!inktag_nrf_external_flash_program(0x2000,payload,sizeof(payload)));
    /* Defensive failed completion after accepted write preserves staged mailbox. */
    prepare(true);inktag_nrf_health_process(1000);
    inktag_nrf_record_event_t event;assert(inktag_nrf_record_store_take_event(&event));
    inktag_nrf_health_record_complete(false);staged=s_store;pending_same(&staged,true);
    inktag_nrf_health_process(60999);assert(config_attempts==1);
    inktag_nrf_health_process(61000);assert(config_attempts==2);ack();
    /* Stable-uptime dirty state coalesces without any periodic successful writes. */
    prepare(false);s_store_dirty=false;s_stable_marked=false;s_store.consecutive_crashes=2;
    inktag_nrf_health_process(59999);assert(config_attempts==0);
    failure=3;inktag_nrf_health_process(60000);assert(config_attempts==1 && s_store.consecutive_crashes==0);
    failure=0;inktag_nrf_health_process(119999);assert(config_attempts==1);
    inktag_nrf_health_process(120000);assert(config_attempts==2);ack();
    inktag_nrf_health_process(999999);assert(config_attempts==2);
    puts("PASS shipping health + real NOR program/erase: unavailable ADC allowed as UNKNOWN, valid-low denial/retry, ACK/lifetime and boundary guards");
}
'''
with tempfile.TemporaryDirectory() as temp:
    out=Path(temp);file=out/'retry.c';binary=out/'retry'
    file.write_text(PREFIX+record_gate+'\n'+health+'\n'+function('range_valid')+'\n'+function('inktag_nrf_external_flash_program')+'\n'+function('inktag_nrf_external_flash_erase_sector')+'\n'+BOUNDARY,encoding='utf-8')
    includes=['-I'+str(ROOT/p) for p in ('tests/mocks/nrf_external','compat/nordic/src','compat/common/power','compat/common/protocol','compat/common/storage','compat/common/epd','compat/common/status')]
    # Use the real exact-board definitions before the generic flash test board.
    includes.insert(0,'-I'+str(ROOT.parents[2]/'boards'))
    includes.insert(1,'-I'+str(ROOT/'compat/nordic/config'))
    includes.append('-I'+str(ROOT/'compat/common/health'))
    sources=[str(file),str(PORT/'inktag_nrf_record_store.c'),str(ROOT/'compat/common/power/inktag_power.c'),str(ROOT/'compat/common/protocol/inktag_protocol_core.c'),str(ROOT/'compat/common/status/inktag_status.c')]
    for flags in (['-O2'],['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=all','-no-pie']):
        subprocess.run([os.environ.get('CC','gcc'),'-std=c99','-Wall','-Wextra','-Werror','-DNRF52811_XXAA',*flags,*includes,*sources,'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
