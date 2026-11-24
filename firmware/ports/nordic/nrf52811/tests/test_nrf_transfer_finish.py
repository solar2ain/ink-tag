"""Execute shipping transfer completion and failure release with real LEDs."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text(encoding='utf-8')
def function(name):
    match = re.search(r'static [^\n]+ ' + name + r'\([\s\S]*?\n}', source)
    assert match, name
    return match.group()

transfer = re.search(r'typedef struct \{\n    bool active;[\s\S]*?static inktag_nrf_v2_transfer_t m_v2_transfer;', source).group()
pending = re.search(r'typedef enum \{\n    INKTAG_NRF_V2_STORE_NONE[\s\S]*?static inktag_nrf_v2_store_pending_t m_v2_store_pending;', source).group()
prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "inktag_protocol_core.h"
#include "inktag_nrf_image_store.h"
#include "inktag_status.h"
#define INKTAG_NRF_INDICATOR_ENABLED 1
#define EPD_V2_STORE_RESET() memset(&m_v2_store_pending, 0, sizeof(m_v2_store_pending))
typedef struct { uint16_t max_data_len; } ble_epd_t;
static uint8_t m_pending_picture_store_id;
static inktag_status_runtime_t leds;
static uint8_t last_status, last_color;
static bool capture_active;
static unsigned responses, aborts;
static void apply(void *context, uint8_t color) { (void)context; last_color = color; }
static void epd_v2_indicator_set_active(inktag_status_scene_t scene, bool active) {
    inktag_status_set_active(&leds, scene, active, 10000u);
    inktag_status_tick(&leds, 10000u);
}
static inktag_status_v2_t epd_v2_store_status(inktag_image_store_result_t result) {
    return result == INKTAG_IMAGE_STORE_OK ? INKTAG_STATUS_OK : INKTAG_STATUS_IO_ERROR;
}
static void epd_v2_send_response(ble_epd_t *p, uint8_t op, uint8_t tx,
    inktag_status_v2_t status, const uint8_t *bytes, uint16_t count) {
    (void)p; (void)op; (void)tx; (void)bytes; (void)count;
    last_status = status; responses++;
}
#define epd_v2_reply_status(p,o,t,s) epd_v2_send_response(p,o,t,s,NULL,0u)

static void epd_v2_send_event(ble_epd_t *p, uint8_t event, inktag_status_v2_t status,
    uint8_t op, uint8_t tx, const uint8_t *bytes, uint16_t count) {
    (void)p; (void)event; (void)status; (void)op; (void)tx; (void)bytes; (void)count;
}
bool inktag_image_store_capture_active(void) { return capture_active; }
void inktag_image_store_abort(void) { capture_active = false; aborts++; }
'''
driver = r'''
static void begin(unsigned wait) {
    memset(&m_v2_transfer, 0, sizeof(m_v2_transfer));
    m_v2_transfer.active = true;
    m_v2_transfer.transaction_id = 7;
    m_v2_transfer.descriptor.object_type = INKTAG_TRANSFER_OBJECT_STORED_IMAGE;
    m_v2_store_pending = (inktag_nrf_v2_store_pending_t){wait, 0x12, 7};
    epd_v2_indicator_set_active(INKTAG_STATUS_SCENE_TRANSFER, true);
    assert(leds.active_mask & (1u << INKTAG_STATUS_SCENE_TRANSFER));
}
static void released(void) {
    assert(!m_v2_transfer.active && !m_v2_store_pending.type);
    assert(!(leds.active_mask & (1u << INKTAG_STATUS_SCENE_TRANSFER)));
    assert(last_color == 0);
    assert(!inktag_status_needs_periodic_tick(&leds,10000u));
}
int main(void) {
    ble_epd_t p = {244};
    inktag_status_config_t cfg; inktag_status_config_default(&cfg);
    inktag_status_init(&leds, &cfg, INKTAG_STATUS_COLOR_WHITE, apply, NULL, 0);
    for (unsigned fail=0; fail<2; fail++) {
        begin(INKTAG_NRF_V2_STORE_COMMIT);
        inktag_image_store_event_t e = {INKTAG_IMAGE_STORE_EVENT_COMMIT,
            fail ? INKTAG_IMAGE_STORE_IO_ERROR : INKTAG_IMAGE_STORE_OK,58,0};
        assert(epd_v2_handle_store_event(&p,&e)); released();
    }
    begin(INKTAG_NRF_V2_STORE_BEGIN);
    inktag_image_store_event_t e = {INKTAG_IMAGE_STORE_EVENT_BEGIN,INKTAG_IMAGE_STORE_IO_ERROR,58,0};
    assert(epd_v2_handle_store_event(&p,&e)); released();
    begin(INKTAG_NRF_V2_STORE_BEGIN); e.result = INKTAG_IMAGE_STORE_OK;
    assert(epd_v2_handle_store_event(&p,&e));
    assert(m_v2_transfer.active && (leds.active_mask & (1u << INKTAG_STATUS_SCENE_TRANSFER)));
    capture_active = true; e.type = INKTAG_IMAGE_STORE_EVENT_WRITE; e.result = INKTAG_IMAGE_STORE_IO_ERROR;
    assert(epd_v2_handle_store_event(&p,&e)); released(); assert(aborts == 1);
    /* Release also repairs an already-cleared lease with a stale LED scene. */
    epd_v2_indicator_set_active(INKTAG_STATUS_SCENE_TRANSFER,true);
    epd_v2_transfer_release(); released();
    puts("shipping transfer finish: success/failure/capture abort and stale-LED cleanup passed");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / 'test.c'
    path.write_text(prefix + transfer + pending + function('epd_v2_transfer_release') + function('epd_v2_handle_store_event') + driver, encoding='utf-8')
    for simple in (1,):
        target = Path(tmp) / 'shipping'
        subprocess.run(['gcc','-std=c99','-O2','-Wall',
            '-Ifirmware/common/epd','-Ifirmware/common/protocol','-Ifirmware/common/status',
            '-Ifirmware/ports/nordic/common/src',
            f'-DINKTAG_NRF_SIMPLE_IMAGE_METADATA={simple}','-DINKTAG_NRF_NATIVE_IMAGE_METADATA=0',
            str(path),'compat/common/status/inktag_status.c','compat/common/protocol/inktag_protocol_core.c','-o',str(target)],cwd=ROOT,check=True)
        subprocess.run([str(target)],check=True)
