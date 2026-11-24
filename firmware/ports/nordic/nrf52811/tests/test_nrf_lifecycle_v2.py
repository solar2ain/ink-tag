"""Real lifecycle handler + native v1 NOR settings; runtime cache restoration.

The independent fixed-image C matrix covers NOR pixels/stride/power cuts. Here
the image boundary is strict and injected, so each storage domain has one owner.
"""
from pathlib import Path
import re,tempfile
from test_nrf_native_v1 import ROOT,compile_run
adapter=(ROOT/'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text()
def function(name):
    found=re.search(r'^static [^\n]+\b'+name+r'\([^;{]*\)\s*\{',adapter,re.M);assert found,name
    depth=1;i=found.end()
    while depth:
        if adapter[i]=='{':depth+=1
        if adapter[i]=='}':depth-=1
        i+=1
    return adapter[found.start():i]
prefix='#define main reference_native_main\n'+(ROOT/'tests/fixtures/nrf_native_v1.inc').read_text()+'\n#undef main\n'
prefix+=r'''
#include "inktag_lifecycle.h"
#include "inktag_status.h"
#include "inktag_nrf_image_store.h"
#include "inktag_power.h"
#define INKTAG_BOARD_LIFECYCLE_V2_SUPPORTED 1
#define INKTAG_NRF_INDICATOR_ENABLED 1
#define INKTAG_NRF_IMAGE_STORE_ENABLED 1
#define INKTAG_NRF_FIXED_IMAGE_SLOTS 1
#define INKTAG_NRF_BOARD_NRF52811_AESL 1
#define INKTAG_BOARD_LAZY_DISPLAY_BUS 1
#define EPD_V2_STORE_BUSY() store_busy
typedef struct {epd_config_t config;epd_screen_config_t screen_config;void *epd;} ble_epd_t;
static epd_display_state_t m_display_state;
static inktag_display_settings_t m_v2_display_settings;
static epd_device_settings_t m_device_settings;
static inktag_status_runtime_t m_v2_indicator_runtime;
static struct {const char *name;uint32_t target_day;} m_v2_countdown_state;
static struct {bool active;} m_v2_transfer;
static bool m_epd_operation_active,m_epd_operation_pending,store_busy,image_busy;
static bool m_native_timer_update_pending,slideshow,led_enabled,images_ready=true,schedule_failure;
static unsigned resets,time_writes,replies,image_clears,image_inits,bus_releases,temperature_clears;
static uint8_t reply_status,reply[8];static uint16_t reply_length;
static int16_t m_v2_timezone_minutes;
static inktag_image_store_result_t image_result=INKTAG_IMAGE_STORE_OK;
bool inktag_image_store_busy(void){return image_busy;}
bool inktag_image_store_init(void){image_inits++;return images_ready;}
inktag_image_store_result_t inktag_image_store_clear_user_images(uint16_t *removed){image_clears++;*removed=3;return image_result;}
static void EPD_GPIO_Uninit(void){bus_releases++;}
static void EPD_GPIO_Load(epd_config_t *config){assert(config->bs_pin==INKTAG_BOARD_EPD_BS_PIN);}
static void epd_v2_led_test_cancel(void){}
static void EPD_LED_SetEnabled(bool enabled){led_enabled=enabled;}
void inktag_nrf_health_panel_temperature_sample(const void *panel){assert(!panel);temperature_clears++;}
static void *epd_find(epd_screen_config_t *screen){assert(screen->schema_version==1);return screen;}
static uint32_t app_monotonic_now_ms(void){return 1234;}
static void epd_slideshow_stop(ble_epd_t *p,bool notify){(void)p;assert(!notify);slideshow=false;}
static inktag_status_v2_t epd_v2_store_status(inktag_image_store_result_t result){return result==INKTAG_IMAGE_STORE_LOW_POWER?INKTAG_STATUS_LOW_POWER:INKTAG_STATUS_IO_ERROR;}
static void epd_v2_send_response(ble_epd_t*p,uint8_t op,uint8_t tx,uint8_t status,const void*data,uint16_t n){(void)p;assert(op==INKTAG_CMD_SYSTEM_ACTION&&tx==9);replies++;reply_status=status;reply_length=n;if(n)memcpy(reply,data,n);}
#define epd_v2_reply_status(p,o,t,s) epd_v2_send_response(p,o,t,s,NULL,0u)

static void epd_v2_send_event(ble_epd_t*p,uint8_t e,uint8_t s,uint8_t op,uint8_t tx,const void*d,unsigned n){(void)p;(void)e;(void)s;(void)op;(void)tx;(void)d;(void)n;}
static uint32_t timestamp(void){return 1800000000;}
static bool test_time_write(uint32_t t,int16_t z){(void)t;(void)z;time_writes++;return true;}
#define epd_time_state_write test_time_write
static bool epd_v2_schedule_system_action(uint8_t action){assert(action==INKTAG_SYSTEM_ACTION_RESET);resets++;return !schedule_failure;}
static void nrf_delay_ms(unsigned ms){assert(ms==250);}
static unsigned ble_dfu_buttonless_bootloader_start_prepare(void){return 0;}
bool inktag_nrf_health_operation_allowed(inktag_power_operation_t operation,bool force){(void)operation;assert(force);return true;}
'''
prefix+='\n'.join(function(n) for n in ('epd_v2_display_settings_sync_runtime','epd_v2_apply_saved_screen','epd_v2_cleanup','epd_v2_system_action'))
main=r'''
static ble_epd_t device;
static void seed(void){
 read_error=false;cut=-1;memset(bytes,255,sizeof(bytes));cold();factory_seed();
 nrf11_factory_record_t factory;uint16_t n;
 assert(inktag_nrf_config_store_read(INKTAG_NRF_CONFIG_BOARD,&factory,sizeof(factory),&n));
 factory.config.week_start=0;factory.checksum=epd_record_checksum((const uint8_t*)&factory,36);
 assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_BOARD,&factory,sizeof(factory)));
 assert(epd_screen_config_read(&device.screen_config));device.screen_config.transform=1;assert(epd_screen_config_write(&device.screen_config));
 epd_device_settings_default(&m_device_settings);strcpy(m_device_settings.label,"Desk");m_device_settings.label_length=4;
 assert(epd_device_settings_write(&m_device_settings));
 m_display_state=(epd_display_state_t){EPD_DISPLAY_STATE_SLIDESHOW,2,1,30};
 m_v2_display_settings=(inktag_display_settings_t){.week_start=1,.flags=1,.target_day=20000,.name_length=3,.name="day"};
 assert(epd_display_settings_write(&m_display_state,&m_v2_display_settings));assert(epd_display_state_write(&m_display_state));
 uint8_t marker[]={42,43,44};assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_HEALTH,marker,sizeof(marker)));
 epd_v2_display_settings_sync_runtime(&device);
 inktag_status_config_t off;inktag_status_config_default(&off);off.policy=INKTAG_STATUS_POLICY_OFF;
 inktag_status_init(&m_v2_indicator_runtime,&off,7,NULL,NULL,0);led_enabled=false;
 slideshow=m_native_timer_update_pending=true;images_ready=true;schedule_failure=false;
 m_epd_operation_active=m_epd_operation_pending=m_v2_transfer.active=store_busy=image_busy=false;
 image_result=INKTAG_IMAGE_STORE_OK;image_clears=image_inits=bus_releases=temperature_clears=resets=time_writes=0;
}
static void invoke(unsigned action,unsigned flags){uint8_t payload=action;replies=0;epd_v2_system_action(&device,INKTAG_CMD_SYSTEM_ACTION,9,flags,&payload,1);assert(replies==1);}
int main(void){
 assert(reference_native_main()==0);
 for(unsigned action=5;action<=6;action++){
  seed();unsigned before=writes+erases;invoke(action,0);assert(reply_status==INKTAG_STATUS_DENIED&&writes+erases==before);
  for(unsigned busy=1;busy<32;busy++){
   m_epd_operation_active=busy&1;m_epd_operation_pending=busy&2;m_v2_transfer.active=busy&4;store_busy=busy&8;image_busy=busy&16;
   invoke(action,INKTAG_REQUEST_FLAG_CONFIRM);assert(reply_status==INKTAG_STATUS_BUSY&&!resets&&writes+erases==before);
  }
 }
 seed();invoke(3,INKTAG_REQUEST_FLAG_CONFIRM);assert(reply_status==INKTAG_STATUS_UNSUPPORTED&&!resets);
 seed();invoke(5,INKTAG_REQUEST_FLAG_CONFIRM);
 assert(reply_status==INKTAG_STATUS_OK&&!reply_length&&resets==1&&!time_writes&&!image_clears);
 assert(!slideshow&&!m_native_timer_update_pending&&!m_device_settings.label_length&&!m_device_settings.name_length);
 assert(device.config.week_start==0&&device.screen_config.transform==3&&device.epd);
 assert(image_inits==1&&bus_releases==1&&temperature_clears==1&&led_enabled);
 assert(m_v2_indicator_runtime.config.policy!=INKTAG_STATUS_POLICY_OFF);
 assert(!m_v2_display_settings.target_day&&!m_v2_display_settings.flags&&!m_v2_display_settings.name_length);
 cold();epd_display_state_t replay;assert(epd_display_state_read(&replay)&&replay.mode==EPD_DISPLAY_STATE_SLIDESHOW&&replay.interval_seconds==30&&!replay.reserved);
 seed();nrf11_user_record_t old=user_record();invoke(6,INKTAG_REQUEST_FLAG_CONFIRM);
 assert(reply_status==INKTAG_STATUS_OK&&reply_length==2&&inktag_read_u16_le(reply)==3&&!resets&&!time_writes);
 nrf11_user_record_t after=user_record();assert(!memcmp(&old,&after,sizeof(old)));
 assert(!slideshow&&!m_native_timer_update_pending&&!strcmp(m_device_settings.label,"Desk")&&device.screen_config.transform==1);
 assert(m_display_state.mode==EPD_DISPLAY_STATE_PICTURE&&m_display_state.argument==255&&!m_display_state.interval_seconds);
 uint8_t protection[3];uint16_t n;assert(inktag_nrf_config_store_read(INKTAG_NRF_CONFIG_HEALTH,protection,sizeof(protection),&n)&&n==3&&protection[0]==42);
 seed();unsigned before=writes+erases;read_error=true;invoke(5,INKTAG_REQUEST_FLAG_CONFIRM);assert(reply_status==INKTAG_STATUS_PERSISTENCE_ERROR&&!resets&&writes+erases==before);read_error=false;
 seed();before=writes+erases;image_result=INKTAG_IMAGE_STORE_LOW_POWER;invoke(6,INKTAG_REQUEST_FLAG_CONFIRM);assert(reply_status==INKTAG_STATUS_LOW_POWER&&!resets&&writes+erases==before);
 seed();images_ready=false;invoke(5,INKTAG_REQUEST_FLAG_CONFIRM);assert(reply_status==INKTAG_STATUS_PERSISTENCE_ERROR&&!resets&&!device.epd);
 seed();schedule_failure=true;invoke(5,INKTAG_REQUEST_FLAG_CONFIRM);assert(reply_status==INKTAG_STATUS_IO_ERROR&&resets==1&&!time_writes);
 puts("PASS actual native lifecycle: confirmation/BUSY, protected records, reset cache/Factory screen/week/indicator, preserved image preferences, no time write and safe I/O failures");
}
'''
with tempfile.TemporaryDirectory() as d:
    compile_run(Path(d),prefix+main,extra_sources=['compat/common/status/inktag_status.c','compat/common/lifecycle/inktag_lifecycle.c'])
