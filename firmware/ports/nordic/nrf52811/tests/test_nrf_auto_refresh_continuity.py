"""Execute nRF11 shipping scheduler/dispatcher/boot policy, strict and sanitized.

All panel, ADC, SDK queue and storage boundaries are fake; no device I/O.
"""
import argparse
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


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "inktag_page_cycle.h"
#include "inktag_protocol_v2.h"
#define INKTAG_NRF_PROTOCOL_V2 1
#define INKTAG_NRF_IMAGE_STORE_ENABLED 1
#define INKTAG_NRF_INDICATOR_ENABLED 0
#define INKTAG_BOARD_LAZY_DISPLAY_BUS 1
#define INKTAG_NRF_BOARD_NRF52811_AESL 1
#define INKTAG_COMPACT_BUSY_DIAG 1
#define INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED 0
#define INKTAG_NRF_V2_OPERATION_BLE 1
#define NRF_SUCCESS 0
#define INKTAG_POWER_OPERATION_DISPLAY 0
#define GFX_format snprintf
__SHIPPING_GUI_MODE_ENUM__
typedef struct epd_model epd_model_t;
typedef struct {
 void (*init)(epd_model_t *), (*refresh)(epd_model_t *), (*sleep)(epd_model_t *);
 void (*clear)(epd_model_t *,bool);
 bool (*read_busy)(epd_model_t *);
} epd_driver_t;
struct epd_model {epd_driver_t *drv;};
typedef struct {epd_model_t *epd; int screen_config; struct {uint8_t display_mode;} config;} ble_epd_t;
static epd_display_state_t m_display_state;
static bool m_display_state_loaded=true, m_epd_dispatch_scheduled;
static bool m_epd_operation_pending, m_epd_operation_active, m_epd_cancel_requested;
static bool m_epd_io_busy, m_picture_io_active, m_native_timer_update_pending;
static bool m_slideshow_active, m_button_slideshow_return_valid, m_ghosting_stop;
static uint8_t m_slideshow_current_id=UINT8_MAX, m_pending_picture_store_id=UINT8_MAX;
static uint32_t m_slideshow_next_at, m_slideshow_interval=30;
static uint32_t m_native_timer_update_timestamp, now;
static bool allowed=true, store_busy, next_available=true, replayable=true;
static bool gui_success=true, show_success=true, stuck_busy, edge_required, edge_seen=true;
static bool stop_in_show, cancel_in_show;
static unsigned late_show, admissions, completed, writes, resets, gpio_refs, end_calls;
static unsigned refuse_on_admission;
static unsigned clear_calls,gui_calls,last_gui_mode;
static uint8_t next_id=9, completion;
static struct {bool active;} m_v2_transfer;
static uint8_t response_status;
static void (*queued)(void *,uint16_t);
static bool (*abort_callback)(void);
static void epd_operation_dispatch(void *,uint16_t);
static void epd_stop_current_operation(ble_epd_t *);
static uint32_t timestamp(void) {return now;}
static bool inktag_time_boundary_crossed(uint32_t a,uint32_t b,uint32_t period) {return a/period!=b/period;}
static bool inktag_nrf_health_operation_allowed(unsigned op,bool refresh) {(void)op;(void)refresh;++admissions;return allowed && admissions!=refuse_on_admission;}
static bool inktag_image_store_busy(void) {return store_busy;}
static bool epd_slideshow_select_next(ble_epd_t *p,uint8_t *id) {(void)p;*id=next_id;return next_available;}
static bool epd_stored_image_replayable(ble_epd_t *p,uint8_t id,bool slide) {(void)p;(void)id;(void)slide;return replayable;}
static void epd_send_text(ble_epd_t *p,const char *s) {(void)p;(void)s;}
static void epd_send_diag(ble_epd_t *p,const char *s) {(void)p;(void)s;}
static uint32_t inktag_read_u32_le(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void epd_v2_send_response(ble_epd_t *p,uint8_t op,uint8_t tx,uint8_t status,const uint8_t *data,uint16_t length) {
 (void)p;(void)op;(void)tx;(void)data;assert(!length);response_status=status;
}
#define epd_v2_reply_status(p,o,t,s) epd_v2_send_response(p,o,t,s,NULL,0u)

static void epd_v2_send_event(ble_epd_t *p,uint8_t ev,uint8_t status,uint8_t op,uint8_t tx,const uint8_t *data,uint16_t length) {
 (void)p;(void)ev;(void)status;(void)op;(void)tx;(void)data;(void)length;
}
bool epd_display_state_write(const epd_display_state_t *s) {(void)s;++writes;return true;}
static unsigned app_sched_event_put(void *data,uint16_t size,void (*handler)(void *,uint16_t)) {
 assert(data==NULL && size==0);queued=handler;return NRF_SUCCESS;
}
/* This scheduler fixture assumes validated, enabled display routes. The
 * shipping disabled/unpublished cases belong to test_nrf_display_gpio_gate. */
static bool EPD_GPIO_IsConfigured(void) {return true;}
static void EPD_GPIO_Init(void) {++gpio_refs;}
static void EPD_GPIO_Uninit(void) {assert(gpio_refs);--gpio_refs;}
static void EPD_BoardPrepare(void) {}
static void EPD_BoardRelease(void) {}
static void EPD_Reset(bool high,unsigned ms) {assert(high && ms==10 && gpio_refs);++resets;}
static void EPD_SetAbortCallback(bool (*callback)(void)) {abort_callback=callback;}
static bool EPD_AbortRequested(void) {return abort_callback && abort_callback();}
static bool epd_abort_requested(void) {return m_epd_cancel_requested;}
static void EPD_BusyDiagReset(void) {}
static bool EPD_BusyDiagAsserted(void) {return edge_seen;}
static bool epd_requires_busy_edge(epd_model_t *p) {(void)p;return edge_required;}
static void inktag_nrf_health_display_begin(void) {}
static void inktag_nrf_health_display_end(void) {++end_calls;}
static void inktag_nrf_health_panel_temperature_sample(const void *p) {(void)p;}
static void app_feed_wdt(void) {}
static void nrf_delay_ms(unsigned ms) {(void)ms;}
static void panel_init(epd_model_t *p) {(void)p;}
static void panel_refresh(epd_model_t *p) {(void)p;}
static void panel_sleep(epd_model_t *p) {(void)p;}
static void panel_clear(epd_model_t *p,bool refresh) {(void)p;(void)refresh;++clear_calls;}
static bool panel_busy(epd_model_t *p) {(void)p;return stuck_busy;}
static epd_driver_t panel={panel_init,panel_refresh,panel_sleep,panel_clear,panel_busy};
static epd_model_t model={&panel};
static epd_model_t *epd_init(const void *config) {(void)config;return &model;}
static bool epd_gui_update_run(ble_epd_t *p,unsigned mode,uint32_t t) {(void)p;(void)t;++gui_calls;last_gui_mode=mode;return gui_success;}
static bool epd_image_store_show_run(ble_epd_t *p,uint8_t id) {
 (void)id;now+=late_show;
 if(stop_in_show)epd_stop_current_operation(p);
 if(cancel_in_show)m_epd_cancel_requested=true;
 return show_success;
}
'''

TEST = r'''
static void run_queued(void) {
 assert(queued);void (*handler)(void *,uint16_t)=queued;queued=NULL;handler(NULL,0);
 assert(!gpio_refs && !m_epd_operation_active && !m_epd_io_busy && !abort_callback);
}
static ble_epd_t device(void) {ble_epd_t p={.epd=&model};p.config.display_mode=MODE_CLOCK;return p;}
static void native(void) {
 ble_epd_t p=device();m_display_state=(epd_display_state_t){MODE_CLOCK,255,0x8000,0};
 now=60;ble_epd_on_timer(&p,59,now,false);
 assert(admissions==1 && m_epd_operation_pending && !m_native_timer_update_pending);run_queued();
 assert(writes==0 && m_display_state.reserved==0x8000);
 allowed=false;now=120;ble_epd_on_timer(&p,119,now,false);
 assert(admissions==2 && !m_native_timer_update_pending && !m_epd_operation_pending);
 for(unsigned i=0;i<1000;i++){epd_native_timer_update_try(&p);}
 assert(admissions==2);
 allowed=true;now=180;ble_epd_on_timer(&p,179,now,false);run_queued();assert(admissions==3);
 m_epd_operation_active=true;now=240;ble_epd_on_timer(&p,239,now,false);
 assert(m_native_timer_update_pending && admissions==3);
 now=300;ble_epd_on_timer(&p,299,now,false);m_epd_operation_active=false;
 store_busy=true;for(unsigned i=0;i<1000;i++){epd_native_timer_update_try(&p);}assert(admissions==3);
 store_busy=false;epd_native_timer_update_try(&p);assert(admissions==4 && m_epd_pending_operation.timestamp==300);run_queued();
 p.config.display_mode=MODE_CALENDAR;now=86400;ble_epd_on_timer(&p,86399,now,false);run_queued();
 assert(admissions==5 && m_display_state.reserved==0x8000);
 now=172800;allowed=false;ble_epd_on_timer(&p,172799,now,false);
 for(unsigned i=0;i<1000;i++){epd_native_timer_update_try(&p);}
 assert(admissions==6 && !m_native_timer_update_pending);
 allowed=true;now=259200;ble_epd_on_timer(&p,259199,now,false);run_queued();assert(admissions==7);
 m_native_timer_update_pending=true;p.config.display_mode=EPD_DISPLAY_STATE_PICTURE;
 epd_native_timer_update_try(&p);assert(!m_native_timer_update_pending && admissions==7);
}
static void failure(void) {
 ble_epd_t p=device();m_display_state=(epd_display_state_t){MODE_CLOCK,255,0,0};
 gui_success=false;now=60;ble_epd_on_timer(&p,59,now,false);run_queued();
 assert(completion==INKTAG_STATUS_IO_ERROR && writes==0 && m_display_state.reserved==0);
 gui_success=true;now=120;ble_epd_on_timer(&p,119,now,false);run_queued();assert(completion==INKTAG_STATUS_OK);
 stuck_busy=true;unsigned before=writes;now=180;ble_epd_on_timer(&p,179,now,false);run_queued();
 assert(completion==INKTAG_STATUS_TIMEOUT && resets==1 && writes==before);
 stuck_busy=false;edge_required=true;edge_seen=false;now=240;ble_epd_on_timer(&p,239,now,false);run_queued();
 assert(completion==INKTAG_STATUS_TIMEOUT && resets==2 && writes==before);
 edge_required=false;edge_seen=true;now=300;ble_epd_on_timer(&p,299,now,false);run_queued();
 assert(completion==INKTAG_STATUS_OK && end_calls==5);
}
static void slideshow(void) {
 ble_epd_t p=device();p.config.display_mode=EPD_DISPLAY_STATE_PICTURE;
 m_display_state=(epd_display_state_t){EPD_DISPLAY_STATE_SLIDESHOW,7,0,30};
 m_slideshow_active=true;m_slideshow_current_id=7;m_slideshow_next_at=30;
 now=30;allowed=false;ble_epd_on_timer(&p,29,now,false);
 assert(admissions==1 && m_slideshow_active && m_slideshow_current_id==7 && m_slideshow_next_at==60);
 for(unsigned i=0;i<100;i++){ble_epd_on_timer(&p,now,now,false);}assert(admissions==1);
 allowed=true;next_available=false;now=60;ble_epd_on_timer(&p,59,now,false);
 assert(admissions==1 && m_slideshow_active && m_slideshow_current_id==7 && m_slideshow_next_at==90);
 next_available=true;now=90;show_success=false;late_show=45;ble_epd_on_timer(&p,89,now,false);
 assert(m_slideshow_current_id==7);run_queued();
 assert(completion!=INKTAG_STATUS_OK && completed==1 && writes==0);
 assert(m_slideshow_active && m_slideshow_current_id==7 && m_slideshow_next_at==now+30);
 late_show=0;show_success=true;now=m_slideshow_next_at;ble_epd_on_timer(&p,now-1,now,false);
 assert(m_slideshow_current_id==7);run_queued();assert(m_slideshow_current_id==9 && m_display_state.argument==9);
 next_id=10;now=m_slideshow_next_at;ble_epd_on_timer(&p,now-1,now,false);
 stuck_busy=true;unsigned before=writes;run_queued();assert(completion==INKTAG_STATUS_TIMEOUT);
 assert(m_slideshow_active && m_slideshow_current_id==9 && writes==before);
 stuck_busy=false;now=m_slideshow_next_at;ble_epd_on_timer(&p,now-1,now,false);
 cancel_in_show=true;run_queued();assert(completion==INKTAG_STATUS_CANCELLED && m_slideshow_current_id==9);
 cancel_in_show=false;now=m_slideshow_next_at;ble_epd_on_timer(&p,now-1,now,false);
 stop_in_show=true;run_queued();assert(completion==INKTAG_STATUS_CANCELLED && !m_slideshow_active && !m_slideshow_next_at);
 before=admissions;now+=300;ble_epd_on_timer(&p,now-1,now,false);assert(admissions==before);
 stop_in_show=false;m_slideshow_active=true;m_slideshow_current_id=7;m_slideshow_next_at=now;
 ble_epd_on_timer(&p,now-1,now,false);assert(m_epd_operation_pending);
 m_native_timer_update_pending=true;epd_stop_current_operation(&p);run_queued();
 assert(!m_slideshow_active && !m_native_timer_update_pending);
}
static void modes(void) {
 ble_epd_t p=device();p.config.display_mode=EPD_DISPLAY_STATE_PICTURE;
 m_slideshow_active=true;m_slideshow_current_id=7;m_slideshow_interval=30;m_slideshow_next_at=200;
 uint8_t resource[]={INKTAG_DISPLAY_MODE_RESOURCE,9};
 epd_v2_set_display_mode(&p,INKTAG_CMD_SET_DISPLAY_MODE,1,resource,sizeof(resource));
 assert(response_status==INKTAG_STATUS_OK && m_slideshow_current_id==7 && m_slideshow_next_at==200);
 show_success=false;run_queued();assert(completion!=INKTAG_STATUS_OK && m_slideshow_current_id==7 && m_slideshow_active);
 show_success=true;now=m_slideshow_next_at;ble_epd_on_timer(&p,now-1,now,false);run_queued();assert(m_slideshow_current_id==9);
 uint8_t slide[]={INKTAG_DISPLAY_MODE_SLIDESHOW,30,0,0,0};next_id=10;
 epd_v2_set_display_mode(&p,INKTAG_CMD_SET_DISPLAY_MODE,2,slide,sizeof(slide));
 assert(response_status==INKTAG_STATUS_OK && m_slideshow_current_id==9);
 show_success=false;run_queued();assert(m_slideshow_current_id==9 && m_slideshow_active);
 show_success=true;now=m_slideshow_next_at;ble_epd_on_timer(&p,now-1,now,false);run_queued();assert(m_slideshow_current_id==10);
 next_available=false;epd_v2_set_display_mode(&p,INKTAG_CMD_SET_DISPLAY_MODE,3,slide,sizeof(slide));
 assert(response_status==INKTAG_STATUS_NOT_FOUND && m_slideshow_current_id==10 && m_slideshow_active);
 allowed=false;epd_v2_set_display_mode(&p,INKTAG_CMD_SET_DISPLAY_MODE,4,resource,sizeof(resource));
 assert(response_status==INKTAG_STATUS_LOW_POWER && m_slideshow_current_id==10 && !m_epd_operation_pending);
 allowed=true;m_v2_transfer.active=true;epd_v2_set_display_mode(&p,INKTAG_CMD_SET_DISPLAY_MODE,5,resource,sizeof(resource));
 assert(response_status==INKTAG_STATUS_BUSY && m_slideshow_current_id==10);m_v2_transfer.active=false;
 epd_v2_set_display_mode(&p,INKTAG_CMD_SET_DISPLAY_MODE,6,resource,0);assert(response_status==INKTAG_STATUS_INVALID_REQUEST);
 uint8_t calendar[]={INKTAG_DISPLAY_MODE_CALENDAR};
 epd_v2_set_display_mode(&p,INKTAG_CMD_SET_DISPLAY_MODE,7,calendar,1);run_queued();assert(!m_slideshow_active);
}
static void admission_start(bool active) {
 ble_epd_t p=device();p.config.display_mode=EPD_DISPLAY_STATE_PICTURE;
 now=100;m_slideshow_active=active;m_slideshow_current_id=7;m_slideshow_interval=30;m_slideshow_next_at=200;
 refuse_on_admission=2;uint8_t slide[]={INKTAG_DISPLAY_MODE_SLIDESHOW,60,0,0,0};
 epd_v2_set_display_mode(&p,INKTAG_CMD_SET_DISPLAY_MODE,1,slide,sizeof(slide));
 assert(admissions==2 && !m_epd_operation_pending && !queued && response_status!=INKTAG_STATUS_OK);
 assert(m_slideshow_active==active && m_slideshow_current_id==7 && m_slideshow_interval==30 && m_slideshow_next_at==200);
 for(unsigned i=0;i<100;i++)ble_epd_on_timer(&p,100,100,false);
 assert(admissions==2 && !m_epd_operation_pending);
 refuse_on_admission=0;epd_v2_set_display_mode(&p,INKTAG_CMD_SET_DISPLAY_MODE,2,slide,sizeof(slide));
 assert(response_status==INKTAG_STATUS_OK && m_slideshow_active && m_slideshow_interval==60 && m_slideshow_next_at==160);
 assert(m_slideshow_current_id==7);run_queued();assert(m_slideshow_current_id==9);
}
static void admission_modes(void) {
 ble_epd_t p=device();
 const uint8_t modes[]={INKTAG_DISPLAY_MODE_DEVICE_INFO,INKTAG_DISPLAY_MODE_CALENDAR,
   INKTAG_DISPLAY_MODE_CLOCK,INKTAG_DISPLAY_MODE_CLEAR,INKTAG_DISPLAY_MODE_RESOURCE};
 for(unsigned i=0;i<sizeof(modes);i++) {
  p.config.display_mode=EPD_DISPLAY_STATE_PICTURE;
  m_display_state=(epd_display_state_t){EPD_DISPLAY_STATE_SLIDESHOW,7,0x8000,30};
  epd_display_state_t saved=m_display_state;
  now=100;m_slideshow_active=true;m_slideshow_current_id=7;m_slideshow_interval=30;m_slideshow_next_at=200;
  m_button_slideshow_return_valid=true;m_native_timer_update_pending=true;
  admissions=0;refuse_on_admission=2;uint8_t request[]={modes[i],9};
  epd_v2_set_display_mode(&p,INKTAG_CMD_SET_DISPLAY_MODE,1,request,2);
  assert(admissions==2 && !m_epd_operation_pending && !queued && response_status!=INKTAG_STATUS_OK);
  assert(m_slideshow_active && m_slideshow_current_id==7 && m_slideshow_interval==30 && m_slideshow_next_at==200);
  assert(m_button_slideshow_return_valid && m_native_timer_update_pending && p.config.display_mode==EPD_DISPLAY_STATE_PICTURE);
  assert(!memcmp(&saved,&m_display_state,sizeof(saved)));
  refuse_on_admission=0;epd_v2_set_display_mode(&p,INKTAG_CMD_SET_DISPLAY_MODE,2,request,2);
  assert(response_status==INKTAG_STATUS_OK && m_epd_operation_pending);
  assert(m_slideshow_active==(modes[i]==INKTAG_DISPLAY_MODE_RESOURCE));run_queued();
 }
}
static void boot(void) {
 ble_epd_t p=device();m_display_state=(epd_display_state_t){MODE_CLOCK,255,0x8000,0};
 assert(ble_epd_restore_display(&p,0,false,false,true) && admissions==0 && writes==0);
 m_display_state.reserved|=EPD_DISPLAY_STATE_FLAG_BOOT_REFRESH;
 assert(ble_epd_restore_display(&p,0,true,true,true) && admissions==0 && writes==0);
 assert(!ble_epd_restore_display(&p,0,false,false,false) && admissions==0);
 now=1;assert(ble_epd_restore_display(&p,now,false,false,true));run_queued();assert(admissions==1);
 assert(m_display_state.reserved==0x8001 && writes==0);
 allowed=false;bool pending=true;for(unsigned i=0;i<30 && pending;i++)pending=!ble_epd_restore_display(&p,now,false,false,true);
 assert(!pending && admissions==2);
 allowed=true;m_epd_operation_active=true;for(unsigned i=0;i<30;i++){assert(!ble_epd_restore_display(&p,now,false,false,true));}
 assert(admissions==2);m_epd_operation_active=false;
 assert(!ble_epd_restore_display(&p,now,true,false,false) && admissions==2);
 assert(ble_epd_restore_display(&p,now,true,false,true));run_queued();assert(m_display_state.mode==EPD_DISPLAY_STATE_DEVICE_INFO);
 m_display_state.reserved=0x8000;unsigned after_first=admissions;
 assert(ble_epd_restore_display(&p,now,false,false,true) && admissions==after_first && !queued);
 allowed=false;assert(ble_epd_restore_display(&p,now,true,false,true));
 assert(admissions==after_first+1 && !queued && !m_epd_operation_pending);
 allowed=true;
 m_display_state=(epd_display_state_t){EPD_DISPLAY_STATE_SLIDESHOW,7,0x8001,30};p.config.display_mode=EPD_DISPLAY_STATE_PICTURE;
 replayable=false;next_available=false;
 assert(ble_epd_restore_display(&p,now,false,false,true));
 assert(m_slideshow_active && m_slideshow_current_id==7 && m_slideshow_next_at==now+30);
 next_available=true;now=m_slideshow_next_at;ble_epd_on_timer(&p,now-1,now,false);run_queued();assert(m_slideshow_current_id==9);
}
static void boot_remap(void) {
 ble_epd_t p=device();
 /* GUI mode3 and persisted CLEAR3 deliberately collide. Read the real GUI
  * enum above; aliasing it to persisted DEVICE_INFO5 would hide this bug. */
 assert((unsigned)MODE_DEVICE_INFO==(unsigned)EPD_DISPLAY_STATE_CLEAR);
 const uint8_t modes[]={EPD_DISPLAY_STATE_PICTURE,EPD_DISPLAY_STATE_CALENDAR,
  EPD_DISPLAY_STATE_CLOCK,EPD_DISPLAY_STATE_CLEAR,EPD_DISPLAY_STATE_SLIDESHOW,
  EPD_DISPLAY_STATE_DEVICE_INFO};
 for(unsigned i=0;i<sizeof(modes);++i) {
  m_display_state=(epd_display_state_t){modes[i],1,EPD_DISPLAY_STATE_FLAG_BOOT_REFRESH,30};
  assert(ble_epd_restore_display(&p,123,true,false,true));
  assert(queued && m_epd_pending_operation.type==NRF_EPD_OP_GUI);
  assert(m_epd_pending_operation.arg==MODE_DEVICE_INFO);
  run_queued();assert(last_gui_mode==MODE_DEVICE_INFO && clear_calls==0);
  assert(m_display_state.mode==EPD_DISPLAY_STATE_DEVICE_INFO);
 }
 m_display_state=(epd_display_state_t){EPD_DISPLAY_STATE_DEVICE_INFO,255,EPD_DISPLAY_STATE_FLAG_BOOT_REFRESH,0};
 assert(ble_epd_restore_display(&p,124,false,false,true));
 assert(queued && m_epd_pending_operation.type==NRF_EPD_OP_GUI);
 assert(m_epd_pending_operation.arg==MODE_DEVICE_INFO);
 run_queued();assert(gui_calls==7 && clear_calls==0);
 for(unsigned color=0;color<2;++color) {
  m_display_state=(epd_display_state_t){EPD_DISPLAY_STATE_CLEAR,color,EPD_DISPLAY_STATE_FLAG_BOOT_REFRESH,0};
  assert(ble_epd_restore_display(&p,125,false,false,true));
  assert(queued && m_epd_pending_operation.type==NRF_EPD_OP_CLEAR);
  assert(m_epd_pending_operation.arg==color);run_queued();
  assert(m_display_state.mode==EPD_DISPLAY_STATE_CLEAR && m_display_state.argument==color);
 }
 assert(gui_calls==7 && clear_calls==2);
}
int main(int argc,char **argv) {
 assert(argc==2);
 if(!strcmp(argv[1],"native"))native();else if(!strcmp(argv[1],"failure"))failure();
 else if(!strcmp(argv[1],"slideshow"))slideshow();else if(!strcmp(argv[1],"modes"))modes();
 else if(!strcmp(argv[1],"admission-start"))admission_start(false);
 else if(!strcmp(argv[1],"admission-restart"))admission_start(true);
 else if(!strcmp(argv[1],"admission-modes"))admission_modes();
 else if(!strcmp(argv[1],"boot"))boot();
 else if(!strcmp(argv[1],"boot-remap"))boot_remap();else assert(false);
 puts("shipping nRF11 refresh continuity case passed");return 0;
}
'''

BOOT = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "inktag_page_cycle.h"
static uint32_t settings_words[16];
#define INKTAG_BOARD_DFU_SETTINGS_START ((uintptr_t)settings_words)
#define DFU_SETTINGS_VERSION_OFFSET 4u
#define DFU_SETTINGS_BANK0_SIZE_OFFSET 0x18u
#define DFU_SETTINGS_BANK0_CRC_OFFSET 0x1cu
#define DFU_SETTINGS_BANK0_CODE_OFFSET 0x20u
#define DFU_BANK_VALID_APP 1u
#define BOOT_DISPLAY_MAGIC 0x3153424eu
#define BOOT_DISPLAY_VERSION 1u
#define BOOT_DISPLAY_FIRST_REQUEST 1u
#define CONFIG_STORE_EXTERNAL 1
#define INKTAG_NRF_CONFIG_BOOT_DISPLAY 12u
'''

BOOT_TEST = r'''
static boot_display_record_t saved;
static bool present, accept=true;
static unsigned writes;
static bool inktag_nrf_config_store_read(unsigned type,void *data,uint16_t size,uint16_t *length) {
 assert(type==INKTAG_NRF_CONFIG_BOOT_DISPLAY && size==sizeof(saved));
 if(!present)return false;
 memcpy(data,&saved,size);*length=size;return true;
}
static bool inktag_nrf_config_store_write(unsigned type,const void *data,uint16_t size) {
 assert(type==INKTAG_NRF_CONFIG_BOOT_DISPLAY && size==sizeof(saved));++writes;
 if(!accept)return false;
 memcpy(&saved,data,size);present=true;return true;
}
'''

BOOT_MAIN = r'''
int main(void) {
 bool first=false,ota=false;settings_words[1]=2;settings_words[6]=69552;settings_words[7]=123;settings_words[8]=1;
 assert(!inktag_nrf_boot_display_context(&first,&ota) && !writes);
 saved=(boot_display_record_t){.magic=BOOT_DISPLAY_MAGIC,.version=1,.flags=1,.app_size=69552,.app_crc=123};present=true;
 assert(inktag_nrf_boot_display_context(&first,&ota) && first && !ota && writes==1);
 assert(inktag_nrf_boot_display_context(&first,&ota) && !first && !ota && writes==1);
 saved.flags=1;assert(inktag_nrf_boot_display_context(&first,&ota) && first && !ota && writes==2);
 assert(inktag_nrf_boot_display_context(&first,&ota) && !first && !ota && writes==2);
 saved.flags=1;settings_words[7]=456;
 assert(inktag_nrf_boot_display_context(&first,&ota) && first && ota && writes==3);
 epd_display_state_t state={EPD_DISPLAY_STATE_CLOCK,255,0x8001,0};
 assert(inktag_boot_display_plan(true,true,&state).target==INKTAG_BOOT_DISPLAY_NONE);
 assert(inktag_boot_display_plan(true,true,&state).consume_first_display_request);
 assert(inktag_nrf_boot_display_context(&first,&ota) && !first && !ota && writes==3);
 settings_words[7]=567;
 assert(inktag_nrf_boot_display_context(&first,&ota) && !first && ota && writes==4);
 assert(inktag_nrf_boot_display_context(&first,&ota) && !first && !ota && writes==4);
 saved.flags=1;settings_words[7]=789;accept=false;
 assert(!inktag_nrf_boot_display_context(&first,&ota) && !first && !ota);
 assert(saved.flags==1 && saved.app_crc==567 && writes==5);
 accept=true;assert(inktag_nrf_boot_display_context(&first,&ota) && first && ota && writes==6);
 assert(inktag_boot_display_plan(first,ota,&state).target==INKTAG_BOOT_DISPLAY_NONE);
 assert(inktag_nrf_boot_display_context(&first,&ota) && !first && !ota && writes==6);
 puts("shipping complete-flashing marker single-use / OTA checkpoint / write refusal passed");return 0;
}
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--expect-old-failures',action='store_true')
    parser.add_argument('--evidence-dir',type=Path)
    parser.add_argument('--case',action='append')
    args=parser.parse_args()
    service=(ROOT/'compat/common/epd/inktag_epd_service.c').read_text(encoding='utf-8')
    config=(ROOT/'compat/nordic/src/inktag_nrf_config_store.c').read_text(encoding='utf-8')
    protocol=(ROOT/'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text(encoding='utf-8')
    types=re.search(r'typedef enum \{\s*NRF_EPD_OP_NONE[\s\S]*?} nrf_epd_operation_type_t;',service).group()
    types+='\n'+re.search(r'typedef struct \{\s*ble_epd_t\* p_epd;[\s\S]*?} nrf_epd_operation_t;',service).group()
    gui=(ROOT/'compat/common/gui/inktag_gui.h').read_text(encoding='utf-8')
    gui_modes=re.search(r'typedef enum \{\s*MODE_PICTURE[\s\S]*?} display_mode_t;',gui).group()
    code=PREFIX.replace('__SHIPPING_GUI_MODE_ENUM__',gui_modes)+types+'\nstatic nrf_epd_operation_t m_epd_pending_operation,m_epd_active_operation;\n'
    code+='static void epd_v2_operation_complete(const nrf_epd_operation_t *op,inktag_status_v2_t status) {(void)op;completion=status;++completed;}\n'
    names=['epd_slideshow_stop','epd_record_display_state']
    if 'static bool epd_automatic_display_refresh_allowed(' in service:
        names+=['epd_display_state_flag','epd_automatic_display_refresh_allowed','epd_persist_display_state_flags']
    names+=['epd_operation_admit','epd_schedule_dispatch','epd_schedule_operation','epd_schedule_background_operation',
            'epd_operation_dispatch','epd_stop_current_operation']
    if 'epd_restore_input_display(' in function(service,'ble_epd_restore_display'):
        names+=['epd_restore_input_display']
    names+=['epd_native_timer_update_try','ble_epd_on_timer','ble_epd_restore_display']
    code+='\n'.join(function(service,n) for n in names)
    code+='\n'+function(protocol,'epd_v2_own_operation')+'\n'+function(protocol,'epd_v2_schedule_operation')+'\n'+function(protocol,'epd_v2_set_display_mode')+TEST
    boot_type=re.search(r'typedef struct \{\s*uint32_t magic;\s*uint8_t version;[\s\S]*?} boot_display_record_t;',config).group()
    boot_code=BOOT+boot_type+BOOT_TEST+function(config,'inktag_nrf_boot_display_context')+BOOT_MAIN
    with tempfile.TemporaryDirectory() as temporary:
        directory=args.evidence_dir or Path(temporary)
        directory.mkdir(parents=True,exist_ok=True)
        for name,source in (('scheduler',code),('boot-context',boot_code)):
            path=directory/(name+'.c');path.write_text(source,encoding='utf-8')
            for variant,flags in (('strict',['-O2']),('asan-ubsan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie'])):
                binary=directory/(name+'-'+variant)
                subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',*flags,
                    '-Ifirmware/common/epd','-Ifirmware/common/input','-Ifirmware/common/ble',str(path),
                    'compat/common/epd/inktag_page_cycle.c','-o',str(binary)],cwd=ROOT,check=True)
                cases=('native','failure','slideshow','modes','admission-start','admission-restart','admission-modes','boot','boot-remap') if name=='scheduler' else ('marker',)
                if args.case:
                    cases=tuple(case for case in cases if case in args.case)
                for case in cases:
                    result=subprocess.run([str(binary),case],capture_output=True,text=True)
                    (directory/(name+'-'+variant+'-'+case+'.stdout.log')).write_text(result.stdout)
                    (directory/(name+'-'+variant+'-'+case+'.stderr.log')).write_text(result.stderr)
                    expected_failure=args.expect_old_failures and name=='scheduler'
                    assert (result.returncode!=0)==expected_failure,(name,variant,case,result.returncode,result.stdout,result.stderr)
                    print(('EXPECTED OLD FAILURE' if expected_failure else 'PASS')+' '+name+' '+variant+' '+case,flush=True)


if __name__=='__main__': main()
