/* Independent native pin ABI: N1P1/40 Factory and N1R1/100 User.
 * Shared pin16 codecs are reused; native persistence remains independent. */
#include "inktag_nrf_config_store.h"
#include "inktag_protocol_core.h"
#include "nrf_error.h"
#include "inktag_nrf_pins.h"

#define NRF11_FACTORY_MAGIC UINT32_C(0x3150314e) /* N1P1 */
#define NRF11_USER_MAGIC UINT32_C(0x3152314e) /* N1R1 */
#define NRF11_LEGACY_FACTORY_MAGIC UINT32_C(0x3146314e) /* N1F1 */
#define NRF11_LEGACY_USER_MAGIC UINT32_C(0x3155314e) /* N1U1 */
#define TIME_RECORD_MAGIC UINT32_C(0x314d544e) /* NTM1 */
#define DISPLAY_STATE_RECORD_MAGIC UINT32_C(0x3150444e) /* NDP1 */

typedef struct {
    uint32_t magic;
    epd_config_t config;
    uint8_t screen[17];
    uint16_t active_low;
    uint32_t checksum;
} nrf11_factory_record_t;
typedef struct {
    uint32_t magic;
    epd_device_settings_t settings;
    uint8_t display_length;
    uint8_t display[21];
    uint8_t pin_override_present;
    uint8_t reserved;
    uint8_t pins[INKTAG_PIN_CONFIG_SIZE];
    uint32_t checksum;
} nrf11_user_record_t;
typedef struct {
    uint32_t magic;
    uint16_t version;
    int16_t timezone_minutes;
    uint32_t timestamp;
    uint32_t checksum;
} epd_time_state_record_t;
typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t reserved[3];
    epd_display_state_t state;
} epd_display_state_record_t;
_Static_assert(sizeof(nrf11_factory_record_t)==40u &&
    offsetof(nrf11_factory_record_t,screen)==17u &&
    offsetof(nrf11_factory_record_t,checksum)==36u,"N1P1 ABI");
_Static_assert(sizeof(nrf11_user_record_t)==100u &&
    offsetof(nrf11_user_record_t,display_length)==56u &&
    offsetof(nrf11_user_record_t,display)==57u &&
    offsetof(nrf11_user_record_t,pin_override_present)==78u &&
    offsetof(nrf11_user_record_t,pins)==80u &&
    offsetof(nrf11_user_record_t,checksum)==96u,"N1R1 ABI");
_Static_assert(sizeof(epd_time_state_record_t)==16u &&
    sizeof(epd_display_state_record_t)==16u,"time/replay ABI");

static uint32_t m_screen_write_error;
static bool nrf11_zero(const uint8_t *data,size_t length) {
    while(length--) if(*data++) return false;
    return true;
}
static const struct {
    uint8_t pins[11];
    uint8_t mode;
    uint8_t week;
} nrf11_defaults = { INKTAG_BOARD_DEFAULT_EPD_CONFIG,
    EPD_DISPLAY_STATE_CALENDAR, INKTAG_DISPLAY_SETTINGS_DEFAULT_WEEK_START };
_Static_assert(sizeof(nrf11_defaults)==sizeof(epd_config_t),"factory defaults ABI");
static __attribute__((noinline,noclone)) bool nrf11_native_pin_wire(const epd_config_t *cfg,uint16_t low,uint8_t wire[16]) {
    wire[0]=INKTAG_PIN_CONFIG_VERSION;wire[1]=0u;
    wire[2]=(uint8_t)low;wire[3]=(uint8_t)(low>>8u);
    memcpy(wire+4u,cfg,6u);wire[10]=cfg->en_pin;
    memcpy(wire+11u,&cfg->reserved_screen_legacy,3u);
    wire[14]=wire[15]=INKTAG_PIN_UNUSED;
    return cfg->bs_pin==INKTAG_BOARD_EPD_BS_PIN && inktag_nrf_pins_valid(wire);
}
static bool nrf11_effective_pins_read(uint8_t wire[16]);
static inktag_settings_journal_read_result_t nrf11_factory_read_pins(
    nrf11_factory_record_t *record,uint8_t pins[16]) {
    uint16_t length=0u;
    inktag_settings_journal_read_result_t result=inktag_nrf_config_store_read_result(
        INKTAG_NRF_CONFIG_BOARD,record,sizeof(*record),&length);
    if(result!=INKTAG_SETTINGS_JOURNAL_READ_FOUND) {
        /* An absent factory record alone selects the exact board defaults.
         * This is RAM adaptation, never an implicit provisioning write. */
        if(result==INKTAG_SETTINGS_JOURNAL_READ_MISSING) {
            memset(record,0,sizeof(*record));
            memcpy(&record->config,&nrf11_defaults,sizeof(record->config));
            record->active_low=0x380u;
            if(!nrf11_native_pin_wire(&record->config,record->active_low,pins))
                return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
        }
        return result;
    }
    if(length!=sizeof(*record) || record->checksum!=epd_record_checksum(
        (const uint8_t*)record,offsetof(nrf11_factory_record_t,checksum)))
        return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    if(record->magic==NRF11_LEGACY_FACTORY_MAGIC) {
        /* N1F1 had fixed wiring. Its reserved/wakeup bytes are not RGB or IRQ
         * configuration. Validate that original ABI before projecting RGB. */
        static const uint8_t original[11]={20,19,6,5,4,3,2,255,255,18,7};
        if(record->active_low || memcmp(&record->config,original,sizeof(original)))
            return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
        record->config.reserved_screen_legacy=INKTAG_BOARD_LED_RED_PIN;
        record->config.wakeup_pin=INKTAG_BOARD_LED_GREEN_PIN;
        record->active_low=0x380u;
    } else if(record->magic!=NRF11_FACTORY_MAGIC)return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    if(!nrf11_native_pin_wire(&record->config,record->active_low,pins) ||
       record->config.display_mode>2u || record->config.week_start>1u)
        return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    if(!nrf11_zero(record->screen,sizeof(record->screen))) {
        epd_screen_config_t screen;
        if(epd_screen_config_decode_exact(record->screen,sizeof(record->screen),&screen)!=EPD_SCREEN_STATUS_OK)
            return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    }
    return result;
}
static inktag_settings_journal_read_result_t nrf11_factory_read(nrf11_factory_record_t *record) {
    uint8_t pins[16];return nrf11_factory_read_pins(record,pins);
}
void epd_config_init(epd_config_t *cfg) {
    (void)cfg;(void)inktag_nrf_config_store_init();
}
void epd_config_read(epd_config_t *cfg) {
    nrf11_factory_record_t factory;uint8_t pins[16];
    if(!cfg)return;
    if(nrf11_factory_read_pins(&factory,pins)==INKTAG_SETTINGS_JOURNAL_READ_ERROR ||
       !nrf11_effective_pins_read(pins) || !inktag_nrf_pins_publish(pins)) {
        memset(cfg,255,sizeof(*cfg));inktag_nrf_pins_invalidate();return;
    }
    *cfg=factory.config;inktag_nrf_pins_project(cfg);
}
void epd_config_write(epd_config_t *cfg) { (void)cfg; /* immutable factory */ }
void epd_config_clear(epd_config_t *cfg) { if(cfg)memset(cfg,255,sizeof(*cfg)); }
bool epd_config_empty(epd_config_t *cfg) {
    /* Only a fully validated factory prefix is published. A read error sets
     * the BS sentinel to FF; later mode/week updates must not hide that sentinel. */
    return !cfg || cfg->bs_pin==255u;
}
bool epd_time_state_read(uint32_t *timestamp,int16_t *timezone) {
    epd_time_state_record_t record;uint16_t length=0u;
    if(!timestamp || !inktag_nrf_config_store_read(INKTAG_NRF_CONFIG_TIME,
       &record,sizeof(record),&length) || length!=sizeof(record) ||
       record.magic!=TIME_RECORD_MAGIC || record.version!=1u ||
       record.timezone_minutes < -840 || record.timezone_minutes>840 ||
       record.checksum!=epd_record_checksum((const uint8_t*)&record,12u))return false;
    *timestamp=record.timestamp;if(timezone)*timezone=record.timezone_minutes;return true;
}
bool epd_time_state_write(uint32_t timestamp,int16_t timezone) {
    if(timezone < -840 || timezone>840)return false;
    epd_time_state_record_t record={TIME_RECORD_MAGIC,1u,timezone,timestamp,0u};
    record.checksum=epd_record_checksum((const uint8_t*)&record,12u);
    return inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_TIME,&record,sizeof(record));
}
bool epd_time_read(uint32_t *timestamp) {return epd_time_state_read(timestamp,NULL);}
bool epd_time_write(uint32_t timestamp) {
    uint32_t previous;int16_t timezone=0;
    (void)epd_time_state_read(&previous,&timezone);return epd_time_state_write(timestamp,timezone);
}
inktag_settings_journal_read_result_t epd_screen_config_read_result(epd_screen_config_t *screen) {
    epd_screen_record_t record; uint16_t length=0u;
    if(!screen)return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    inktag_settings_journal_read_result_t result=inktag_nrf_config_store_read_result(
        INKTAG_NRF_CONFIG_SCREEN,&record,sizeof(record),&length);
    if(result==INKTAG_SETTINGS_JOURNAL_READ_FOUND)
        return length==sizeof(record) && epd_screen_record_decode(&record,screen)==EPD_SCREEN_STATUS_OK ?
            result : INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    if(result==INKTAG_SETTINGS_JOURNAL_READ_ERROR)return result;
    nrf11_factory_record_t factory;
    result=nrf11_factory_read(&factory);
    if(result!=INKTAG_SETTINGS_JOURNAL_READ_FOUND)return result;
    if(nrf11_zero(factory.screen,sizeof(factory.screen)))return INKTAG_SETTINGS_JOURNAL_READ_MISSING;
    return epd_screen_config_decode_exact(factory.screen,sizeof(factory.screen),screen)==EPD_SCREEN_STATUS_OK ?
        INKTAG_SETTINGS_JOURNAL_READ_FOUND : INKTAG_SETTINGS_JOURNAL_READ_ERROR;
}
bool epd_screen_config_read(epd_screen_config_t *screen) {
    return epd_screen_config_read_result(screen)==INKTAG_SETTINGS_JOURNAL_READ_FOUND;
}
bool epd_screen_config_write(const epd_screen_config_t *screen) {
    epd_screen_record_t record;
    if(epd_screen_record_encode(screen,&record)!=EPD_SCREEN_STATUS_OK) {
        m_screen_write_error=NRF_ERROR_INVALID_PARAM;return false;
    }
    bool ok=inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_SCREEN,&record,sizeof(record));
    m_screen_write_error=inktag_nrf_config_store_last_error();return ok;
}
uint32_t epd_screen_config_last_write_error(void){return m_screen_write_error;}
void epd_screen_config_clear(void){(void)inktag_nrf_config_store_delete(INKTAG_NRF_CONFIG_SCREEN);}

static inktag_settings_journal_read_result_t nrf11_user_read(
    nrf11_user_record_t *record,inktag_display_settings_t *display) {
    uint16_t length=0u;
    memset(record,0,sizeof(*record));
    memset(display,0,sizeof(*display));display->week_start=INKTAG_DISPLAY_SETTINGS_DEFAULT_WEEK_START;
    inktag_settings_journal_read_result_t result=inktag_nrf_config_store_read_result(
        INKTAG_NRF_CONFIG_DEVICE_SETTINGS,record,sizeof(*record),&length);
    if(result==INKTAG_SETTINGS_JOURNAL_READ_ERROR)return result;
    if(result==INKTAG_SETTINGS_JOURNAL_READ_MISSING) {
        epd_device_settings_default(&record->settings);
        nrf11_factory_record_t factory;
        if(nrf11_factory_read(&factory)==INKTAG_SETTINGS_JOURNAL_READ_ERROR)return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
        display->week_start=factory.config.week_start;return result;
    }
    if(length==84u) {
        if(record->magic!=NRF11_LEGACY_USER_MAGIC || record->pin_override_present || record->reserved ||
           inktag_read_u32_le(record->pins)!=epd_record_checksum((const uint8_t*)record,80u))
            return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
        /* N1U1 offsets 0..77 retain their meaning; its checksum is not pin data. */
        memset(record->pins,0,sizeof(record->pins));
    } else if(length!=sizeof(*record) || record->magic!=NRF11_USER_MAGIC ||
              record->checksum!=epd_record_checksum((const uint8_t*)record,96u))
        return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    if(
       !epd_device_settings_valid(&record->settings) ||
       record->reserved || record->pin_override_present>1u ||
       (record->pin_override_present ?
          (!inktag_nrf_pin_epoch_ready() || !inktag_nrf_pins_valid(record->pins)) :
          !nrf11_zero(record->pins,sizeof(record->pins))) ||
       !nrf11_zero(record->settings.name+record->settings.name_length,
                    sizeof(record->settings.name)-record->settings.name_length) ||
       !nrf11_zero(record->settings.label+record->settings.label_length,
                    sizeof(record->settings.label)-record->settings.label_length) ||
       record->display_length<9u || record->display_length>sizeof(record->display) ||
       !nrf11_zero(record->display+record->display_length,sizeof(record->display)-record->display_length) ||
       !inktag_display_settings_decode(record->display,record->display_length,display))
        return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    return result;
}
static bool nrf11_user_encode(nrf11_user_record_t *record,const inktag_display_settings_t *display) {
    memset(record->display,0,sizeof(record->display));
    size_t length=inktag_display_settings_encode(display,record->display,sizeof(record->display));
    if(!length || !epd_device_settings_valid(&record->settings))return false;
    memset(record->settings.name+record->settings.name_length,0,
           sizeof(record->settings.name)-record->settings.name_length);
    memset(record->settings.label+record->settings.label_length,0,
           sizeof(record->settings.label)-record->settings.label_length);
    record->reserved=0u;
    if(!record->pin_override_present)memset(record->pins,0,sizeof(record->pins));
    record->magic=NRF11_USER_MAGIC;record->display_length=(uint8_t)length;
    record->checksum=epd_record_checksum((const uint8_t*)record,96u);return true;
}
bool inktag_nrf_factory_pins(uint8_t wire[16]) {
    nrf11_factory_record_t factory;
    return wire && nrf11_factory_read_pins(&factory,wire)!=INKTAG_SETTINGS_JOURNAL_READ_ERROR;
}
static bool nrf11_effective_pins_read(uint8_t wire[16]) {
    nrf11_user_record_t user;inktag_display_settings_t display;
    /* Caller already supplied validated factory pins; only the user layer
     * remains. Avoid rereading and validating the factory a second time. */
    if(nrf11_user_read(&user,&display)==INKTAG_SETTINGS_JOURNAL_READ_ERROR)return false;
    if(user.pin_override_present)memcpy(wire,user.pins,16u);
    return true;
}
static bool nrf11_user_verify(const nrf11_user_record_t *record) {
    return inktag_nrf_config_store_read_matches(INKTAG_NRF_CONFIG_DEVICE_SETTINGS,
                                               record,sizeof(*record));
}
static bool nrf11_user_write(const nrf11_user_record_t *record) {
    return inktag_nrf_pin_epoch_ready() &&
        inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_DEVICE_SETTINGS,record,sizeof(*record)) &&
        nrf11_user_verify(record);
}
inktag_status_v2_t inktag_nrf_settings_pins_commit(const uint8_t wire[16],uint8_t effective[16]) {
    nrf11_user_record_t user;inktag_display_settings_t display;uint8_t defaults[16];
    if(!inktag_nrf_pin_epoch_ready())return INKTAG_STATUS_INCOMPATIBLE;
    if(wire && !inktag_nrf_pins_valid(wire))return INKTAG_STATUS_INVALID_REQUEST;
    if(!inktag_nrf_factory_pins(defaults) ||
       nrf11_user_read(&user,&display)==INKTAG_SETTINGS_JOURNAL_READ_ERROR)return INKTAG_STATUS_PERSISTENCE_ERROR;
    user.pin_override_present=wire && memcmp(wire,defaults,16u)!=0;
    if(user.pin_override_present)memcpy(user.pins,wire,16u);
    if(!nrf11_user_encode(&user,&display) || !nrf11_user_write(&user))return INKTAG_STATUS_PERSISTENCE_ERROR;
    if(effective)memcpy(effective,wire?wire:defaults,16u);
    return INKTAG_STATUS_OK;
}
bool inktag_nrf_settings_pins_write(const uint8_t wire[16]) {
    return inktag_nrf_settings_pins_commit(wire,NULL)==INKTAG_STATUS_OK;
}
static epd_display_state_record_t nrf11_replay_record(const epd_display_state_t *state) {
    epd_display_state_record_t record={.magic=DISPLAY_STATE_RECORD_MAGIC,.version=1u,.state=*state};
    record.state.reserved=0u;return record;
}
bool epd_display_state_read(epd_display_state_t *state) {
    epd_display_state_record_t record;uint16_t length=0u;
    if(!state || !inktag_nrf_config_store_read(INKTAG_NRF_CONFIG_DISPLAY_STATE,
        &record,sizeof(record),&length) || length!=sizeof(record) ||
        record.magic!=DISPLAY_STATE_RECORD_MAGIC || record.version!=1u ||
        !nrf11_zero(record.reserved,sizeof(record.reserved)) || record.state.reserved ||
        !epd_display_state_valid(&record.state))return false;
    *state=record.state;
    nrf11_user_record_t user;inktag_display_settings_t display;
    if(nrf11_user_read(&user,&display)==INKTAG_SETTINGS_JOURNAL_READ_ERROR)return false;
    state->reserved=display.flags;return true;
}
bool epd_display_state_write(const epd_display_state_t *state) {
    if(!epd_display_state_valid(state))return false;
    epd_display_state_record_t record=nrf11_replay_record(state);
    return inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_DISPLAY_STATE,&record,sizeof(record));
}
void epd_display_state_clear(void){(void)inktag_nrf_config_store_delete(INKTAG_NRF_CONFIG_DISPLAY_STATE);}
bool epd_display_settings_read(inktag_display_settings_t *display) {
    nrf11_user_record_t user;
    return display && nrf11_user_read(&user,display)!=INKTAG_SETTINGS_JOURNAL_READ_ERROR;
}
bool epd_display_settings_write(const epd_display_state_t *state,
                                const inktag_display_settings_t *display) {
    nrf11_user_record_t user;inktag_display_settings_t old;
    if(!epd_display_state_valid(state) || !display || display->flags!=(uint8_t)state->reserved ||
       nrf11_user_read(&user,&old)==INKTAG_SETTINGS_JOURNAL_READ_ERROR ||
       !nrf11_user_encode(&user,display))return false;
    return nrf11_user_write(&user);
}
bool epd_display_settings_cleanup(const epd_display_state_t *state,
    const inktag_display_settings_t *display,uint32_t mask) {
    nrf11_user_record_t user;inktag_display_settings_t old;
    if(!epd_display_state_valid(state) || !display)return false;
    if(mask&INKTAG_SETTINGS_RECORD_BIT(INKTAG_NRF_CONFIG_DEVICE_SETTINGS)) {
        /* Explicit clear-settings owns this entire preference record. It may
         * replace corrupt ordinary data; image-only clear must preserve it. */
        memset(&user,0,sizeof(user));epd_device_settings_default(&user.settings);
    } else if(nrf11_user_read(&user,&old)==INKTAG_SETTINGS_JOURNAL_READ_ERROR)return false;
    if(!nrf11_user_encode(&user,display))return false;
    epd_display_state_record_t replay=nrf11_replay_record(state);
    return inktag_nrf_pin_epoch_ready() &&
        inktag_nrf_config_store_cleanup_user(mask,&user,sizeof(user),&replay,sizeof(replay)) &&
        nrf11_user_verify(&user);
}
bool epd_device_settings_read(epd_device_settings_t *settings) {
    nrf11_user_record_t user;inktag_display_settings_t display;
    if(!settings || nrf11_user_read(&user,&display)==INKTAG_SETTINGS_JOURNAL_READ_ERROR)return false;
    *settings=user.settings;return true;
}
bool epd_device_settings_write(const epd_device_settings_t *settings) {
    nrf11_user_record_t user;inktag_display_settings_t display;
    if(!epd_device_settings_valid(settings) ||
       nrf11_user_read(&user,&display)==INKTAG_SETTINGS_JOURNAL_READ_ERROR)return false;
    user.settings=*settings;
    return nrf11_user_encode(&user,&display) &&
        nrf11_user_write(&user);
}
void epd_device_settings_clear(void){(void)inktag_nrf_config_store_delete(INKTAG_NRF_CONFIG_DEVICE_SETTINGS);}

/* The shared OFF/CRC decoder is inserted unchanged from the active source. */
bool epd_device_info_qr_read(uint8_t slot, epd_device_info_qr_t *qr) {
    uint16_t length=0u;
    if(slot>=EPD_DEVICE_INFO_QR_SLOT_COUNT || !qr ||
       !inktag_nrf_config_store_read(INKTAG_NRF_CONFIG_DEVICE_INFO_QR,
            qr,sizeof(*qr),&length) || length<9u ||
       qr->version!=1u || qr->state!=0u || qr->modules<21u ||
       qr->modules>EPD_DEVICE_INFO_QR_MAX_MODULES || ((qr->modules-21u)&3u))return false;
    uint16_t expected=((uint16_t)qr->modules*qr->modules+7u)/8u;
    return qr->length==expected && length==9u+expected &&
        (inktag_crc32_update(UINT32_MAX,qr->data,expected)^UINT32_MAX)==qr->crc;
}
