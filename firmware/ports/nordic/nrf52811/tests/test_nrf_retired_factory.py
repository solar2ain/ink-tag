"""Retired action3 cannot reach power/idle/destructive dispatch admission."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text()
start = source.index('static void epd_v2_process_packet(')
end = source.index('    switch (request.opcode) {', start)
body = source[start:end] + '''    dispatches++;
    epd_v2_system_action(p_epd, request.opcode, request.transaction_id,
        request.flags, request.payload, request.payload_length);
}
'''
def function(name):
    found=re.search(r'^static [^\n]+\b'+name+r'\([^;{]*\)\s*\{[\s\S]*?\n}',source,re.M)
    assert found,name
    return found.group()

fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "inktag_protocol_core.h"
#include "inktag_power.h"
#include "inktag_lifecycle.h"
typedef struct { int unused; } ble_epd_t;
#define INKTAG_NRF_CMD_SET_IMAGE_METADATA 0x1e
#define INKTAG_NRF_HEALTH_OPERATION_BLE 1
#define EPD_V2_STORE_BUSY() store_busy
#define INKTAG_BOARD_LIFECYCLE_QUALIFIED 0
#define INKTAG_BOARD_LIFECYCLE_V2_SUPPORTED 0
#define INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED 0
#define INKTAG_NRF_IMAGE_STORE_ENABLED 1
#define NRF_SUCCESS 0
static bool m_epd_operation_active, m_epd_operation_pending, store_busy;
static struct { bool active; } m_v2_transfer;
static bool power_ok;
static unsigned power_checks, dispatches, replies;
static unsigned time_writes, reset_requests;
static int16_t m_v2_timezone_minutes;
static uint8_t status, replied_opcode, replied_transaction;
static void inktag_nrf_health_stage(unsigned stage,unsigned op) { (void)stage;(void)op; }
/* Backend may be idle while its completion ACK is still pending. */
static bool inktag_image_store_busy(void) { return false; }
static uint32_t timestamp(void) { return 42; }
static bool epd_time_state_write(uint32_t t,int16_t zone) {
    assert(t==42 && zone==0);time_writes++;return true;
}
static bool epd_v2_schedule_system_action(uint8_t action) {
    assert(action==INKTAG_SYSTEM_ACTION_RESET);reset_requests++;return true;
}
static void nrf_delay_ms(unsigned n) { assert(n==250); }
static unsigned ble_dfu_buttonless_bootloader_start_prepare(void) { return NRF_SUCCESS; }
static void epd_v2_send_event(ble_epd_t *p,uint8_t event,uint8_t s,uint8_t op,
        uint8_t tx,const uint8_t *data,unsigned n) {
    (void)p;(void)event;(void)s;(void)op;(void)tx;(void)data;(void)n;
}
static uint8_t epd_v2_display_settings_write_access(const uint8_t *p,unsigned n) {
    (void)p;(void)n;return INKTAG_STATUS_OK;
}
static bool inktag_nrf_health_operation_allowed(unsigned op,bool fresh) {
    assert(op==INKTAG_POWER_OPERATION_FLASH && fresh);power_checks++;return power_ok;
}
static void epd_v2_send_response(ble_epd_t *p,uint8_t op,uint8_t tx,uint8_t s,
        const uint8_t *data,unsigned n) {
    (void)p;(void)data;assert(n==0);replies++;status=s;replied_opcode=op;replied_transaction=tx;
}
#define epd_v2_reply_status(p,o,t,s) epd_v2_send_response(p,o,t,s,NULL,0u)

''' + function('epd_v2_command_policy')+'\n'+function('epd_v2_command_requires_idle_display') + '\n' + function('epd_v2_system_action') + '\n' + body + r'''
int main(void) {
    for(unsigned busy=0;busy<16;busy++) for(unsigned power=0;power<2;power++)
    for(unsigned confirm=0;confirm<2;confirm++) for(unsigned extra=0;extra<2;extra++) {
        uint8_t packet[]={INKTAG_CMD_SYSTEM_ACTION,39,
            confirm?INKTAG_REQUEST_FLAG_CONFIRM:0,INKTAG_SYSTEM_ACTION_FACTORY_RESET,0};
        power_ok=power;
        m_epd_operation_active=busy&1;m_epd_operation_pending=busy&2;
        m_v2_transfer.active=busy&4;store_busy=busy&8;
        power_checks=dispatches=replies=0;epd_v2_process_packet(NULL,packet,4+extra);
        assert(replies==1 && status==INKTAG_STATUS_UNSUPPORTED && !power_checks && dispatches==1);
        assert(!time_writes && !reset_requests);
        assert(replied_opcode==INKTAG_CMD_SYSTEM_ACTION && replied_transaction==39);
    }
    uint8_t reset[]={INKTAG_CMD_SYSTEM_ACTION,39,0,INKTAG_SYSTEM_ACTION_RESET};
    m_epd_operation_active=m_epd_operation_pending=m_v2_transfer.active=store_busy=false;
    replies=dispatches=power_checks=0;power_ok=false;
    epd_v2_process_packet(NULL,reset,sizeof(reset));
    assert(replies==1 && status==INKTAG_STATUS_OK && dispatches==1 && !power_checks);
    assert(time_writes==1 && reset_requests==1); /* Ordinary reboot kept. */
    for(unsigned busy=1;busy<16;busy++) {
        m_epd_operation_active=busy&1;m_epd_operation_pending=busy&2;
        m_v2_transfer.active=busy&4;store_busy=busy&8;
        epd_v2_process_packet(NULL,reset,sizeof(reset));
        assert(status==INKTAG_STATUS_BUSY && time_writes==1 && reset_requests==1);
    }
    m_epd_operation_active=m_epd_operation_pending=m_v2_transfer.active=store_busy=false;
    reset[3]=INKTAG_SYSTEM_ACTION_RESET_SETTINGS;
    reset[2]=INKTAG_REQUEST_FLAG_CONFIRM;dispatches=0;
    epd_v2_process_packet(NULL,reset,sizeof(reset));
    assert(status==INKTAG_STATUS_LOW_POWER && !dispatches && power_checks==1);
    /* Invalid wire transaction IDs never enter this private normalized
     * dispatcher; test_nrf_v1_queue executes that admission boundary. */
    puts("PASS production retired action3 rejects before every busy/power gate, reboot and low-power admission retained");
}
'''
with tempfile.TemporaryDirectory() as directory:
    file = Path(directory) / 'retired.c'
    binary = Path(directory) / 'retired'
    file.write_text(fixture)
    subprocess.run([os.environ.get('CC', 'gcc'), '-std=c99', '-O2', '-Wall',
                    '-Wextra', '-Werror', '-DINKTAG_PROTOCOL_BASE_ONLY_TARGET=1',
                    '-Ifirmware/common/epd', '-Ifirmware/common/protocol',
                    '-Ifirmware/common/power', str(file),
                    'compat/common/protocol/inktag_protocol_core.c',
                    'compat/common/lifecycle/inktag_lifecycle.c',
                    '-Ifirmware/common/lifecycle',
                    '-o', str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], check=True)
