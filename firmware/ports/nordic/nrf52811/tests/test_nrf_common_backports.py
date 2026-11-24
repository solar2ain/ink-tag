"""Run shipping navigation and mailbox code against fixed behavior contracts."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def function(source, name):
    found = re.search(r'^(?:static )?[^\n]+\b' + name +
                      r'\([^;{]*\)\s*\{[\s\S]*?\n}', source, re.M)
    assert found, name
    return found.group()

service = (ROOT / 'compat/common/epd/inktag_epd_service.c').read_text()
protocol = (ROOT / 'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text()
PREFIX = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "inktag_page_cycle.h"
#define INKTAG_NRF_IMAGE_STORE_ENABLED 1
#define MODE_CALENDAR EPD_DISPLAY_STATE_CALENDAR
#define MODE_CLOCK EPD_DISPLAY_STATE_CLOCK
#define MODE_DEVICE_INFO EPD_DISPLAY_STATE_DEVICE_INFO
#define INKTAG_IMAGE_STORE_OK 0
typedef struct { unsigned screen_config; } ble_epd_t;
typedef enum { NRF_EPD_OP_GUI, NRF_EPD_OP_IMAGE_SHOW } nrf_epd_operation_type_t;
static epd_display_state_t m_display_state;
static bool m_display_state_loaded=true, m_slideshow_active;
static bool m_button_slideshow_return_valid, available=true, visible=true;
static uint8_t m_slideshow_current_id=UINT8_MAX;
static uint32_t m_slideshow_next_at, m_slideshow_interval=3600;
static unsigned enumerated;
static uint32_t timestamp(void) { return 1000u; }
static void epd_send_text(ble_epd_t *p,const char *text) {(void)p;(void)text;}
static uint16_t inktag_image_store_sector_count(void) { return 3u; }
static int inktag_image_store_next_compatible(uint8_t after,unsigned *screen,uint8_t *id) {
    (void)screen;enumerated++;
    if(!available) return 1;
    *id=after == UINT8_MAX ? 1u : after == 1u ? 2u : 1u; return 0;
}
static bool epd_v2_image_record_visible(ble_epd_t *p,uint8_t id) {(void)p;return visible && id==2u;}
static uint8_t m_v2_rx_packet[64];
static uint16_t m_v2_rx_length;
static bool m_v2_rx_pending,m_v2_rx_processing,m_epd_operation_active;
static ble_epd_t *m_v2_display_read_service;
static unsigned processed,expected;
static void epd_v2_display_read_yield(void);
static void epd_v2_process_packet(ble_epd_t *p,const uint8_t *packet,uint16_t length) {
    assert(p && length==4u && packet[0]==expected);
    assert(m_v2_rx_pending && m_v2_rx_processing);
    processed++;
    m_v2_display_read_service=p;
    epd_v2_display_read_yield(); /* No recursive dispatch while leased. */
    assert(m_v2_rx_processing);m_v2_display_read_service=NULL;
}
'''

TEST = r'''
static void navigation(void) {
    ble_epd_t device={0};nrf_epd_operation_type_t operation;uint8_t argument;bool rotate;
    const uint8_t modes[]={EPD_DISPLAY_STATE_CALENDAR,EPD_DISPLAY_STATE_CLOCK,
        EPD_DISPLAY_STATE_DEVICE_INFO,EPD_DISPLAY_STATE_PICTURE};
    const uint8_t expected_pages[]={MODE_CLOCK,MODE_DEVICE_INFO,2u,MODE_CALENDAR};
    for(unsigned i=0;i<4u;i++) {
        m_display_state.mode=modes[i];m_slideshow_active=false;
        assert(epd_page_cycle_resolve(&device,&operation,&argument,&rotate));
        assert(argument==expected_pages[i]);
        assert((operation==NRF_EPD_OP_IMAGE_SHOW)==(i==2u) && rotate==(i==2u));
    }
    m_slideshow_active=true;m_slideshow_current_id=2u;m_slideshow_next_at=2000u;
    m_display_state.mode=EPD_DISPLAY_STATE_DEVICE_INFO;
    assert(epd_page_cycle_resolve(&device,&operation,&argument,&rotate));
    assert(operation==NRF_EPD_OP_GUI && argument==MODE_CALENDAR && !rotate);
    assert(m_slideshow_active && m_slideshow_next_at==2000u); /* Admission may still fail. */
    epd_page_cycle_scheduled(&device,rotate);
    assert(!m_slideshow_active && !m_slideshow_next_at);
    epd_page_cycle_scheduled(&device,true);
    assert(m_slideshow_active && m_slideshow_current_id==UINT8_MAX && m_slideshow_next_at==4600u);
    m_slideshow_interval=0u;epd_page_cycle_scheduled(&device,true);
    assert(m_slideshow_interval==3600u && m_slideshow_active);
    m_slideshow_active=false;m_display_state.mode=EPD_DISPLAY_STATE_DEVICE_INFO;
    for(unsigned hidden=0;hidden<2u;hidden++) {
        available=hidden!=0u;visible=false;enumerated=0u;
        assert(epd_page_cycle_resolve(&device,&operation,&argument,&rotate));
        assert(operation==NRF_EPD_OP_GUI && argument==MODE_CALENDAR && !rotate);
        assert(enumerated<=3u);
    }
}
static void mailbox(void) {
    ble_epd_t device={0};
    for(unsigned active=0;active<2u;active++) for(unsigned opcode=0;opcode<256u;opcode++) {
        m_epd_operation_active=active!=0u;
        bool allowed=active && (opcode==INKTAG_CMD_GET_TIME || opcode==INKTAG_CMD_GET_HEALTH);
        assert(epd_v2_defer_display_read(&device,(uint8_t)opcode)==allowed);
        m_v2_display_read_service=NULL;
    }
    m_epd_operation_active=true;
    for(unsigned i=0;i<64u;i++) {
        expected=i&1u ? INKTAG_CMD_GET_TIME : INKTAG_CMD_GET_HEALTH;
        m_v2_rx_packet[0]=expected;m_v2_rx_length=4u;m_v2_rx_pending=true;
        assert(epd_v2_defer_display_read(&device,expected));
        m_v2_rx_processing=true;epd_v2_display_read_yield();assert(processed==i);
        m_v2_rx_processing=false;epd_v2_display_read_yield();
        assert(processed==i+1u && !m_v2_rx_pending && !m_v2_rx_length && !m_v2_rx_processing);
        epd_v2_display_read_yield();assert(processed==i+1u);
    }
    m_v2_display_read_service=&device;m_v2_rx_pending=false; /* Disconnect. */
    epd_v2_display_read_yield();assert(processed==64u && !m_v2_display_read_service);
}
int main(void) {
    navigation();mailbox();puts("PASS four-page navigation, refused admission preserves rotation, empty/hidden bounds, mailbox lease and disconnect");
}
'''

with tempfile.TemporaryDirectory() as directory:
    out=Path(directory);file=out/'backports.c';binary=out/'backports'
    code=PREFIX+'\n'.join(function(service,n) for n in (
        'epd_slideshow_stop','epd_slideshow_select_next','epd_page_cycle_resolve','epd_page_cycle_scheduled'))
    code+='\n'.join(function(protocol,n) for n in (
        'epd_v2_process_queued','epd_v2_defer_display_read','epd_v2_display_read_yield'))+TEST
    file.write_text(code)
    for optimization in (['-O2'], ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']):
        subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',*optimization,
            '-Ifirmware/common/epd','-Ifirmware/common/protocol',
            '-Ifirmware/common/ble','-Ifirmware/common/input',str(file),
            'compat/common/epd/inktag_page_cycle.c','-o',str(binary)],cwd=ROOT,check=True)
        subprocess.run([str(binary)],check=True)
