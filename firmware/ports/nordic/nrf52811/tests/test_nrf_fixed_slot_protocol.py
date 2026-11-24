"""Run shipping INFO/LIST/profile cases against strict NOR mocks."""
from pathlib import Path
import json, re, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text(encoding='utf-8')
service=(ROOT/'compat/common/epd/inktag_epd_service.c').read_text(encoding='utf-8')
matches=re.search(r'^static bool epd_screen_config_matches\([^;{]*\)\s*\{[\s\S]*?\n}',service,re.M).group()
def function(name,text=source):
    m=re.search(r'^(?:static )?[^\n]+\b'+name+r'\([^;{]*\)\s*\{[\s\S]*?\n}',text,re.M)
    assert m,name
    return m.group()
fixture=(ROOT/'tests/test_nrf_fixed_image_store.c').read_text(encoding='utf-8').split('int main(void)')[0]
screen_source=(ROOT/'compat/common/epd/inktag_epd_screen.c').read_text(encoding='utf-8')
fixture=fixture.replace('#include "test_nrf_physical_image_store.c"',
 '#define epd_screen_config_encode reference_screen_encode\n#define epd_screen_config_decode reference_screen_decode\n#define epd_screen_config_decode_exact reference_screen_decode_exact\n#define epd_screen_wire_image_compatible reference_screen_compatible\n#include "test_nrf_physical_image_store.c"\n#undef epd_screen_config_encode\n#undef epd_screen_config_decode\n#undef epd_screen_config_decode_exact\n#undef epd_screen_wire_image_compatible')
fixture += '\n#define EPD_TRACE(x) ((void)(x))\n' + '\n'.join(function(n,screen_source) for n in ('read_u16_le','write_u16_le','screen_config_decode','epd_screen_config_encode','epd_screen_config_decode','epd_screen_config_decode_exact','epd_screen_wire_image_compatible'))+'\n'
fixture=fixture.replace('bool epd_screen_config_read(epd_screen_config_t *screen) { *screen = hema_screen(); return true; }',
 'static epd_screen_config_t persisted_screen;\nbool epd_screen_config_read(epd_screen_config_t *screen) { *screen = persisted_screen; return true; }')
start=source.index('        case INKTAG_CMD_SET_SCREEN_PROFILE: {')
end=source.index('        case INKTAG_CMD_SET_DISPLAY_MODE:',start)
prefix=r'''
#undef INKTAG_NRF_FIXED_IMAGE_SLOTS
#define INKTAG_NRF_FIXED_IMAGE_SLOTS PROTOCOL_FIXED
#include "inktag_protocol_core.h"
#include "inktag_epd_config.h"
#define EPD_V2_STORE_BUSY() inktag_image_store_busy()
#define INKTAG_STATUS_PERSISTENCE_ERROR 13
#define INKTAG_EVENT_DEVICE_CHANGED 1
#define INKTAG_BOARD_LAZY_DISPLAY_BUS 1
#define INKTAG_BOARD_IMAGE_STORE_SIZE (118u*4096u)
#define INKTAG_NRF_DEVICE_NAME_WRITABLE 0
typedef struct {epd_screen_config_t screen_config;void *epd;epd_config_t config;uint16_t max_data_len;} ble_epd_t;
static uint32_t epd_v2_capabilities(void) {return 0x12345678;}
static epd_device_settings_t m_device_settings;
static bool factory_qualified=true;
bool epd_config_empty(epd_config_t *c){(void)c;return !factory_qualified;}
bool epd_device_settings_read(epd_device_settings_t *s){memset(s,0,sizeof(*s));return true;}
static bool epd_v2_apply_saved_screen(ble_epd_t *p){p->screen_config=persisted_screen;return true;}
static uint8_t response[100],response_status;
static uint16_t response_length;
static unsigned profile_writes,stops;
static bool persist_failure,m_epd_operation_active,m_epd_operation_pending;
static struct {bool active;} m_v2_transfer;
static bool m_slideshow_active;
static uint32_t m_slideshow_interval=30;
static void epd_v2_send_response(ble_epd_t *p,uint8_t op,uint8_t tx,uint8_t status,const uint8_t *data,uint16_t length) {
 (void)p;(void)op;(void)tx;assert(length<=sizeof(response));response_status=status;response_length=length;if(length)memcpy(response,data,length);
}
#define epd_v2_reply_status(p,o,t,s) epd_v2_send_response(p,o,t,s,NULL,0u)

static inktag_status_v2_t epd_v2_store_status(inktag_image_store_result_t r) {
 return r==INKTAG_IMAGE_STORE_OK?INKTAG_STATUS_OK:r==INKTAG_IMAGE_STORE_LOW_POWER?INKTAG_STATUS_LOW_POWER:r==INKTAG_IMAGE_STORE_BUSY?INKTAG_STATUS_BUSY:INKTAG_STATUS_IO_ERROR;
}
static bool epd_v2_image_record_visible(ble_epd_t *p,uint8_t id) {(void)p;return id!=20;}
static inktag_image_store_result_t epd_v2_visible_store_status(ble_epd_t *p,const epd_screen_config_t *s,uint32_t bytes,inktag_image_store_status_t *out) {
 (void)p;inktag_image_store_result_t r=inktag_image_store_status(s,bytes,out);
#if INKTAG_NRF_ACTIVATION_ENABLED
 if(r==INKTAG_IMAGE_STORE_OK&&out->saved_count==2)out->saved_count=out->compatible_count=out->playable_count=1;
#endif
 return r;
}
static bool inktag_nrf_screen_omitted(const uint8_t *w,uint16_t n) {(void)w;(void)n;return false;}
static void *epd_find(const epd_screen_config_t *s) {return s->driver_id==EPD_SCREEN_DRIVER_SSD1619?(void*)1:NULL;}
bool epd_screen_config_write(const epd_screen_config_t *s) {profile_writes++;if(persist_failure)return false;persisted_screen=*s;return true;}
static void epd_slideshow_stop(ble_epd_t *p,bool persist) {(void)p;(void)persist;stops++;m_slideshow_active=false;}
static void epd_record_display_state(ble_epd_t *p,uint8_t m,uint8_t a,uint32_t i) {(void)p;(void)m;(void)a;(void)i;}
static void EPD_GPIO_Uninit(void) {}
static void epd_v2_send_screen(ble_epd_t *p,uint8_t op,uint8_t tx) {
 uint8_t w[EPD_SCREEN_WIRE_SIZE];assert(epd_screen_config_encode(&p->screen_config,w)==EPD_SCREEN_STATUS_OK);epd_v2_send_response(p,op,tx,INKTAG_STATUS_OK,w,sizeof(w));
}
static void epd_v2_send_event(ble_epd_t *p,uint8_t ev,uint8_t st,uint8_t op,uint8_t tx,const uint8_t *d,uint16_t n) {(void)p;(void)ev;(void)st;(void)op;(void)tx;(void)d;(void)n;}
'''
wrapper='''static void set_profile(ble_epd_t *p_epd,const uint8_t *wire,uint16_t length) {
 struct {uint8_t opcode,transaction_id;const uint8_t *payload;uint16_t payload_length;} request={INKTAG_CMD_SET_SCREEN_PROFILE,9,wire,length};
 switch(request.opcode) {
'''+source[start:end]+''' default:assert(false);
 }
}
'''
main=r'''
int main(void) {
 persisted_screen=hema_screen();configured_frame=8000;memset(s_flash,255,sizeof(s_flash));memset(s_payload,0x34,sizeof(s_payload));reboot();
 unsigned before_invalid=operation;
 for(unsigned invalid=16;invalid<=128;invalid<<=1) {
  upload_black=invalid;assert(upload(&persisted_screen,"invalid-flags")==INKTAG_IMAGE_STORE_BAD_ARGUMENT);assert(operation==before_invalid);
 }
 upload_black=1;assert(upload(&persisted_screen,"visible")==INKTAG_IMAGE_STORE_OK);
 ble_epd_t p={.screen_config=persisted_screen,.epd=(void*)1,.max_data_len=247};
 for(unsigned mtu=20;mtu<=35;mtu++) {
  p.max_data_len=mtu;epd_v2_send_capabilities(&p,0,1);
  assert(response_status==INKTAG_STATUS_OK&&response_length==(mtu>=INKTAG_CAPABILITIES_EXTENDED_SIZE+INKTAG_PROTOCOL_V2_HEADER_SIZE?28:17));
  assert(response[0]==1&&inktag_read_u32_le(response+1)==0x12345678&&response[13]==0);
  if(response_length==28) {
   for(unsigned i=17;i<24;i++)assert(response[i]==0);
   assert(inktag_read_u32_le(response+24)==INKTAG_CAPABILITY_EXTENSION_IMAGE_READING_ORIENTATION);
  }
 }
 p.max_data_len=247;
 for(unsigned rotation=0;rotation<4;rotation++) {
  upload_black=1|(rotation<<2);assert(inktag_image_store_replace_slot(0));assert(upload(&persisted_screen,"visible")==INKTAG_IMAGE_STORE_OK);
  reboot();inktag_image_store_header_t h;assert(inktag_image_store_get(0,&h)==INKTAG_IMAGE_STORE_OK&&h.black_mask==upload_black);
  epd_v2_list_resource(&p,0,1,NULL,0);assert(response_status==INKTAG_STATUS_OK&&response_length==27&&response[26]==rotation);
 }
 upload_black=1;assert(inktag_image_store_replace_slot(20));assert(upload(&persisted_screen,"hidden")==INKTAG_IMAGE_STORE_OK);
 epd_v2_send_resource_info(&p,0,1);assert(response_status==INKTAG_STATUS_OK);
#if INKTAG_NRF_FIXED_IMAGE_SLOTS
 assert(response_length==34&&inktag_read_u16_le(response+30)==57&&inktag_read_u16_le(response+32)==59);
#else
 assert(response_length==30);
#endif
#if INKTAG_NRF_ACTIVATION_ENABLED
 assert(inktag_read_u16_le(response+17)==1);
#else
 assert(inktag_read_u16_le(response+17)==2);
#endif
 epd_v2_list_resource(&p,0,1,NULL,0);assert(response[0]==0&&response[1]==0);
 uint8_t cursor=0;epd_v2_list_resource(&p,0,1,&cursor,1);
#if !INKTAG_NRF_ACTIVATION_ENABLED
 assert(response[0]==0&&response[1]==20);cursor=20;epd_v2_list_resource(&p,0,1,&cursor,1);
#endif
 assert(response[0]==1&&response[1]==255);
#if INKTAG_NRF_FIXED_IMAGE_SLOTS
 assert(response_length==10);for(unsigned n=0;n<8;n++)assert(response[2+n]==(n==0?1:n==2?16:0));
#else
 assert(response_length==2);
#endif
 uint8_t same[EPD_SCREEN_WIRE_SIZE];assert(epd_screen_config_encode(&p.screen_config,same)==EPD_SCREEN_STATUS_OK);
 unsigned unchanged=operation;factory_qualified=false;set_profile(&p,same,sizeof(same));assert(response_status==INKTAG_STATUS_PERSISTENCE_ERROR && operation==unchanged);factory_qualified=true;set_profile(&p,same,sizeof(same));assert(response_status==INKTAG_STATUS_OK&&operation==unchanged);
 epd_screen_config_t scaled=p.screen_config;scaled.native_ui_scale=scaled.native_ui_scale==1?2:1;
 uint8_t scale_wire[EPD_SCREEN_WIRE_SIZE];assert(epd_screen_config_encode(&scaled,scale_wire)==EPD_SCREEN_STATUS_OK);
 set_profile(&p,scale_wire,sizeof(scale_wire));assert(response_status==INKTAG_STATUS_OK&&operation==unchanged);
 inktag_image_store_header_t scaled_header;assert(inktag_image_store_get(0,&scaled_header)==INKTAG_IMAGE_STORE_OK);
 set_profile(&p,same,sizeof(same));assert(response_status==INKTAG_STATUS_OK&&operation==unchanged);profile_writes=stops=0;
 uint8_t wire[EPD_SCREEN_WIRE_SIZE];epd_screen_config_t changed=p.screen_config;changed.transform=1;assert(epd_screen_config_encode(&changed,wire)==EPD_SCREEN_STATUS_OK);
 unsigned writes=operation;m_epd_operation_active=true;set_profile(&p,wire,sizeof(wire));assert(response_status==INKTAG_STATUS_BUSY&&operation==writes&&!profile_writes);m_epd_operation_active=false;
 set_profile(&p,wire,1);assert(response_status==INKTAG_STATUS_INCOMPATIBLE&&!profile_writes);
#if INKTAG_NRF_FIXED_IMAGE_SLOTS
 s_allow_health=false;set_profile(&p,wire,sizeof(wire));assert(response_status==INKTAG_STATUS_LOW_POWER&&operation==writes&&!profile_writes&&p.screen_config.transform==0);
 s_allow_health=true;fail_at=operation+1;set_profile(&p,wire,sizeof(wire));assert(response_status==INKTAG_STATUS_IO_ERROR&&!profile_writes&&p.screen_config.transform==0);fail_at=0;reboot();
#endif
 persist_failure=true;set_profile(&p,wire,sizeof(wire));assert(response_status==INKTAG_STATUS_PERSISTENCE_ERROR&&p.screen_config.transform==0&&persisted_screen.transform==0&&!stops);
 persist_failure=false;set_profile(&p,wire,sizeof(wire));assert(response_status==INKTAG_STATUS_OK&&p.screen_config.transform==1&&persisted_screen.transform==1&&stops==1);
#if INKTAG_NRF_FIXED_IMAGE_SLOTS
 inktag_image_store_header_t h;assert(inktag_image_store_get(20,&h)==INKTAG_IMAGE_STORE_NOT_FOUND);reboot();assert(inktag_image_store_get(20,&h)==INKTAG_IMAGE_STORE_NOT_FOUND);epd_v2_list_resource(&p,0,1,NULL,0);assert(response[0]==1);
#endif
 puts("PASS production INFO/LIST capacity+hidden occupancy and profile busy/invalid/low/IO/persistence/reboot gates");
}
'''
manifest=json.loads((ROOT/('tests/source-test-manifest.json' if (ROOT/'tests/source-test-manifest.json').is_file() else 'tests/host-test-manifest.json')).read_text());entry=next(x for x in manifest['hostC'] if x['id']=='test_nrf_fixed_image_store')
with tempfile.TemporaryDirectory() as temp:
 c=Path(temp)/'test.c';c.write_text(fixture+prefix+matches+'\n'+function('epd_v2_screen_images_match')+'\n'+function('epd_v2_send_capabilities')+'\n'+function('epd_v2_send_resource_info')+'\n'+function('epd_v2_list_resource')+'\n'+wrapper+main)
 for fixed in (0,1):
  for activation in (0,):
   binary=Path(temp)/f'test-{fixed}-{activation}';flags=[f for f in entry['defines'] if not f.startswith('-DINKTAG_NRF_FIXED_IMAGE_SLOTS=')]
   subprocess.run(['gcc','-std=c99','-O2','-Itests',*flags,'-DINKTAG_NRF_FIXED_IMAGE_SLOTS=1',f'-DPROTOCOL_FIXED={fixed}',f'-DINKTAG_NRF_ACTIVATION_ENABLED={activation}',str(c),*entry['sources'][1:],'-o',str(binary)],cwd=ROOT,check=True)
   subprocess.run([str(binary)],cwd=ROOT,check=True)
