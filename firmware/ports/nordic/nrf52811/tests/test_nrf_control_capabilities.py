"""Run production test/time cases through the real parser, mailbox and scheduler.

Only transport, SDK timers, NOR writes and elapsed-counter sampling are mocked.
The board, RUN_TEST cases, native time encoder, wall-clock setter and locator
state machine are production code. No SDK, Git objects or hardware are needed.
"""
from datetime import datetime, timezone
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PROTOCOL = (ROOT/'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text(encoding='utf-8')
MAIN = (ROOT/'compat/nordic/src/main.c').read_text(encoding='utf-8')
SERVICE = (ROOT/'compat/common/epd/inktag_epd_service.c').read_text(encoding='utf-8')
NATIVE = (ROOT/'compat/nordic/src/inktag_nrf_native_v1.inc').read_text(encoding='utf-8')
SCREEN = (ROOT/'compat/common/epd/inktag_epd_screen.c').read_text(encoding='utf-8')
MAKE = (ROOT/'compat/nordic/nrf52.mk').read_text(encoding='utf-8')
for option in ('INKTAG_NRF_TEST_ENABLED=1', 'INKTAG_NRF_INDICATOR_ENABLED=1', 'INKTAG_NRF_FIND_ENABLED=0'):
    assert '-D'+option in MAKE, option


def function(source, name):
    match = re.search(r'^(?:static )?[^\n;{}]+\b'+name+r'\([^;{]*\)\s*\{', source, re.M)
    assert match, name
    depth, end = 1, match.end()
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


PREFIX = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "inktag_board.h"
#include "inktag_protocol_core.h"
#include "inktag_release_v1.h"
#include "inktag_status.h"
#include "inktag_nrf_unsupported_status.h"
#define INKTAG_NRF_TEST_ENABLED 1
#define INKTAG_NRF_INDICATOR_ENABLED 1
#define INKTAG_NRF_IMAGE_STORE_ENABLED 1
#define INKTAG_NRF_SIMPLE_IMAGE_METADATA 1
#define INKTAG_NRF_CAPABILITY_IMAGE_METADATA UINT32_C(0x10000000)
#define INKTAG_NRF_FIND_ENABLED 0
#define INKTAG_NRF_ACTIVATION_ENABLED 0
#define INKTAG_NRF_CMD_SET_IMAGE_METADATA 0x1e
#define INKTAG_NRF_V2_OPERATION_BLE 1
#include "inktag_settings_records.h"
#define INKTAG_NRF_CONFIG_TIME INKTAG_SETTINGS_RECORD_TIME
#define INKTAG_NRF_HEALTH_OPERATION_BLE 1
#define INKTAG_POWER_OPERATION_FLASH 1
#define INKTAG_POWER_OPERATION_DISPLAY 2
#define MODE_CALENDAR 1
#define MODE_CLOCK 2
#define NRF_SUCCESS 0
#define EPD_V2_STORE_BUSY() false
#define CLOCK_TIMER_INTERVAL 1
typedef struct {struct {uint8_t week_start,display_mode;} config;void *epd;
 struct {uint16_t cccd_handle,value_handle;} char_handles;} ble_epd_t;
typedef struct {uint16_t handle,len;uint8_t data[32];} ble_gatts_evt_write_t;
typedef struct {struct {uint16_t evt_id;} header;struct {struct {uint16_t conn_handle;
 struct {ble_gatts_evt_write_t write;struct {uint16_t count;} hvn_tx_complete;} params;} gatts_evt;} evt;} ble_evt_t;
#define BLE_GATTS_EVT_WRITE 1u
#define BLE_GAP_EVT_CONNECTED 2u
#define BLE_GAP_EVT_DISCONNECTED 3u
static unsigned notification_changes,connection_events,scheduler_mailboxes,health_stages;
static bool ble_srv_is_notification_enabled(const uint8_t *p){return (p[0]&1u)!=0;}
static void epd_notifications_set(ble_epd_t*p,bool on){(void)p;(void)on;notification_changes++;}
static void on_connect(ble_epd_t*p,ble_evt_t*e){(void)p;(void)e;connection_events++;}
static void on_disconnect(ble_epd_t*p,ble_evt_t*e){(void)p;(void)e;connection_events++;}
typedef enum {NRF_EPD_OP_NONE,NRF_EPD_OP_CLEAR,NRF_EPD_OP_REFRESH,NRF_EPD_OP_GUI,NRF_EPD_OP_IMAGE_SHOW} nrf_epd_operation_type_t;
typedef struct {ble_epd_t *p_epd;uint32_t timestamp;uint8_t type,arg,background,origin,v2_opcode,v2_transaction;} nrf_epd_operation_t;
static uint32_t m_timestamp=1800000000u, m_time_checkpoint, m_time_checkpoint_retry_at;
static bool m_time_valid=true, m_time_checkpoint_valid;
static int m_monotonic_clock;
static unsigned critical;
#define CRITICAL_REGION_ENTER() (++critical)
#define CRITICAL_REGION_EXIT() (--critical)
static int16_t m_v2_timezone_minutes=480;
static bool m_epd_operation_active,m_epd_operation_pending,m_epd_cancel_requested;
static nrf_epd_operation_t m_epd_active_operation,m_epd_pending_operation;
static bool m_epd_dispatch_scheduled,m_picture_io_active,m_native_timer_update_pending,display_queue_fail;
static struct {bool active;} m_v2_transfer;
static bool m_v2_rx_pending,m_v2_rx_processing,scheduler_fail,storage_fail,low_power;
static uint8_t m_v2_rx_packet[512];static uint16_t m_v2_rx_length;
static ble_epd_t *m_v2_display_read_service;
static bool m_v2_led_test_active;
static uint32_t m_v2_led_test_deadline;
static inktag_status_runtime_t m_v2_indicator_runtime;
static unsigned responses,events,writes,timer_starts,timer_stops,rebases,led_calls,ticks;
static uint8_t status,response[32],event_payload[32],last_led;
static uint8_t last_opcode;
static uint16_t response_length,event_length;
static uint8_t durable[16];
static bool sensor_valid=true;
static unsigned sensor_samples;
static int16_t sensor_temperature=-160;
static void inktag_nrf_health_temperature_sample(void){sensor_samples++;}
static bool inktag_nrf_health_mcu_temperature(int16_t *value){if(sensor_valid)*value=sensor_temperature;return sensor_valid;}
static uint16_t inktag_nrf_health_voltage_mv(bool force){assert(force);sensor_samples++;return 2999;}
static bool inktag_nrf_health_voltage_valid(void){return sensor_valid;}

static bool m_slideshow_active;
static uint32_t m_slideshow_next_at,m_slideshow_interval=3600,m_native_timer_update_timestamp;
static uint8_t m_slideshow_current_id;
static bool inktag_image_store_busy(void){return false;}
static bool epd_slideshow_select_next(ble_epd_t*p,uint8_t*id){(void)p;(void)id;return false;}
static bool resource_backend=true;
static bool inktag_image_store_supported(void){return resource_backend;}
static uint32_t timestamp(void){return m_timestamp;}
static uint32_t app_monotonic_now_ms(void){return 500u;}
static void clock_sample(void){}
static void inktag_monotonic_clock_rebase_wall(int *p){assert(p==&m_monotonic_clock);rebases++;}
static void inktag_nrf_led_apply(uint8_t mask){led_calls++;last_led=mask;}
static void epd_v2_indicator_tick(void){ticks++;inktag_status_tick(&m_v2_indicator_runtime,500u);}
static void epd_v2_send_response(ble_epd_t *p,uint8_t op,uint8_t tx,uint8_t st,const uint8_t *b,uint16_t n){
 (void)p;assert(tx==19);responses++;last_opcode=op;status=st;response_length=n;
 assert(n<=sizeof(response));if(n)memcpy(response,b,n);
}
static unsigned ble_epd_string_send(ble_epd_t *p,uint8_t *b,uint16_t n){
 assert(n==3);epd_v2_send_response(p,b[0],b[1],b[2],NULL,0);return 0;
}
static void epd_v2_send_event(ble_epd_t *p,uint8_t event,uint8_t st,uint8_t op,uint8_t tx,const uint8_t *b,uint16_t n){
 (void)p;assert((event==INKTAG_EVENT_COMPLETE||event==INKTAG_EVENT_PROGRESS)&&st==INKTAG_STATUS_OK&&op==INKTAG_CMD_RUN_TEST&&tx==19);
 if(event==INKTAG_EVENT_PROGRESS)return;
 events++;event_length=n;assert(n<=sizeof(event_payload));if(n)memcpy(event_payload,b,n);
}
static bool inktag_nrf_config_store_read(unsigned kind,void *p,unsigned n,uint16_t *length){
 assert(kind==INKTAG_NRF_CONFIG_TIME&&n==16);memcpy(p,durable,16);*length=16;return true;
}
static bool inktag_nrf_config_store_write(unsigned kind,const void *p,unsigned n){
 assert(kind==INKTAG_NRF_CONFIG_TIME&&n==16);writes++;if(storage_fail)return false;memcpy(durable,p,n);return true;
}
static void inktag_nrf_health_stage(unsigned tag,unsigned op){(void)tag;(void)op;health_stages++;}
static bool inktag_nrf_health_operation_allowed(unsigned op,bool b){(void)op;(void)b;return !low_power;}
static inktag_status_v2_t epd_v2_display_settings_write_access(const uint8_t*p,unsigned n){(void)p;(void)n;return INKTAG_STATUS_UNSUPPORTED;}
static void epd_slideshow_stop(ble_epd_t *p,bool b){(void)p;(void)b;}
static void (*scheduled_fn)(void*,uint16_t),(*scheduled_display)(void*,uint16_t);static ble_epd_t *scheduled_device;
static void epd_send_text(ble_epd_t*p,const char*s){(void)p;(void)s;}
static void epd_operation_dispatch(void*,uint16_t);
static int app_sched_event_put(void*p,unsigned n,void(*fn)(void*,uint16_t)){
 if(n==0){assert(!p);if(display_queue_fail)return 1;scheduled_display=fn;return 0;}
 assert(n==sizeof(ble_epd_t*));scheduler_mailboxes++;if(scheduler_fail)return 1;scheduled_fn=fn;scheduled_device=*(ble_epd_t**)p;return 0;
}
static void epd_v2_process_queued(void*,uint16_t);
'''

DRIVER = r'''
static ble_epd_t device={.config={1,1},.epd=(void*)1,.char_handles={0x10,0x11}};
static void reset(void){
 responses=events=writes=timer_starts=timer_stops=rebases=led_calls=ticks=0;
 notification_changes=connection_events=scheduler_mailboxes=health_stages=0;
 status=255;sensor_samples=0;sensor_valid=true;response_length=event_length=0;scheduled_fn=scheduled_display=NULL;
 m_epd_dispatch_scheduled=m_picture_io_active=m_native_timer_update_pending=display_queue_fail=false;
 device.config.week_start=1;device.config.display_mode=1;
 scheduler_fail=storage_fail=low_power=false;m_v2_rx_pending=m_v2_rx_processing=false;m_v2_display_read_service=NULL;m_epd_cancel_requested=false;
 m_epd_operation_active=m_epd_operation_pending=m_v2_transfer.active=false;
 m_timestamp=1800000000u;m_v2_timezone_minutes=480;m_time_valid=true;m_time_checkpoint_valid=false;
 memset(durable,0xa5,sizeof(durable));memset(response,0,sizeof(response));
 m_v2_led_test_active=false;inktag_status_config_t config;inktag_status_config_default(&config);
 config.policy=INKTAG_STATUS_POLICY_OFF;inktag_status_init(&m_v2_indicator_runtime,&config,7,NULL,NULL,500);
}
static void ingress(uint8_t op,const uint8_t*p,unsigned n){
 ble_evt_t event={.header={BLE_GATTS_EVT_WRITE}};
 ble_gatts_evt_write_t *write=&event.evt.gatts_evt.params.write;
 write->handle=device.char_handles.value_handle;write->len=n+3;
 write->data[0]=op;write->data[1]=19;write->data[2]=0x10;
 assert(n<=29);if(n)memcpy(write->data+3,p,n);
 ble_epd_evt_handler(&event,&device);
}
static void request(uint8_t op,const uint8_t*p,unsigned n){
 ingress(op,p,n);
 if(scheduled_fn){void(*fn)(void*,uint16_t)=scheduled_fn;scheduled_fn=NULL;fn(&scheduled_device,sizeof(scheduled_device));}
}
static void unchanged(void){
 assert(m_timestamp==1800000000u&&m_v2_timezone_minutes==480&&m_time_valid);
 assert(!timer_starts&&!timer_stops&&!rebases&&!events);
 assert(device.config.week_start==1&&device.config.display_mode==1);
 for(unsigned i=0;i<sizeof(durable);i++)assert(durable[i]==0xa5);
}
static void time_request(uint32_t utc,int16_t zone,uint8_t week,unsigned length){
 uint8_t b[10]={0};inktag_write_u32_le(b,utc);inktag_write_u16_le(b+4,(uint16_t)zone);b[6]=week;
 request(INKTAG_CMD_SET_TIME,b,length);
}
int main(void){
 uint32_t random=0x71c4a3b9u;
 for(unsigned i=0;i<200000;i++){
  random=random*1664525u+1013904223u;uint32_t base=random;
  random=random*1664525u+1013904223u;int32_t delta=(int32_t)random;uint32_t result=0x5a5a5a5au;
  int64_t oracle=(int64_t)base+delta;bool valid=epd_v2_time_offset(base,delta,&result);
  assert(valid==(oracle>=0&&oracle<=UINT32_MAX));assert(result==(valid?(uint32_t)oracle:0x5a5a5a5au));
 }
 _Static_assert(INKTAG_BOARD_INPUT_QUALIFIED==0&&INKTAG_BOARD_BUTTON_PRESENT==0,"actual board has no qualified input");
 _Static_assert(INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED==1,"actual RGB board");
 const uint32_t present=INKTAG_CAPABILITY_TIME|INKTAG_CAPABILITY_PIN_CONFIG|INKTAG_CAPABILITY_SCREEN_PROFILE|
  INKTAG_CAPABILITY_TEST|INKTAG_CAPABILITY_INDICATOR|INKTAG_CAPABILITY_TELEMETRY|INKTAG_CAPABILITY_HEALTH;
 const uint32_t absent=INKTAG_CAPABILITY_INPUT|INKTAG_CAPABILITY_IO|INKTAG_CAPABILITY_NFC|INKTAG_CAPABILITY_LAYOUT|
  INKTAG_CAPABILITY_ACTIVATION|INKTAG_CAPABILITY_FIND|INKTAG_CAPABILITY_DEVICE_PASSWORD|INKTAG_CAPABILITY_CLOCK_PARTIAL_REFRESH;
 assert((epd_v2_capabilities()&present)==present&&!(epd_v2_capabilities()&absent));
 resource_backend=false;assert(!(epd_v2_capabilities()&(INKTAG_CAPABILITY_RESOURCES|INKTAG_CAPABILITY_SLIDESHOW)));
 resource_backend=true;assert((epd_v2_capabilities()&(INKTAG_CAPABILITY_RESOURCES|INKTAG_CAPABILITY_SLIDESHOW))==(INKTAG_CAPABILITY_RESOURCES|INKTAG_CAPABILITY_SLIDESHOW));
 const uint8_t unsupported[]={
  INKTAG_CMD_GET_INPUT_CONFIG,INKTAG_CMD_SET_INPUT_CONFIG,
  INKTAG_CMD_GET_FIND_CAPABILITIES,INKTAG_CMD_GET_FIND_CONFIG,INKTAG_CMD_SET_FIND_CONFIG,INKTAG_CMD_FIND_ACTION,
  INKTAG_CMD_GET_FIND_OWNER_STATUS,INKTAG_CMD_BEGIN_FIND_OWNER_ROTATION,INKTAG_CMD_STAGE_FIND_OWNER_ROTATION,
  INKTAG_CMD_COMMIT_FIND_OWNER_ROTATION,INKTAG_CMD_ABORT_FIND_OWNER_ROTATION,
  INKTAG_CMD_GET_ACTIVATION_STATUS,INKTAG_CMD_SET_ACTIVATION_CODE,INKTAG_CMD_GET_ACTIVATION_REQUEST,
  INKTAG_CMD_GET_NFC_CAPABILITIES,INKTAG_CMD_GET_NFC_STATUS,INKTAG_CMD_GET_NFC_NDEF,INKTAG_CMD_SET_NFC_NDEF,
  INKTAG_CMD_GET_LAYOUT_CAPABILITIES,INKTAG_CMD_VALIDATE_LAYOUT,INKTAG_CMD_ACTIVATE_LAYOUT,INKTAG_CMD_GET_LAYOUT_STATUS,
  INKTAG_CMD_GET_SECURITY_INFO,INKTAG_CMD_OPEN_DEBUG_SESSION,INKTAG_CMD_DEBUG_READ,
  INKTAG_CMD_CLOSE_DEBUG_SESSION,INKTAG_CMD_ABORT_DEBUG_OPERATION,INKTAG_CMD_DEBUG_EXECUTE};
 for(unsigned i=0;i<sizeof(unsupported);i++)assert(!epd_v2_command_supported_on_target(unsupported[i]));
 /* Frozen InkTag wire contract, independent of the production packed table. */
 const uint8_t implemented[]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,
                             24,25,26,27,28,34,35,36,37,112,113,114};
 const uint8_t idle_commands[]={8,9,10,16,27,28,35,113};
 unsigned admitted=0,allocated=0,negative_cases=0;
 for(unsigned candidate=0;candidate<256;candidate++){
  bool expected=memchr(implemented,candidate,sizeof(implemented))!=NULL;
  assert(epd_v2_command_supported_on_target(candidate)==expected);
  assert(epd_v2_command_requires_idle_display(candidate)==
         (memchr(idle_commands,candidate,sizeof(idle_commands))!=NULL));
  assert(epd_v2_command_admission_status(candidate)==(expected?INKTAG_STATUS_OK:
         inktag_nrf_unhandled_command_status(candidate)));
  allocated+=inktag_nrf_unhandled_command_status(candidate)==INKTAG_STATUS_UNSUPPORTED;
  if(expected){admitted++;continue;}
  for(unsigned busy=0;busy<2;busy++)for(unsigned power=0;power<2;power++)for(unsigned length=0;length<2;length++)for(unsigned mailbox=0;mailbox<4;mailbox++){
   reset();m_epd_operation_active=busy;low_power=power;
   m_v2_rx_pending=(mailbox&1u)!=0;m_v2_rx_processing=(mailbox&2u)!=0;
   m_v2_rx_length=7;memset(m_v2_rx_packet,0x5a,sizeof(m_v2_rx_packet));
   uint8_t before[sizeof(m_v2_rx_packet)];memcpy(before,m_v2_rx_packet,sizeof(before));
   m_epd_cancel_requested=true;m_native_timer_update_pending=true;m_v2_display_read_service=&device;
   uint8_t body[7]={0xa5,1,2,3,4,5,6};ingress(candidate,body,length?sizeof(body):0);
   assert(!scheduler_mailboxes&&!scheduled_fn&&!notification_changes&&!connection_events&&!health_stages);
   assert(m_v2_rx_pending==((mailbox&1u)!=0)&&m_v2_rx_processing==((mailbox&2u)!=0));
   assert(m_v2_rx_length==7&&!memcmp(before,m_v2_rx_packet,sizeof(before)));
   assert(m_epd_cancel_requested&&m_native_timer_update_pending&&m_v2_display_read_service==&device);
   assert(responses==1&&last_opcode==candidate&&status==inktag_nrf_unhandled_command_status(candidate)&&response_length==0);
   assert(!writes&&!led_calls&&!sensor_samples&&!m_epd_operation_pending&&!m_v2_transfer.active);
   assert(m_epd_operation_active==busy);unchanged();
   negative_cases++;
  }
 }
 assert(admitted==33&&allocated==69&&negative_cases==7136);
 unsigned aux_cases=0;
 for(unsigned busy=0;busy<4;busy++)for(unsigned power=0;power<2;power++)for(unsigned mailbox=0;mailbox<4;mailbox++)for(unsigned field=0;field<256;field++){
  reset();m_epd_operation_active=(busy&1u)!=0;m_epd_operation_pending=(busy&2u)!=0;low_power=power;
  m_v2_rx_pending=(mailbox&1u)!=0;m_v2_rx_processing=(mailbox&2u)!=0;
  m_v2_rx_length=7;memset(m_v2_rx_packet,0x5a,sizeof(m_v2_rx_packet));
  uint8_t before[sizeof(m_v2_rx_packet)];memcpy(before,m_v2_rx_packet,sizeof(before));
  uint8_t aux[12]={1};aux[3]=(uint8_t)field;ingress(INKTAG_CMD_SET_IO_CONFIG,aux,sizeof(aux));
  assert(responses==1&&status==INKTAG_STATUS_UNSUPPORTED&&last_opcode==INKTAG_CMD_SET_IO_CONFIG&&!response_length);
  assert(!scheduler_mailboxes&&!scheduled_fn&&!notification_changes&&!connection_events&&!health_stages);
  assert(!writes&&!led_calls&&!sensor_samples&&!m_v2_transfer.active);
  assert(m_epd_operation_active==((busy&1u)!=0)&&m_epd_operation_pending==((busy&2u)!=0));
  assert(m_v2_rx_pending==((mailbox&1u)!=0)&&m_v2_rx_processing==((mailbox&2u)!=0));
  assert(m_v2_rx_length==7&&!memcmp(before,m_v2_rx_packet,sizeof(before)));unchanged();aux_cases++;
 }
 assert(aux_cases==8192);puts("PASS8192 actual BLE IRQ InkTag AUX customization rejects before display/BUSY/ADC/mailbox; ordinary screen/LED PIN_CONFIG remains admitted");

 printf("PASS unified InkTag interface: all256 opcodes independently checked (33 implemented,36 known unavailable,187 unallocated); %u real BLE IRQ negative cases across all32 mailbox/display/power/payload states; %zu explicit missing-feature IDs\n",negative_cases,sizeof(unsupported));
 reset();request(INKTAG_CMD_LIST_TESTS,NULL,0);
 assert(responses==1&&status==INKTAG_STATUS_OK&&response_length==7);
 assert(response[0]==1&&response[1]==5&&response[2]==INKTAG_TEST_LED_CHANNEL&&response[3]==INKTAG_TEST_DEVICE_LOCATE&&response[4]==INKTAG_TEST_TIME_SHIFT&&response[5]==INKTAG_TEST_TIME_SET&&response[6]==INKTAG_TEST_ADC_READ);
 for(unsigned op=0;op<256;op++){
  reset();uint8_t b[10]={op,1};request(INKTAG_CMD_RUN_TEST,b,sizeof(b));
  assert(responses==1&&!events&&!writes&&!timer_starts&&!timer_stops);
  assert(status==((op==INKTAG_TEST_LED_CHANNEL||op==INKTAG_TEST_DEVICE_LOCATE||op==INKTAG_TEST_TIME_SHIFT||op==INKTAG_TEST_TIME_SET||op==INKTAG_TEST_ADC_READ)?INKTAG_STATUS_INVALID_REQUEST:INKTAG_STATUS_UNSUPPORTED));
 }
 for(unsigned mask=0;mask<8;mask++){
  reset();uint8_t b[]={INKTAG_TEST_LED_CHANNEL,mask};request(INKTAG_CMD_RUN_TEST,b,2);
  assert(status==INKTAG_STATUS_OK&&responses==1&&events==1&&event_length==1&&event_payload[0]==mask);
  assert(led_calls==1&&last_led==mask&&m_v2_led_test_deadline==10500u&&!writes);
 }
 for(unsigned value=0;value<3;value++){
  reset();m_v2_led_test_active=true;uint8_t b[]={INKTAG_TEST_DEVICE_LOCATE,value};request(INKTAG_CMD_RUN_TEST,b,2);
  assert(status==(value<2?INKTAG_STATUS_OK:INKTAG_STATUS_INVALID_REQUEST));
  assert(events==(unsigned)(value<2)&&!writes);
  if(value<2)assert(!m_v2_led_test_active&&ticks==1&&inktag_status_locate_active(&m_v2_indicator_runtime)==(value!=0));
 }
 for(unsigned channel=0;channel<4;channel++)for(unsigned valid=0;valid<2;valid++){
  reset();sensor_valid=valid;uint8_t adc[]={INKTAG_TEST_ADC_READ,channel};request(INKTAG_CMD_RUN_TEST,adc,2);
  if(channel<2){assert(status==INKTAG_STATUS_OK&&response_length==6&&response[0]==1&&response[1]==channel&&response[2]==valid&&events==1&&event_payload[0]==channel&&sensor_samples==1);}
  else assert(status==(channel==2?INKTAG_STATUS_UNSUPPORTED:INKTAG_STATUS_INVALID_REQUEST)&&!events&&!sensor_samples);
  if(!channel&&valid)assert((int16_t)inktag_read_u16_le(response+3)==-40&&(int8_t)response[5]==-10);
  if(channel==1)assert(inktag_read_u16_le(response+3)==2999);
  assert(!writes);
 }
 for(int t=-32768;t<=32764;t+=4){
  reset();sensor_temperature=t;uint8_t b[]={INKTAG_TEST_ADC_READ,0};request(INKTAG_CMD_RUN_TEST,b,2);
  assert(status==INKTAG_STATUS_OK&&inktag_read_u16_le(response+3)==(uint16_t)(t/4));
 }
 sensor_temperature=-160;
 reset();uint8_t led[]={INKTAG_TEST_LED_CHANNEL,1};scheduler_fail=true;request(INKTAG_CMD_RUN_TEST,led,2);
 assert(status==INKTAG_STATUS_INTERNAL_ERROR&&!events&&!led_calls);unchanged();
 reset();m_epd_operation_active=true;request(INKTAG_CMD_RUN_TEST,led,2);
 assert(status==INKTAG_STATUS_BUSY&&!events&&!led_calls);unchanged();
 reset();request(INKTAG_CMD_GET_TEST_STATUS,NULL,0);assert(status==INKTAG_STATUS_OK&&response_length==3&&response[0]==1);
 for(unsigned i=0;i<100;i++){
  reset();assert(epd_time_state_write(years[i],0));assert(writes==1&&!timer_starts&&!timer_stops);
  uint32_t saved;int16_t saved_zone;assert(epd_time_state_read(&saved,&saved_zone)&&saved==years[i]&&saved_zone==0);
  reset();time_request(years[i],0,1,7);assert(responses==1&&status==INKTAG_STATUS_OK);
  assert(m_timestamp==years[i]&&m_v2_timezone_minutes==0&&writes==1&&!timer_starts&&!timer_stops&&rebases==1&&!events&&m_time_valid&&!critical);
 }
 for(unsigned length=0;length<=10;length++)if(length!=7){reset();time_request(1800000000u,0,1,length);assert(status==INKTAG_STATUS_INVALID_REQUEST&&!writes);unchanged();}
 const int zones[]={-841,841};
 for(unsigned i=0;i<2;i++){reset();time_request(1800000000u,zones[i],1,7);assert(status==INKTAG_STATUS_INVALID_REQUEST&&!writes);unchanged();}
 reset();time_request(1800000000u,0,2,7);assert(status==INKTAG_STATUS_INVALID_REQUEST&&!writes);unchanged();
 reset();m_time_valid=false;time_request(0,0,1,7);assert(status==INKTAG_STATUS_OK&&m_timestamp==0&&m_time_valid);
 reset();time_request(UINT32_MAX,0,1,7);assert(status==INKTAG_STATUS_OK&&m_timestamp==UINT32_MAX&&m_time_valid);
 reset();time_request(0,-1,1,7);assert(status==INKTAG_STATUS_INVALID_REQUEST&&!writes);unchanged();
 reset();time_request(UINT32_MAX,1,1,7);assert(status==INKTAG_STATUS_INVALID_REQUEST&&!writes);unchanged();
 for(int zone=-840;zone<=840;zone+=1680){
  reset();uint32_t utc=(uint32_t)((int64_t)946684800u-zone*60);time_request(utc,zone,1,7);
  assert(status==INKTAG_STATUS_OK&&m_timestamp==946684800u&&m_v2_timezone_minutes==zone);
  assert(inktag_read_u32_le(response)==utc&&(int16_t)inktag_read_u16_le(response+4)==zone);
 }
 for(unsigned mode=1;mode<=2;mode++)for(unsigned week=0;week<=6;week++){
  reset();uint8_t b[9]={INKTAG_TEST_TIME_SET,mode};inktag_write_u32_le(b+2,946684800u);inktag_write_u16_le(b+6,0);b[8]=week;
  request(INKTAG_CMD_RUN_TEST,b,9);assert(status==INKTAG_STATUS_OK&&m_timestamp==946684800u&&device.config.week_start==week&&device.config.display_mode==mode);
  assert(m_epd_operation_pending&&m_epd_pending_operation.timestamp==946684800u&&!events&&response_length==7);
  scheduled_display(NULL,0);assert(events==1&&event_payload[0]==mode&&!m_epd_operation_pending);
  uint32_t next=946684800u+(mode==MODE_CLOCK?60u:86400u);
  ble_epd_on_timer(&device,946684800u,next,false);
  assert(m_epd_operation_pending&&m_epd_pending_operation.timestamp==next&&m_epd_pending_operation.arg==mode&&m_epd_pending_operation.background);
  scheduled_display(NULL,0);assert(events==1&&!m_epd_operation_pending);
 }
 for(unsigned fault=0;fault<4;fault++){
  reset();uint8_t b[9]={INKTAG_TEST_TIME_SET,2};inktag_write_u32_le(b+2,946684800u);b[8]=1;
  storage_fail=fault==0;display_queue_fail=fault==1;scheduler_fail=fault==2;low_power=fault==3;
  request(INKTAG_CMD_RUN_TEST,b,9);assert(status==(fault==0?INKTAG_STATUS_PERSISTENCE_ERROR:fault==1?INKTAG_STATUS_BUSY:fault==2?INKTAG_STATUS_INTERNAL_ERROR:INKTAG_STATUS_LOW_POWER));
  assert(!m_epd_operation_pending);if(scheduled_display)scheduled_display(NULL,0);unchanged();
 }
 const int32_t shifts[]={-366*86400,-60,60,366*86400};
 for(unsigned i=0;i<4;i++){
  reset();uint8_t b[6]={INKTAG_TEST_TIME_SHIFT,2};inktag_write_u32_le(b+2,(uint32_t)shifts[i]);request(INKTAG_CMD_RUN_TEST,b,6);
  assert(status==INKTAG_STATUS_OK&&m_timestamp==(uint32_t)(1800000000ll+shifts[i])&&!events);
  scheduled_display(NULL,0);assert(events==1);
 }
 const int32_t bad_shift[]={0,366*86400+1,-366*86400-1,INT32_MIN,INT32_MAX};
 for(unsigned i=0;i<sizeof(bad_shift)/sizeof(*bad_shift);i++){
  reset();uint8_t b[6]={INKTAG_TEST_TIME_SHIFT,2};inktag_write_u32_le(b+2,(uint32_t)bad_shift[i]);request(INKTAG_CMD_RUN_TEST,b,6);
  assert(status==INKTAG_STATUS_INVALID_REQUEST&&!writes);unchanged();
 }
 for(unsigned mode=0;mode<4;mode++)if(mode!=1&&mode!=2){
  reset();uint8_t b[9]={INKTAG_TEST_TIME_SET,mode};inktag_write_u32_le(b+2,946684800u);b[8]=1;request(INKTAG_CMD_RUN_TEST,b,9);
  assert(status==INKTAG_STATUS_INVALID_REQUEST&&!writes);unchanged();
 }
 reset();uint8_t bad_week[9]={INKTAG_TEST_TIME_SET,2};inktag_write_u32_le(bad_week+2,946684800u);bad_week[8]=7;
 request(INKTAG_CMD_RUN_TEST,bad_week,9);assert(status==INKTAG_STATUS_INVALID_REQUEST&&!writes);unchanged();
 reset();storage_fail=true;time_request(1800001000u,-60,1,7);assert(status==INKTAG_STATUS_PERSISTENCE_ERROR&&writes==1);unchanged();
 reset();scheduler_fail=true;time_request(1800001000u,-60,1,7);assert(status==INKTAG_STATUS_INTERNAL_ERROR&&!writes&&!m_v2_rx_pending&&!m_v2_rx_length);unchanged();
 reset();low_power=true;time_request(1800001000u,-60,1,7);assert(status==INKTAG_STATUS_LOW_POWER&&!writes);unchanged();
 puts("PASS production LIST/RUN/status: exact5items, all256suboperations, RGB+locate COMPLETE, time tests and ADC0/1 and unsupported input/reference/GPIO and BUSY/scheduler gates");
 puts("PASS production SET_TIME + native encoder + clock setter:100years, full uint32 range/timezones, invalid/storage/scheduler/low-power failures leave wall/timers/NOR untouched");
}
'''

with tempfile.TemporaryDirectory() as directory:
    out = Path(directory)
    time_type = re.search(r'typedef struct \{[^{}]+\} epd_time_state_record_t;', NATIVE).group()
    # Keep the actual production dispatcher preamble/gates; select only the
    # audited switch cases so unrelated peripherals need no replacement stubs.
    dispatch = function(PROTOCOL, 'epd_v2_process_packet')
    dispatch = dispatch[:dispatch.index('    switch (request.opcode) {')+len('    switch (request.opcode) {')]
    dispatch += PROTOCOL[PROTOCOL.index('        case INKTAG_CMD_GET_TIME:'):PROTOCOL.index('        case INKTAG_CMD_GET_SCREEN_PROFILE:')]
    start = PROTOCOL.index('        case INKTAG_CMD_LIST_TESTS: {')
    end = PROTOCOL.index('\n#endif', PROTOCOL.index('        case INKTAG_CMD_GET_TEST_STATUS:', start))
    dispatch += PROTOCOL[start:end]+'\n    default: assert(0);\n    }\n}\n'
    definitions = '\n'.join(re.search(r'^#define '+name+r'[^\n]+', source, re.M).group() for source, name in [(NATIVE,'TIME_RECORD_MAGIC')])+'\n'+time_type
    definitions += function(SCREEN, 'epd_record_checksum')
    definitions += function(NATIVE, 'epd_time_state_read')+function(NATIVE, 'epd_time_state_write')
    definitions += function(MAIN, 'set_timestamp')
    definitions += '\n'.join(function(PROTOCOL, n) for n in ('epd_v2_command_policy','epd_v2_command_requires_idle_display',
        'epd_v2_reply_status', 'epd_v2_command_admission_status', 'epd_v2_command_supported_on_target', 'epd_v2_capabilities', 'epd_v2_send_time', 'epd_v2_led_test_cancel', 'epd_v2_run_led_test'))
    definitions += '\n'.join(function(SERVICE, n) for n in ('epd_operation_admit','epd_schedule_dispatch','epd_schedule_operation','epd_schedule_background_operation','epd_native_timer_update_try'))
    definitions += '\n'.join(function(PROTOCOL,n) for n in ('epd_v2_own_operation','epd_v2_time_offset','epd_v2_time_command','epd_v2_adc_test','epd_v2_operation_complete'))
    definitions += function((ROOT.parents[3]/'firmware/common/time/inktag_monotonic_clock.c').read_text(encoding='utf-8'),'inktag_time_boundary_crossed')+function(SERVICE,'ble_epd_on_timer')
    definitions += 'static void epd_operation_dispatch(void*p,uint16_t n){(void)p;(void)n;m_epd_dispatch_scheduled=false;if(!m_epd_operation_pending)return;m_epd_operation_pending=false;epd_v2_operation_complete(&m_epd_pending_operation,INKTAG_STATUS_OK); }\n'
    definitions += 'static uint8_t epd_v2_current_display_mode(void){return 1;}\n'+dispatch
    definitions += '\n'.join(function(PROTOCOL, n) for n in ('epd_v2_process_queued', 'epd_v2_defer_display_read', 'epd_v2_queue_packet'))
    definitions += '\n'.join(function(SERVICE,n) for n in ('epd_service_on_write','on_write','ble_epd_on_ble_evt','ble_epd_evt_handler'))
    years = ','.join(str(int(datetime(year, 1, 1, tzinfo=timezone.utc).timestamp()))+'u' for year in range(2000, 2100))
    (out/'driver.c').write_text(PREFIX+definitions+'\nstatic const uint32_t years[]={'+years+'};\n'+DRIVER, encoding='utf-8', newline='\n')
    flags = ['-O2'] if not os.environ.get('INKTAG_TEST_SANITIZERS') else ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=all','-no-pie']
    includes = ['compat/nordic/src','../../../boards','compat/nordic/config','compat/common/protocol','compat/common/epd','compat/common/status','compat/common/storage']
    subprocess.run(['cc','-std=gnu11','-Wall','-Werror',*flags,'-DNRF52811_XXAA',
        *['-I'+str(ROOT/p) for p in includes],str(out/'driver.c'),
        str(ROOT/'compat/common/protocol/inktag_protocol_core.c'),str(ROOT/'compat/common/status/inktag_status.c'),
        '-o',str(out/'test')], check=True)
    subprocess.run([str(out/'test')], check=True)
