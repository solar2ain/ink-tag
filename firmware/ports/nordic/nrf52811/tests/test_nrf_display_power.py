"""Exercise shipping GPIO ownership, dispatcher, cancellation and link cleanup."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
service = (ROOT / 'compat/common/epd/inktag_epd_service.c').read_text(encoding='utf-8')
protocol = (ROOT / 'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text(encoding='utf-8')
driver = (ROOT / 'compat/common/epd/inktag_epd_driver.c').read_text(encoding='utf-8')

def function(source, name):
    found = re.search(r'^(?:static )?[^\n]+\b' + name + r'\([^;{]*\)\s*\{[\s\S]*?\n}', source, re.M)
    assert found, name
    return found.group()

PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define INKTAG_NRF_PROTOCOL_V2 1
#define INKTAG_NRF_SDK17 1
#define INKTAG_NRF_IMAGE_STORE_ENABLED 0
#define INKTAG_NRF_INDICATOR_ENABLED 0
#define INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED 0
#define INKTAG_COMPACT_BUSY_DIAG 1
#define INKTAG_EPD_SPI_SOFTWARE_CS 0
#define INKTAG_EPD_SPI_FREQUENCY 4000000
#define INKTAG_EPD_BUSY_INPUT_MODE 0
#define NRF_DRV_SPI_DEFAULT_CONFIG {0}
#define NRF_DRV_SPI_PIN_NOT_USED 255
#define NRF_SUCCESS 0
#define BLE_CONN_HANDLE_INVALID 65535
#define UNUSED_PARAMETER(x) (void)(x)
#define APP_ERROR_CHECK(x) assert((x)==0)
#define DEFAULT 0
#define INPUT 0
#define OUTPUT 1
#define LOW 0
#define HIGH 1
#define EPD_LED_OFF() ((void)0)
#define EPD_LED_ON() ((void)0)
#define EPD_V2_STORE_RESET() ((void)0)
#define INKTAG_NRF_V2_OPERATION_BLE 1
#define INKTAG_TRANSFER_OBJECT_STORED_IMAGE 2
#define INKTAG_TRANSFER_OBJECT_DIRECT_FRAME 1
#define INKTAG_STATUS_OK 0
#define INKTAG_STATUS_INCOMPATIBLE 6
#define INKTAG_STATUS_CANCELLED 7
#define INKTAG_STATUS_IO_ERROR 8
#define INKTAG_STATUS_TIMEOUT 9
#define EPD_DISPLAY_STATE_CLEAR 0
#define EPD_DISPLAY_STATE_PICTURE 5
#define EPD_DISPLAY_STATE_DEVICE_INFO 3
#define EPD_DISPLAY_STATE_NO_SOURCE 255
#define EPD_DISPLAY_STATE_FLAG_AUTO_REFRESH_SUSPENDED 2
#define MODE_DEVICE_INFO 3
#define MODE_CALENDAR 1
#define MODE_CLOCK 2
#define GFX_format snprintf
typedef uint8_t inktag_status_v2_t;
typedef unsigned display_mode_t;
typedef struct { unsigned sck_pin, mosi_pin, ss_pin, frequency; } nrf_drv_spi_config_t;
typedef struct epd_model epd_model_t;
typedef struct {
    void (*init)(epd_model_t *), (*refresh)(epd_model_t *), (*sleep)(epd_model_t *);
    void (*clear)(epd_model_t *, bool);
    bool (*read_busy)(epd_model_t *);
} epd_driver_t;
struct epd_model { epd_driver_t *drv; };
typedef struct { epd_model_t *epd; int screen_config; uint16_t conn_handle; bool is_notification_enabled; } ble_epd_t;
typedef struct { struct { struct { uint16_t conn_handle; } gap_evt; } evt; } ble_evt_t;
typedef enum { NRF_EPD_OP_NONE, NRF_EPD_OP_CLEAR, NRF_EPD_OP_REFRESH, NRF_EPD_OP_GUI } nrf_epd_operation_type_t;
typedef struct { ble_epd_t *p_epd; uint32_t timestamp; uint8_t type, arg, background, origin, v2_opcode, v2_transaction; } nrf_epd_operation_t;
typedef struct { bool active; struct { unsigned object_type; } descriptor; } inktag_nrf_v2_transfer_t;
static bool m_epd_dispatch_scheduled, m_epd_operation_pending, m_epd_operation_active;
static bool m_epd_cancel_requested, m_epd_io_busy, m_picture_io_active, m_v2_rx_pending;
static nrf_epd_operation_t m_epd_pending_operation, m_epd_active_operation;
static inktag_nrf_v2_transfer_t m_v2_transfer;
static unsigned m_driver_refs, spi, spi_inits, spi_uninits, refreshes, resets;
static unsigned pin_mode[32], pin_value[32], expected_refs, completion;
static bool EPD_LED_ENABLED, cancel_in_refresh, disconnect_in_refresh, gui_success=true;
enum epd_pad { PAD_MOSI,PAD_SCLK,PAD_CS,PAD_DC,PAD_RESET,PAD_BUSY,PAD_BS,PAD_ENABLE,PAD_LED,PAD_COUNT };
static uint8_t epd_pads[PAD_COUNT]={20,19,6,5,4,3,2,7,255};
#define EPD_MOSI_PIN epd_pads[PAD_MOSI]
#define EPD_SCLK_PIN epd_pads[PAD_SCLK]
#define EPD_CS_PIN epd_pads[PAD_CS]
#define EPD_DC_PIN epd_pads[PAD_DC]
#define EPD_RST_PIN epd_pads[PAD_RESET]
#define EPD_BUSY_PIN epd_pads[PAD_BUSY]
#define EPD_BS_PIN epd_pads[PAD_BS]
#define EPD_EN_PIN epd_pads[PAD_ENABLE]
#define EPD_LED_PIN epd_pads[PAD_LED]
static ble_epd_t *connected;
static ble_epd_t *m_v2_display_read_service;
static bool (*abort_callback)(void);
/* These power-ownership cases assume validated, enabled display routes.
 * test_nrf_display_gpio_gate exercises the real disabled/unpublished gate. */
static bool EPD_GPIO_IsConfigured(void) { return true; }
static void pinMode(unsigned pin, unsigned mode) { if(pin!=255) pin_mode[pin]=mode; }
static void digitalWrite(unsigned pin, unsigned value) { if(pin!=255) pin_value[pin]=value; }
static int nrf_drv_spi_init(unsigned *instance, nrf_drv_spi_config_t *cfg, void *handler, void *context) {
    (void)instance;(void)handler;(void)context;assert(cfg->frequency==4000000);spi_inits++;return 0;
}
static void nrf_drv_spi_uninit(unsigned *instance) { (void)instance;spi_uninits++; }
static void EPD_BoardPrepare(void) {}
static void EPD_BoardRelease(void) {}
/* The actual nRF52811 board has no separate BoardPrepare implementation. */
static bool EPD_BoardIsPrepared(void) { return false; }
static void EPD_SetAbortCallback(bool (*callback)(void)) { abort_callback=callback; }
static bool EPD_AbortRequested(void) { return abort_callback && abort_callback(); }
static bool epd_abort_requested(void) { return m_epd_cancel_requested; }
static void epd_v2_display_read_yield(void) {}
static void EPD_Reset(bool level, unsigned ms) { (void)level;(void)ms;assert(pin_value[7]);resets++; }
static void EPD_BusyDiagReset(void) {}
static bool EPD_BusyDiagAsserted(void) { return true; }
static bool epd_requires_busy_edge(epd_model_t *epd) { (void)epd;return false; }
static void inktag_nrf_health_display_begin(void) {}
static void inktag_nrf_health_display_end(void) {}
static void epd_send_text(ble_epd_t *p,const char *text) { (void)p;(void)text; }
static void epd_send_diag(ble_epd_t *p,const char *text) { (void)p;(void)text; }
static void epd_slideshow_stop(ble_epd_t *p,bool notify) { (void)p;(void)notify; }
static void epd_v2_send_response(ble_epd_t *p,uint8_t opcode,uint8_t tx,uint8_t status,const void *data,unsigned count) {
    (void)p;(void)opcode;(void)tx;(void)data;(void)count;assert(status==INKTAG_STATUS_OK);
}
#define epd_v2_reply_status(p,o,t,s) epd_v2_send_response(p,o,t,s,NULL,0u)

static void epd_record_display_state(ble_epd_t *p,unsigned mode,unsigned arg,unsigned interval) { (void)p;(void)mode;(void)arg;(void)interval; }
static bool epd_persist_display_state_flags(unsigned set,unsigned clear) { (void)set;(void)clear;return true; }
static void epd_v2_operation_complete(const nrf_epd_operation_t *op,inktag_status_v2_t status) { (void)op;completion=status; }
static void epd_schedule_dispatch(void) {}
static void app_feed_wdt(void) {}
static void nrf_delay_ms(unsigned ms) { (void)ms; }
static void on_disconnect(ble_epd_t *,ble_evt_t *);
static void panel_init(epd_model_t *p) { (void)p;assert(pin_value[7]); }
static void panel_refresh(epd_model_t *p) {
    (void)p;assert(pin_value[7] && m_driver_refs==expected_refs);refreshes++;
    if(cancel_in_refresh) m_epd_cancel_requested=true;
    if(disconnect_in_refresh) on_disconnect(connected,NULL);
    assert(pin_value[7]); /* Cancellation must leave EN alive until RESET. */
}
static void panel_sleep(epd_model_t *p) { (void)p;assert(pin_value[7]); }
static void panel_clear(epd_model_t *p,bool refresh) { if(refresh) panel_refresh(p); }
static bool panel_busy(epd_model_t *p) { (void)p;return false; }
static epd_driver_t panel={panel_init,panel_refresh,panel_sleep,panel_clear,panel_busy};
static epd_model_t model={&panel};
static epd_model_t *epd_init(void *config) { (void)config;return &model; }
static bool epd_gui_update_run(ble_epd_t *p,unsigned mode,uint32_t timestamp) {
    (void)mode;(void)timestamp;if(gui_success) panel_refresh(p->epd);return gui_success;
}
'''

TEST = r'''
static void queue(ble_epd_t *p,unsigned type) {
    m_epd_pending_operation=(nrf_epd_operation_t){.p_epd=p,.type=type,.origin=1};
    m_epd_operation_pending=true;
}
static void off(void) {
    assert(!m_driver_refs && !pin_value[7]);
    assert(spi_inits==spi_uninits && !m_epd_operation_active);
}
static void begin_direct(void) {
    EPD_GPIO_Init();m_picture_io_active=true;
    m_v2_transfer.active=true;
    m_v2_transfer.descriptor.object_type=INKTAG_TRANSFER_OBJECT_DIRECT_FRAME;
}
int main(void) {
    ble_epd_t p={.epd=&model};connected=&p;
    ble_evt_t event={.evt.gap_evt.conn_handle=1};
    on_connect(&p,&event);
#if INKTAG_BOARD_LAZY_DISPLAY_BUS
    off(); /* Queries and stored-image transfers own no display reference. */
    m_v2_transfer.active=true;
    m_v2_transfer.descriptor.object_type=INKTAG_TRANSFER_OBJECT_STORED_IMAGE;
    epd_v2_cancel(&p,1,1);off();
    expected_refs=1;
    queue(&p,NRF_EPD_OP_GUI);epd_operation_dispatch(NULL,0);off();
    assert(completion==INKTAG_STATUS_OK);
    queue(&p,NRF_EPD_OP_CLEAR);m_epd_pending_operation.arg=1;
    epd_operation_dispatch(NULL,0);off();
    begin_direct();epd_v2_transfer_release();queue(&p,NRF_EPD_OP_REFRESH);
    epd_operation_dispatch(NULL,0);off();assert(!m_picture_io_active);
    begin_direct();epd_v2_cancel(&p,1,1);off();assert(!m_v2_transfer.active);
    begin_direct();epd_v2_transfer_release();queue(&p,NRF_EPD_OP_REFRESH);
    epd_v2_cancel(&p,1,1);off();assert(!m_epd_operation_pending);
    unsigned before=refreshes;epd_operation_dispatch(NULL,0);assert(refreshes==before);
    begin_direct();on_disconnect(&p,NULL);off();assert(!m_picture_io_active);
    on_connect(&p,&event);off();
    begin_direct();epd_v2_transfer_release();queue(&p,NRF_EPD_OP_REFRESH);
    on_disconnect(&p,NULL);off();assert(!m_epd_operation_pending);
    on_connect(&p,&event);off();
    for(unsigned direct=0;direct<2;direct++) for(unsigned disconnect=0;disconnect<2;disconnect++) {
        if(direct) { begin_direct();epd_v2_transfer_release(); }
        queue(&p,direct?NRF_EPD_OP_REFRESH:NRF_EPD_OP_GUI);
        cancel_in_refresh=!disconnect;disconnect_in_refresh=disconnect;
        before=resets;epd_operation_dispatch(NULL,0);off();
        assert(resets==before+1 && completion==INKTAG_STATUS_CANCELLED);
        cancel_in_refresh=disconnect_in_refresh=false;
        on_connect(&p,&event);off();
    }
    gui_success=false;queue(&p,NRF_EPD_OP_GUI);epd_operation_dispatch(NULL,0);
    off();assert(completion==INKTAG_STATUS_IO_ERROR);
    on_disconnect(&p,NULL);off();
#else
    assert(m_driver_refs==1 && pin_value[7]);
    expected_refs=2;queue(&p,NRF_EPD_OP_GUI);epd_operation_dispatch(NULL,0);
    assert(m_driver_refs==1 && pin_value[7]);
    on_disconnect(&p,NULL);off();
#endif
    puts("shipping display power: queries, native/direct refresh, abort, queued COMMIT cancellation, disconnect and active RESET ownership passed");
}
'''

with tempfile.TemporaryDirectory() as temp:
    source = Path(temp) / 'power.c'
    pieces = [PREFIX]
    pieces += [function(driver, name) for name in ('EPD_GPIO_Init', 'EPD_GPIO_Uninit')]
    pieces += [function(protocol, name) for name in ('epd_v2_transfer_release', 'epd_v2_transfer_reset', 'epd_v2_link_closed', 'epd_v2_cancel')]
    pieces += [function(service, name) for name in ('epd_notifications_set', 'on_connect', 'on_disconnect', 'epd_operation_dispatch')]
    source.write_text('\n'.join(pieces) + TEST, encoding='utf-8')
    for lazy in (1, 0):
        binary = Path(temp) / f'power-{lazy}'
        subprocess.run(['cc', '-std=c99', '-O2', f'-DINKTAG_BOARD_LAZY_DISPLAY_BUS={lazy}', str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
