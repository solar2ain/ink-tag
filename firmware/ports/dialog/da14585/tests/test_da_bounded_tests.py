"""Actual public test handlers, native scheduler and completion; inert storage/IO."""
from da_host_helpers import ROOT, SOURCE, compile_run, function


def test_real_bounded_tests_and_failure_atomicity(tmp_path):
    protocol = SOURCE.read_text(encoding='utf-8')
    application = (SOURCE.parent/'user_custs1_impl.c').read_text(encoding='utf-8')
    tests = (SOURCE.parent/'inktag_da_tests.inc').read_text(encoding='utf-8')
    transport = (SOURCE.parent/'inktag_da_notifications.inc').read_text(encoding='utf-8')
    dispatch = function(protocol, 'process_packet(')
    start = dispatch.index('    switch (opcode) {')
    begin = dispatch.index('        case INKTAG_CMD_SET_TIME:', start)
    end = dispatch.index('        case INKTAG_CMD_GET_SCREEN_PROFILE:', begin)
    selected = dispatch[:start] + '    switch (opcode) {\n' + dispatch[begin:end] + 'default: assert(false);\n}\n}\n'
    harness = r'''
#include <assert.h>
#include <string.h>
#include "inktag_epd_config.h"
#include "inktag_display_settings.h"
#include "inktag_protocol_core.h"
#include "inktag_power.h"
#define put_u16_le inktag_write_u16_le
#define put_u32_le inktag_write_u32_le
#define read_u16_le inktag_read_u16_le
typedef enum {EPD_WORK_NONE,EPD_WORK_CLEAR,EPD_WORK_REFRESH,EPD_WORK_GUI,
              EPD_WORK_IMAGE_SHOW,EPD_WORK_GHOST_CLEAN} epd_work_type_t;
#define MODE_DEVICE_INFO 3u
#define MODE_CLOCK 1u
#define MODE_CALENDAR 2u
static epd_config_t s_config;
static inktag_display_settings_t s_display_settings;
static epd_display_state_t s_display_state;
static bool s_display_state_loaded=true,s_time_valid=true;
static bool s_work_pending,s_work_active,s_refresh_waiting,s_work_background;
static bool s_refresh_seen_busy,s_active_work_background,s_picture_session_active;
static bool s_cancel_requested,s_slideshow_active;
static uint8_t s_work_arg,s_active_work_arg,s_v2_work_opcode,s_v2_work_transaction;
static uint8_t s_slideshow_current_id,s_pending_picture_store_id,s_ghost_pass,s_ghost_result;
static uint32_t s_timestamp,s_work_timestamp,s_active_work_timestamp;
static uint32_t s_refresh_start_tick,s_refresh_assert_ms,s_refresh_busy_ms;
static uint32_t s_slideshow_next_at,s_slideshow_interval;
static epd_work_type_t s_work_type,s_active_work_type;
static int16_t s_timezone_minutes;
static struct {bool active;} s_v2_transfer;
static uint8_t s_system_action_pending;
static bool telemetry=true,screen_ready=true,flash_power=true,display_power=true;
static bool preference_ok=true,time_ok=true,rollback_ok=true,spi_ok=true;
static bool late_display_refusal;
static bool time_valid=true,calibrated=true;
static uint32_t saved_time;static int16_t saved_zone;
static unsigned preference_calls,time_calls,time_updates,config_updates,power_calls,adc_calls;
static unsigned display_power_calls;
static unsigned replies,progresses,completions,records;
static uint8_t reply[16],reply_status,reply_op,reply_tx,complete_status,complete_op,complete_tx;
static uint16_t reply_length,voltage=3000;
struct panel;struct driver {void(*sleep)(struct panel*);};struct panel {struct driver *drv;};
static void sleep_panel(struct panel *p){(void)p;}
static struct driver driver={sleep_panel};static struct panel panel={&driver},*s_epd=&panel;
static bool inktag_da_voltage_telemetry_available(void){return telemetry;}
static uint16_t inktag_da_read_battery_mv(void){++adc_calls;return voltage;}
static bool epd_ensure_screen(void){return screen_ready;}
static uint8_t epd_unavailable_status(void){return INKTAG_STATUS_INCOMPATIBLE;}
static bool inktag_da_health_operation_allowed(inktag_power_operation_t op,bool fresh){
 assert(fresh);++power_calls;
 if(op==INKTAG_POWER_OPERATION_DISPLAY){
  ++display_power_calls;
  return display_power && !(late_display_refusal && display_power_calls>1);
 }
 return flash_power;
}
static void defer_automatic_display_work(void){assert(false);}
static void send_v2_response(uint8_t op,uint8_t tx,inktag_status_v2_t status,
                            const uint8_t *p,uint16_t n){
 ++replies;reply_op=op;reply_tx=tx;reply_status=status;reply_length=n;
 assert(n<=sizeof reply);if(n)memcpy(reply,p,n);else assert(!p);
}
static void send_v2_event(uint8_t ev,inktag_status_v2_t status,uint8_t op,uint8_t tx,
                         const uint8_t *p,uint16_t n){
 assert(op==INKTAG_CMD_RUN_TEST && tx==7 && p && n);
 if(ev==INKTAG_EVENT_PROGRESS)++progresses;
 else {assert(ev==INKTAG_EVENT_COMPLETE);++completions;
       complete_status=status;complete_op=op;complete_tx=tx;}
}
bool epd_dialog_display_settings_write(const epd_display_state_t *state,
                                      const inktag_display_settings_t *settings){
 (void)state;(void)settings;++preference_calls;
 return preference_calls==1?preference_ok:rollback_ok;
}
bool epd_time_state_write(uint32_t timestamp,int16_t zone){
 ++time_calls;
 if(time_calls==1 && time_ok){saved_time=timestamp;saved_zone=zone;return true;}
 if(time_calls>1 && rollback_ok){saved_time=timestamp;saved_zone=zone;return true;}
 return false;
}
void epd_config_write(epd_config_t *config){assert(config==&s_config);++config_updates;}
static void set_timestamp(uint32_t t){s_timestamp=t;s_time_valid=true;calibrated=true;++time_updates;}
static bool epd_abort_requested(void){return s_cancel_requested;}
static bool EPD_AbortRequested(void){return false;}
static bool EPD_DialogSpiReady(void){return spi_ok;}
static int EPD_DialogSpiLastError(void){return 0;}
static bool ghost_clean_continue(inktag_status_v2_t status){(void)status;assert(false);return false;}
static void send_public_failure_log(void){}
static void EPD_Reset(bool high,unsigned delay){assert(high&&delay==10);}
static void delay(unsigned ms){assert(ms==100);}
static void epd_gpio_close_if_needed(void){}
static void EPD_SetAbortCallback(void *p){assert(!p);}
static void record_display_state(uint8_t mode,uint8_t arg){(void)arg;++records;s_display_state.mode=mode;}
'''
    harness += '\n'.join(function(protocol, n) for n in ('v2_display_busy(', 'v2_current_display_mode(',
        'v2_encode_time(', 'v2_send_time(', 'v2_free_only_command_unsupported('))
    harness += function(transport, 'send_v2_status(')
    harness += function(protocol, 'v2_unsupported(')
    harness += function(application, 'schedule_epd_work(')
    harness += function(protocol, 'v2_schedule_work(')
    harness += tests
    harness += function(application, 'finish_epd_work(')
    harness += selected + r'''
static void reset(void){
 s_timestamp=1791696000u;s_timezone_minutes=480;s_config.week_start=1;
 s_display_settings=(inktag_display_settings_t){.week_start=1};
 s_display_state=(epd_display_state_t){.mode=EPD_DISPLAY_STATE_CALENDAR};
 s_time_valid=time_valid=calibrated=true;s_work_pending=s_work_active=s_refresh_waiting=false;
 s_v2_transfer.active=false;s_system_action_pending=0;s_v2_work_opcode=s_v2_work_transaction=0;
 s_work_timestamp=123;s_work_type=EPD_WORK_NONE;s_cancel_requested=false;
 screen_ready=flash_power=display_power=preference_ok=time_ok=rollback_ok=spi_ok=true;
 late_display_refusal=false;display_power_calls=0;
 replies=progresses=completions=records=preference_calls=time_calls=time_updates=config_updates=power_calls=adc_calls=0;
 saved_time=s_timestamp;saved_zone=s_timezone_minutes;
}
static void request(uint8_t opcode,const uint8_t *p,unsigned n,uint8_t flags){
 uint8_t packet[20]={opcode,7,flags};assert(n<=17);if(n)memcpy(packet+3,p,n);process_packet(packet,3+n);
 assert(replies==1 && reply_op==opcode && reply_tx==7);
}
static void unchanged(void){
 assert(s_timestamp==1791696000u && s_timezone_minutes==480 && s_config.week_start==1);
 assert(s_display_settings.week_start==1 && calibrated && !time_updates && !config_updates);
 assert(!s_work_pending && !s_v2_work_transaction && s_work_timestamp==123 && !progresses && !completions);
}
static void make_set(uint8_t *p,uint32_t utc,int16_t zone,uint8_t mode,uint8_t week){
 p[0]=INKTAG_TEST_TIME_SET;p[1]=mode;put_u32_le(p+2,utc);put_u16_le(p+6,zone);p[8]=week;
}
int main(void){
 uint8_t p[12];
 for(unsigned available=0;available<2;available++){
  reset();telemetry=available;request(INKTAG_CMD_LIST_TESTS,NULL,0,0);
  assert(reply_status==INKTAG_STATUS_OK && reply_length==4+available && reply[0]==1 && reply[1]==2+available);
  assert(reply[2]==INKTAG_TEST_TIME_SHIFT && reply[3]==INKTAG_TEST_TIME_SET);
  if(available)assert(reply[4]==INKTAG_TEST_ADC_READ);
 }
 for(unsigned mode=1;mode<=2;mode++)for(unsigned fault=0;fault<7;fault++){
  reset();make_set(p,1791700000u,-60,mode,0);
  if(fault==1)preference_ok=false;
  if(fault==2)time_ok=false;
  if(fault==3)late_display_refusal=true;
  if(fault==4){late_display_refusal=true;rollback_ok=false;}
  if(fault==5)flash_power=false;
  if(fault==6)display_power=false;
  request(INKTAG_CMD_RUN_TEST,p,9,0);
  if(!fault){
   assert(reply_status==INKTAG_STATUS_OK && reply_length==7 && s_work_pending && progresses==1 && !completions);
   assert(s_timestamp==1791696400u && s_timezone_minutes==-60 && s_config.week_start==0);
   assert(s_work_timestamp==s_timestamp && s_work_type==EPD_WORK_GUI);
   assert(s_work_arg==(mode==1?MODE_CALENDAR:MODE_CLOCK));
   s_work_pending=false;s_work_active=true;s_active_work_type=s_work_type;s_refresh_seen_busy=true;
   finish_epd_work(EPD_WORK_GUI,s_work_arg,true,true);
   assert(completions==1 && complete_status==INKTAG_STATUS_OK && complete_op==INKTAG_CMD_RUN_TEST && complete_tx==7);
  }else{
   assert(reply_status==(fault==3?INKTAG_STATUS_BUSY:fault>=5?INKTAG_STATUS_LOW_POWER:INKTAG_STATUS_PERSISTENCE_ERROR));
   unchanged();
   if(fault>=5)assert(!preference_calls && !time_calls);
   if(fault==3)assert(saved_time==s_timestamp && saved_zone==s_timezone_minutes);
  }
 }
 const uint32_t bad_times[]={0,946684800u,1672531200u,1704067199u,4102444801u,UINT32_MAX};
 for(unsigned i=0;i<sizeof(bad_times)/sizeof(bad_times[0]);i++){
  reset();make_set(p,bad_times[i],0,1,0);request(INKTAG_CMD_RUN_TEST,p,9,0);
  assert(reply_status==INKTAG_STATUS_INVALID_REQUEST && !preference_calls && !time_calls);unchanged();
 }
 for(unsigned edge=0;edge<2;edge++){
  reset();make_set(p,edge?4102444800u:1704067200u,0,2,1);request(INKTAG_CMD_RUN_TEST,p,9,0);
  assert(reply_status==INKTAG_STATUS_OK && s_work_pending);
 }
 /* A later physical refresh failure reports COMPLETE failure after the
  * successful time ACK; it must not masquerade as failed time admission. */
 for(unsigned cancelled=0;cancelled<2;cancelled++){
  reset();make_set(p,1791700000u,0,2,1);request(INKTAG_CMD_RUN_TEST,p,9,0);
  assert(reply_status==INKTAG_STATUS_OK && time_updates==1);
  s_work_pending=false;s_work_active=true;s_refresh_seen_busy=true;
  spi_ok=cancelled;s_cancel_requested=cancelled;
  finish_epd_work(EPD_WORK_GUI,s_work_arg,true,true);
  assert(completions==1 && complete_status==(cancelled?INKTAG_STATUS_CANCELLED:INKTAG_STATUS_IO_ERROR));
  assert(s_timestamp==1791700000u && time_updates==1 && !s_v2_work_transaction);
 }
 for(unsigned i=0;i<3;i++){
  const int32_t shifts[]={60,-86400,31622400};reset();p[0]=INKTAG_TEST_TIME_SHIFT;p[1]=2;put_u32_le(p+2,shifts[i]);
  request(INKTAG_CMD_RUN_TEST,p,6,0);assert(reply_status==INKTAG_STATUS_OK && s_timestamp==1791696000u+shifts[i]);
 }
 const int32_t bad_shifts[]={0,31622401,-31622401,INT32_MIN,INT32_MAX};
 for(unsigned i=0;i<5;i++){
  reset();p[0]=INKTAG_TEST_TIME_SHIFT;p[1]=1;put_u32_le(p+2,bad_shifts[i]);request(INKTAG_CMD_RUN_TEST,p,6,0);
  assert(reply_status==INKTAG_STATUS_INVALID_REQUEST && !preference_calls);unchanged();
 }
 reset();s_time_valid=false;p[0]=INKTAG_TEST_TIME_SHIFT;p[1]=1;put_u32_le(p+2,60);request(INKTAG_CMD_RUN_TEST,p,6,0);
 assert(reply_status==INKTAG_STATUS_INVALID_STATE && !preference_calls);unchanged();
 for(unsigned flags=1;flags<4;flags++){
  reset();make_set(p,1791700000u,0,1,0);request(INKTAG_CMD_RUN_TEST,p,9,flags);
  assert(reply_status==INKTAG_STATUS_INVALID_REQUEST && !preference_calls && !power_calls);unchanged();
 }
 for(unsigned busy=0;busy<5;busy++){
  reset();make_set(p,1791700000u,0,1,0);
  if(busy==0)s_work_pending=true;
  if(busy==1)s_work_active=true;
  if(busy==2)s_refresh_waiting=true;
  if(busy==3)s_v2_transfer.active=true;
  if(busy==4)s_system_action_pending=4;
  request(INKTAG_CMD_RUN_TEST,p,9,0);assert(reply_status==INKTAG_STATUS_BUSY && !preference_calls && !time_updates);
 }
 reset();screen_ready=false;make_set(p,1791700000u,0,1,0);request(INKTAG_CMD_RUN_TEST,p,9,0);
 assert(reply_status==INKTAG_STATUS_INCOMPATIBLE && !preference_calls);unchanged();
 for(unsigned n=0;n<12;n++)if(n!=9){
  reset();make_set(p,1791700000u,0,1,0);request(INKTAG_CMD_RUN_TEST,p,n,0);
  assert(reply_status==INKTAG_STATUS_INVALID_REQUEST && !preference_calls);unchanged();
 }
 for(unsigned selector=0;selector<256;selector++)if(selector!=INKTAG_TEST_TIME_SET && selector!=INKTAG_TEST_TIME_SHIFT && selector!=INKTAG_TEST_ADC_READ){
  reset();p[0]=selector;request(INKTAG_CMD_RUN_TEST,p,1,0);assert(reply_status==INKTAG_STATUS_UNSUPPORTED);unchanged();
 }
 for(unsigned available=0;available<2;available++)for(unsigned channel=0;channel<4;channel++){
  reset();telemetry=available;p[0]=INKTAG_TEST_ADC_READ;p[1]=channel;request(INKTAG_CMD_RUN_TEST,p,2,0);
  assert(!preference_calls && !time_updates);
  if(available&&channel==1){assert(reply_status==INKTAG_STATUS_OK && reply_length==6 && reply[2]==1);
   assert(inktag_read_u16_le(reply+3)==3000 && adc_calls==1 && completions==1);}
  else assert(reply_status==(channel==3?INKTAG_STATUS_INVALID_REQUEST:INKTAG_STATUS_UNSUPPORTED) && !adc_calls && !completions);
 }
 reset();telemetry=true;voltage=0;p[0]=INKTAG_TEST_ADC_READ;p[1]=1;request(INKTAG_CMD_RUN_TEST,p,2,0);
 assert(reply_status==INKTAG_STATUS_OK && reply[2]==0 && inktag_read_u16_le(reply+3)==0 && completions==1);
 reset();request(INKTAG_CMD_GET_TEST_STATUS,NULL,0,0);assert(reply_length==3 && reply[0]==1 && reply[1]==0 && reply[2]==INKTAG_DISPLAY_MODE_CALENDAR);
 reset();s_work_pending=true;request(INKTAG_CMD_GET_TEST_STATUS,NULL,0,0);assert(reply[1]==1);
 return 0;
}
'''
    path = tmp_path/'actual-da-bounded-tests.c'
    path.write_text(harness, encoding='utf-8')
    compile_run(tmp_path, ['-DINKTAG_DIALOG', '-DINKTAG_PROTOCOL_FREE_ONLY_TARGET=1',
        '-Icompat/common/epd', '-Icompat/common/input', '-Icompat/common/power'],
        [path, 'compat/common/protocol/inktag_protocol_core.c'], sanitize=True)
