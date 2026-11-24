#include <assert.h>
#include <stdio.h>
#include <string.h>
#define INKTAG_NRF_STREAMING_CONFIG_STORE 1
#define INKTAG_BOARD_EXTERNAL_FLASH_SETTINGS_START 0u
#define INKTAG_BOARD_SETTINGS_SIZE 8192u
#define INKTAG_BOARD_EXTERNAL_FLASH_CAPACITY 0x80000u
#define INKTAG_BOARD_EXTERNAL_FLASH_ASSET_OFFSET 0x2000u
#define INKTAG_BOARD_EXTERNAL_FLASH_ASSET_SIZE 0x3000u
#define INKTAG_BOARD_DFU_SETTINGS_START 0x2f000u
#define INKTAG_BOARD_DEFAULT_EPD_CONFIG {20,19,6,5,4,3,2,255,255,18,7}
#include "inktag_nrf_config_store.c"
#define INKTAG_NRF_COMPACT_STORE 1
#define INKTAG_NRF_PROTOCOL_V2 1
#define INKTAG_DEVICE_SETTINGS_ENABLED 1
#include "candidate-config.c"

static uint8_t bytes[8192], saved[8192];
static unsigned sessions, writes, erases, ops;
static int cut=-1, cut_mode;
static int silent_drop_at=-1;
static bool read_error, silent_drop;
static uint32_t last_error;
bool inktag_nrf_external_flash_begin(void){++sessions;return true;}
void inktag_nrf_external_flash_end(void){assert(sessions);--sessions;}
uint32_t inktag_nrf_external_flash_last_error(void){return last_error;}
bool inktag_nrf_external_flash_read(uint32_t address,void *data,uint32_t length){
    assert(sessions && address+length<=sizeof(bytes));
    if(read_error){last_error=NRF_ERROR_INTERNAL;return false;}
    memcpy(data,bytes+address,length);last_error=NRF_SUCCESS;return true;
}
bool inktag_nrf_external_flash_program(uint32_t address,const void *data,uint16_t length){
    assert(sessions && length && address+length<=sizeof(bytes) && (address&255u)+length<=256u);
    bool fail=(int)ops++==cut;
    unsigned n=fail?(cut_mode==2?length:cut_mode==1?length/2u:0u):length;
    if(silent_drop || (int)(ops-1u)==silent_drop_at){n=0;silent_drop=false;}
    for(unsigned i=0;i<n;++i){assert((bytes[address+i]&((const uint8_t*)data)[i])==((const uint8_t*)data)[i]);bytes[address+i]&=((const uint8_t*)data)[i];}
    ++writes;last_error=fail?NRF_ERROR_INTERNAL:NRF_SUCCESS;return !fail;
}
bool inktag_nrf_external_flash_erase_sector(uint32_t address){
    assert(sessions && !(address&4095u) && address+4096u<=sizeof(bytes));
    bool fail=(int)ops++==cut;
    if(!fail || cut_mode==2)memset(bytes+address,255,4096);
    ++erases;last_error=fail?NRF_ERROR_INTERNAL:NRF_SUCCESS;return !fail;
}
static void cold(void){assert(!sessions);s_initialized=false;cut=-1;assert(inktag_nrf_config_store_init());}
static nrf11_user_record_t user_record(void){
    nrf11_user_record_t record;uint16_t length=0;
    assert(inktag_nrf_config_store_read(INKTAG_NRF_CONFIG_DEVICE_SETTINGS,&record,sizeof(record),&length));
    assert(length==100u);return record;
}
static void restore(void){memcpy(bytes,saved,sizeof(bytes));cold();}
static void factory_seed(void){
    nrf11_factory_record_t record={.magic=NRF11_FACTORY_MAGIC,.active_low=0x380u};
    memcpy(&record.config,&nrf11_defaults,sizeof(record.config));
    epd_screen_config_t screen={EPD_SCREEN_SCHEMA_VERSION,EPD_SCREEN_DRIVER_SSD1619,
        INKTAG_RECIPE_SSD1619_HEMA_213_BWR,EPD_SCREEN_COLOR_BWR,3u,
        128u,250u,0u,0u,122u,250u,0u};
    assert(epd_screen_config_encode(&screen,record.screen)==EPD_SCREEN_STATUS_OK);
    record.checksum=epd_record_checksum((const uint8_t*)&record,36);
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_BOARD,&record,sizeof(record)));
}

static void epoch_refresh(void){
    test_native_dfu[0]=~inktag_crc32_update(UINT32_MAX,(const uint8_t*)test_native_dfu+4,88);
}
static void pin_roundtrip(void){
    memset(bytes,255,sizeof(bytes));cold();factory_seed();
    epd_config_t config;epd_config_read(&config);assert(!epd_config_empty(&config));
    uint8_t defaults[16],next[16];assert(inktag_nrf_factory_pins(defaults));
    memcpy(next,defaults,16);next[2]=0xc0;next[3]=3;
    for(unsigned role=0;role<10;++role)next[4+role]=defaults[4+(role+1)%10];
    assert(inktag_nrf_pins_valid(next));
    assert(inktag_nrf_settings_pins_write(next));cold();epd_config_read(&config);
    assert(!memcmp(inktag_nrf_pins_current(),next,16));
    assert(config.reserved_screen_legacy==next[11] && config.wakeup_pin==next[12]);
    nrf11_user_record_t old=user_record();
    assert(old.pin_override_present==1 && !memcmp(old.pins,next,16));
    epd_device_settings_t settings;assert(epd_device_settings_read(&settings));
    settings.label_length=3;memcpy(settings.label,"Pin",3);
    assert(epd_device_settings_write(&settings));
    epd_display_state_t state={EPD_DISPLAY_STATE_CALENDAR,255,1,0};
    inktag_display_settings_t display={.week_start=0,.flags=1,.target_day=20000,.name_length=1,.name="N"};
    assert(epd_display_settings_write(&state,&display));
    old=user_record();assert(old.pin_override_present && !memcmp(old.pins,next,16));
    assert(epd_display_settings_cleanup(&state,&display,INKTAG_SETTINGS_CLEAR_IMAGES_MASK));
    nrf11_user_record_t after=user_record();assert(!memcmp(&after,&old,sizeof(old)));
    assert(inktag_nrf_settings_pins_write(NULL));cold();epd_config_read(&config);
    assert(!memcmp(inktag_nrf_pins_current(),defaults,16));
    after=user_record();assert(!after.pin_override_present && nrf11_zero(after.pins,16));
    assert(after.settings.label_length==3 && after.settings.label[0]=='P');
    assert(epd_display_settings_read(&display) && display.target_day==20000);
    assert(inktag_nrf_settings_pins_write(next));

    static uint8_t pin_snapshot[8192];memcpy(pin_snapshot,bytes,sizeof(bytes));
    ops=0;assert(inktag_nrf_settings_pins_write(NULL));unsigned pin_boundaries=ops;
    for(cut_mode=0;cut_mode<3;++cut_mode)for(unsigned at=0;at<pin_boundaries;++at){
        memcpy(bytes,pin_snapshot,sizeof(bytes));cold();ops=0;cut=(int)at;
        (void)inktag_nrf_settings_pins_write(NULL);cold();
        after=user_record();
        assert(after.settings.label_length==3 && after.settings.label[0]=='P');
        assert(!after.pin_override_present || !memcmp(after.pins,next,16));
        if(!after.pin_override_present)assert(nrf11_zero(after.pins,16));
        assert(epd_display_settings_read(&display) && display.target_day==20000);
    }
    memcpy(bytes,pin_snapshot,sizeof(bytes));cold();
    for(unsigned at=0;at<pin_boundaries;++at){
        memcpy(bytes,pin_snapshot,sizeof(bytes));cold();ops=0;silent_drop_at=(int)at;
        assert(!inktag_nrf_settings_pins_write(NULL));silent_drop_at=-1;cold();
        after=user_record();assert(after.pin_override_present && !memcmp(after.pins,next,16));
    }
    memcpy(bytes,pin_snapshot,sizeof(bytes));cold();
    silent_drop=true;assert(!inktag_nrf_settings_pins_write(NULL));
    cold();after=user_record();assert(after.pin_override_present && !memcmp(after.pins,next,16));
    assert(inktag_nrf_config_store_read_matches(INKTAG_NRF_CONFIG_DEVICE_SETTINGS,&after,sizeof(after)));
    after.pins[4]^=1;assert(!inktag_nrf_config_store_read_matches(INKTAG_NRF_CONFIG_DEVICE_SETTINGS,&after,sizeof(after)));
    read_error=true;assert(!inktag_nrf_config_store_read_matches(INKTAG_NRF_CONFIG_DEVICE_SETTINGS,&after,sizeof(after)));read_error=false;
    cold();
    printf("PASS pin override/restore power cuts: %u boundaries x3; silent-program/readback failure rejected\n",pin_boundaries);

    /* A durable native SDK confirmation, not an App constant, gates both SET
     * and consumption of the persisted override. Every gate has a valid CRC. */
    const unsigned words[]={1,2,3,5,6,8,11};
    const uint32_t values[]={1,0x000c8u,0x200c9u,1,0,0,1};
    for(unsigned i=0;i<sizeof(words)/sizeof(words[0]);++i){
        uint32_t previous=test_native_dfu[words[i]];test_native_dfu[words[i]]=values[i];epoch_refresh();
        unsigned before=writes+erases;assert(!inktag_nrf_pin_epoch_ready());
        assert(!inktag_nrf_settings_pins_write(defaults));
        epd_config_read(&config);assert(epd_config_empty(&config) && !inktag_nrf_pins_current());
        assert(writes+erases==before);test_native_dfu[words[i]]=previous;epoch_refresh();
    }
    test_native_dfu[0]^=1;assert(!inktag_nrf_pin_epoch_ready());
    assert(!inktag_nrf_settings_pins_write(NULL));epoch_refresh();
    assert(inktag_nrf_pin_epoch_ready());epd_config_read(&config);
    assert(!memcmp(inktag_nrf_pins_current(),next,16));
    display=(inktag_display_settings_t){.week_start=1};state.reserved=0;
    assert(epd_display_settings_cleanup(&state,&display,INKTAG_SETTINGS_RESET_SETTINGS_V2_MASK));
    cold();epd_config_read(&config);assert(!memcmp(inktag_nrf_pins_current(),defaults,16));

    /* Malformed legacy records are errors, never absent/default records. */
    uint8_t legacy_user[84]={0};memcpy(legacy_user,"N1U1",4);
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_DEVICE_SETTINGS,legacy_user,sizeof(legacy_user)));
    unsigned before=writes+erases;assert(!epd_device_settings_read(&settings));
    assert(!inktag_nrf_settings_pins_write(next) && writes+erases==before);
    nrf11_factory_record_t factory;assert(nrf11_factory_read(&factory)==INKTAG_SETTINGS_JOURNAL_READ_FOUND);
    factory.magic=UINT32_C(0x3146314e);factory.checksum=epd_record_checksum((const uint8_t*)&factory,36);
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_BOARD,&factory,sizeof(factory)));
    before=writes+erases;epd_config_read(&config);
    assert(epd_config_empty(&config) && !inktag_nrf_pins_current() && writes+erases==before);
    assert(!test_critical && !sessions);
    puts("PASS N1P1/N1R1 ten-role override, sibling writes, restore/reset, immutable Factory, native confirmed epoch/CRC gates and malformed legacy rejection");
}

static void legacy_ota_read_upgrade(void){
    memset(bytes,255,sizeof(bytes));cold();
    nrf11_factory_record_t factory;epd_config_t config;epd_screen_config_t screen;
    uint8_t defaults[16]=INKTAG_BOARD_DEFAULT_PIN_WIRE;
    unsigned before=writes+erases;
    assert(nrf11_factory_read(&factory)==INKTAG_SETTINGS_JOURNAL_READ_MISSING);
    epd_config_read(&config);assert(!epd_config_empty(&config));
    assert(!memcmp(inktag_nrf_pins_current(),defaults,16));
    assert(epd_screen_config_read_result(&screen)==INKTAG_SETTINGS_JOURNAL_READ_MISSING);
    assert(writes+erases==before); /* no boot-time migration/provisioning */
    factory_seed();assert(nrf11_factory_read(&factory)==INKTAG_SETTINGS_JOURNAL_READ_FOUND);
    factory.magic=NRF11_LEGACY_FACTORY_MAGIC;factory.active_low=0;
    factory.config.reserved_screen_legacy=255;factory.config.wakeup_pin=255;
    factory.config.display_mode=2;factory.config.week_start=0;
    factory.checksum=epd_record_checksum((const uint8_t*)&factory,36);
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_BOARD,&factory,40));
    nrf11_user_record_t user={0};epd_device_settings_default(&user.settings);
    user.settings.label_length=3;memcpy(user.settings.label,"Old",3);
    inktag_display_settings_t display={.week_start=0,.flags=1,.target_day=20500,.name_length=3,.name="Day"};
    assert(nrf11_user_encode(&user,&display));user.magic=NRF11_LEGACY_USER_MAGIC;
    inktag_write_u32_le(user.pins,epd_record_checksum((const uint8_t*)&user,80));
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_DEVICE_SETTINGS,&user,84));
    uint8_t sibling[3]={4,5,6};
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_HEALTH,sibling,3));
    static uint8_t legacy_snapshot[8192];memcpy(legacy_snapshot,bytes,sizeof(bytes));
    before=writes+erases;cold();epd_config_read(&config);
    assert(!memcmp(inktag_nrf_pins_current(),defaults,16));
    assert(config.display_mode==2 && config.week_start==0);
    assert(config.reserved_screen_legacy==16 && config.wakeup_pin==17 && config.led_pin==18);
    assert(epd_screen_config_read(&screen) && screen.visible_width==122);
    epd_device_settings_t settings;assert(epd_device_settings_read(&settings));
    assert(settings.label_length==3 && !memcmp(settings.label,"Old",3));
    assert(epd_display_settings_read(&display) && display.target_day==20500 && display.name_length==3);
    assert(writes+erases==before && !memcmp(bytes,legacy_snapshot,sizeof(bytes)));

    uint8_t next[16];memcpy(next,defaults,16);next[11]=8;
    ops=0;assert(inktag_nrf_settings_pins_write(next));unsigned boundaries=ops;
    for(cut_mode=0;cut_mode<3;++cut_mode)for(unsigned at=0;at<boundaries;++at){
        memcpy(bytes,legacy_snapshot,sizeof(bytes));cold();ops=0;cut=(int)at;
        (void)inktag_nrf_settings_pins_write(next);cold();epd_config_read(&config);
        assert(!epd_config_empty(&config));
        assert(!memcmp(inktag_nrf_pins_current(),next,16) || !memcmp(inktag_nrf_pins_current(),defaults,16));
        assert(epd_device_settings_read(&settings) && !memcmp(settings.label,"Old",3));
        assert(epd_display_settings_read(&display) && display.target_day==20500);
        uint16_t length;uint8_t check[3];nrf11_factory_record_t unchanged;
        assert(inktag_nrf_config_store_read(INKTAG_NRF_CONFIG_BOARD,&unchanged,40,&length));
        assert(length==40 && !memcmp(&unchanged,&factory,40));
        assert(inktag_nrf_config_store_read(INKTAG_NRF_CONFIG_HEALTH,check,3,&length) && !memcmp(check,sibling,3));
    }
    memcpy(bytes,legacy_snapshot,sizeof(bytes));cold();silent_drop=true;
    assert(!inktag_nrf_settings_pins_write(next));cold();epd_config_read(&config);
    assert(!memcmp(inktag_nrf_pins_current(),defaults,16));
    assert(inktag_nrf_settings_pins_write(next));cold();user=user_record();
    assert(user.magic==NRF11_USER_MAGIC && user.pin_override_present && !memcmp(user.pins,next,16));
    assert(inktag_nrf_settings_pins_write(NULL));cold();epd_config_read(&config);
    assert(!memcmp(inktag_nrf_pins_current(),defaults,16));

    /* Original ABI length, checksum, reserved bytes and all ordinary fields
     * remain checked before RAM adaptation. None may trigger fallback. */
    for(unsigned variant=0;variant<7;++variant){
        memcpy(bytes,legacy_snapshot,sizeof(bytes));cold();
        uint8_t raw[100];uint16_t length;
        assert(inktag_nrf_config_store_read(INKTAG_NRF_CONFIG_DEVICE_SETTINGS,raw,sizeof(raw),&length) && length==84);
        if(variant==0)raw[0]^=1;
        if(variant==1)raw[78]=1;
        if(variant==2)raw[79]=1;
        if(variant==3)raw[56]=22;
        if(variant==4)raw[80]^=1;
        if(variant==5)++length;
        if(variant==6)raw[5]=255;
        if(variant!=4 && length==84)inktag_write_u32_le(raw+80,epd_record_checksum(raw,80));
        assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_DEVICE_SETTINGS,raw,length));
        before=writes+erases;epd_config_read(&config);
        assert(epd_config_empty(&config) && !epd_device_settings_read(&settings));
        assert(!inktag_nrf_settings_pins_write(next) && writes+erases==before);
    }
    for(unsigned variant=0;variant<5;++variant){
        memcpy(bytes,legacy_snapshot,sizeof(bytes));cold();
        nrf11_factory_record_t bad=factory;
        if(variant==0)bad.magic^=1;
        if(variant==1)bad.active_low=1;
        if(variant==2)bad.config.wakeup_pin=17;
        if(variant==3)bad.config.display_mode=255;
        bad.checksum=epd_record_checksum((const uint8_t*)&bad,36);
        if(variant==4)bad.checksum^=1;
        assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_BOARD,&bad,40));
        before=writes+erases;epd_config_read(&config);
        assert(epd_config_empty(&config) && !inktag_nrf_pins_current() && writes+erases==before);
    }
    memcpy(bytes,legacy_snapshot,sizeof(bytes));cold();before=writes+erases;read_error=true;
    epd_config_read(&config);assert(epd_config_empty(&config) && !inktag_nrf_pins_current());
    assert(writes+erases==before);read_error=false;cold();
    printf("PASS exact N1F1/N1U1 RAM-only OTA adaptation; missing-only defaults; corrupt/IO fail closed; explicit R1 upgrade %u cuts x3, fresh readback, sibling preservation\n",boundaries);
}

static void generation_ordering(void){
    const uint32_t cases[][3]={
        {1,1,2},{1,UINT32_C(0x80000001),2},{UINT32_C(0x80000001),1,2},
        {UINT32_MAX,0,1},{0,UINT32_MAX,0},{0,1,1},{1,0,0}
    };
    for(unsigned c=0;c<sizeof(cases)/sizeof(cases[0]);++c){
        memset(bytes,255,sizeof(bytes));s_initialized=false;
        for(unsigned page=0;page<2;++page){
            assert(write_page_header(page*4096,cases[c][page],true));
            nrf11_user_record_t user={0};epd_device_settings_default(&user.settings);
            user.settings.label_length=1;user.settings.label[0]='A'+page;
            inktag_display_settings_t display={.week_start=page};assert(nrf11_user_encode(&user,&display));
            uint16_t offset=16;assert(append_to_page(page*4096,&offset,INKTAG_NRF_CONFIG_DEVICE_SETTINGS,&user,sizeof(user)));
        }
        unsigned mutations=writes+erases;epd_device_settings_t settings;
        if(cases[c][2]==2){
            assert(!inktag_nrf_config_store_init() && !s_initialized && s_last_error==NRF_ERROR_INVALID_DATA);
            nrf11_user_record_t user;uint16_t length=99;
            assert(inktag_nrf_config_store_read_result(INKTAG_NRF_CONFIG_DEVICE_SETTINGS,&user,sizeof(user),&length)==INKTAG_SETTINGS_JOURNAL_READ_ERROR);
            assert(!epd_device_settings_read(&settings));
        } else {
            assert(inktag_nrf_config_store_init());assert(epd_device_settings_read(&settings));
            assert(settings.label_length==1 && settings.label[0]=='A'+cases[c][2]);
        }
        assert(writes+erases==mutations && !sessions);
        /* A single valid page must remain recoverable after a cut header. */
        bytes[4096]^=1;s_initialized=false;assert(inktag_nrf_config_store_init());
        assert(epd_device_settings_read(&settings) && settings.label[0]=='A');
        assert(writes+erases==mutations && !sessions);
    }
}

int main(void){
    pin_roundtrip();
    legacy_ota_read_upgrade();
    /* Exercise the shipping validator before touching the NOR journal. */
    epd_config_t pins;memcpy(&pins,&nrf11_defaults,sizeof(pins));
    uint8_t pin_wire[16];assert(nrf11_native_pin_wire(&pins,0x380u,pin_wire));
    for(unsigned role=0;role<7;++role){
        for(unsigned pin=0;pin<256;++pin){
            epd_config_t next=pins;((uint8_t*)&next)[role==6?10:role]=pin;
            bool valid=pin<32 && !(UINT32_C(0x0027f007)&(UINT32_C(1)<<(pin&31)));
            for(unsigned other=0;other<7;++other)
                if(other!=role && ((uint8_t*)&pins)[other==6?10:other]==pin)valid=false;
            if(role==6 && pin==255)valid=true;
            assert(nrf11_native_pin_wire(&next,0x380u,pin_wire)==valid);
        }
    }
    generation_ordering();writes=erases=ops=0;
    memset(bytes,255,sizeof(bytes));cold();
    epd_config_t config;epd_config_read(&config);
    assert(!epd_config_empty(&config) && inktag_nrf_pins_current());
    factory_seed();
    nrf11_factory_record_t factory;
    assert(nrf11_factory_read(&factory)==INKTAG_SETTINGS_JOURNAL_READ_FOUND);
    factory.config.mosi_pin=8;factory.config.en_pin=9;
    factory.checksum=epd_record_checksum((const uint8_t*)&factory,36);
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_BOARD,&factory,sizeof(factory)));
    epd_config_read(&config);assert(config.mosi_pin==8 && config.en_pin==9);
    epd_device_settings_t pin_settings;epd_device_settings_default(&pin_settings);
    pin_settings.label_length=3;memcpy(pin_settings.label,"Pin",3);
    assert(epd_device_settings_write(&pin_settings));
    assert(inktag_nrf_config_store_clear_records(INKTAG_SETTINGS_RESET_SETTINGS_V2_MASK));
    epd_config_read(&config);assert(config.mosi_pin==8 && config.en_pin==9);
    assert(epd_device_settings_read(&pin_settings) && !pin_settings.label_length);
    factory.config.busy_pin=12;factory.checksum=epd_record_checksum((const uint8_t*)&factory,36);
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_BOARD,&factory,sizeof(factory)));
    epd_config_read(&config);assert(epd_config_empty(&config));
    config.display_mode=EPD_DISPLAY_STATE_CALENDAR;config.week_start=1;
    assert(epd_config_empty(&config)); /* replay/settings cannot revive bad wiring */
    memset(bytes,255,sizeof(bytes));cold();writes=erases=ops=0;
    epd_screen_config_t screen;
    assert(!epd_screen_config_read(&screen));
    epd_device_settings_t settings;
    inktag_display_settings_t display;
    assert(epd_device_settings_read(&settings));
    assert(epd_display_settings_read(&display));
    assert(writes==0 && erases==0); /* RAM-only unprovisioned base. */
    factory_seed();cold();
    assert(epd_screen_config_read(&screen) && screen.visible_width==122);
    epd_device_settings_default(&settings);
    memcpy(settings.name,"IKT_NRF_ABCD",12);settings.name_length=12;
    memcpy(settings.label,"Desk",4);settings.label_length=4;
    assert(epd_device_settings_write(&settings));
    nrf11_user_record_t old=user_record();
    assert(old.magic==NRF11_USER_MAGIC && old.settings.version==1 && old.display_length==9);
    epd_display_state_t state={EPD_DISPLAY_STATE_CALENDAR,255,1,0};
    display=(inktag_display_settings_t){.week_start=0,.flags=1,.target_day=20000,.name_length=1,.name="0"};
    assert(epd_display_settings_write(&state,&display));
    assert(epd_device_settings_read(&settings) && settings.name_length==12 && settings.label_length==4);
    settings.label[0]='T';assert(epd_device_settings_write(&settings));
    assert(epd_display_settings_read(&display) && display.target_day==20000 && display.name[0]=='0');
    assert(epd_display_state_write(&state));
    uint8_t protection[9]={11,22,33};
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_HEALTH,protection,sizeof(protection)));
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_BOOT_DISPLAY,protection,sizeof(protection)));
    memcpy(saved,bytes,sizeof(bytes));

    old=user_record();
    const unsigned before=writes+erases;
    uint8_t retired[60]={0};memcpy(retired,"DSV1",4);retired[4]=1;
    uint32_t checksum=epd_record_checksum(retired,56);memcpy(retired+56,&checksum,4);
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_DEVICE_SETTINGS,retired,sizeof(retired)));
    unsigned changed=writes+erases;
    assert(changed>before && !epd_device_settings_read(&settings));
    assert(!epd_display_settings_read(&display));
    epd_device_settings_default(&settings);assert(!epd_device_settings_write(&settings));
    assert(writes+erases==changed);restore();
    nrf11_user_record_t malformed=user_record();
    malformed.reserved=1;malformed.checksum=epd_record_checksum((const uint8_t*)&malformed,96);
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_DEVICE_SETTINGS,&malformed,sizeof(malformed)));
    assert(!epd_device_settings_read(&settings));restore();
    malformed=user_record();malformed.display[1]=3;malformed.checksum=epd_record_checksum((const uint8_t*)&malformed,96);
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_DEVICE_SETTINGS,&malformed,sizeof(malformed)));
    assert(!epd_display_settings_read(&display));restore();
    malformed=user_record();malformed.settings.name[18]=1;malformed.checksum=epd_record_checksum((const uint8_t*)&malformed,96);
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_DEVICE_SETTINGS,&malformed,sizeof(malformed)));
    assert(!epd_device_settings_read(&settings));restore();

    uint8_t qr_wire[9+137]={1,0,33,137,0};
    for(unsigned i=0;i<137;++i)qr_wire[9+i]=(uint8_t)i;
    checksum=inktag_crc32_update(UINT32_MAX,qr_wire+9,137)^UINT32_MAX;
    inktag_write_u32_le(qr_wire+5,checksum);
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_DEVICE_INFO_QR,qr_wire,sizeof(qr_wire)));
    epd_device_info_qr_t qr;assert(epd_device_info_qr_read(0,&qr));
    assert(qr.modules==33 && qr.length==137 && !memcmp(qr.data,qr_wire+9,137));
    qr_wire[1]=1;
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_DEVICE_INFO_QR,qr_wire,sizeof(qr_wire)));
    assert(!epd_device_info_qr_read(0,&qr));
    qr_wire[1]=0;qr_wire[5]^=1;
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_DEVICE_INFO_QR,qr_wire,sizeof(qr_wire)));
    assert(!epd_device_info_qr_read(0,&qr));restore();
    assert(epd_time_state_write(1800000000u,480));
    uint32_t timestamp;int16_t timezone;
    assert(epd_time_state_read(&timestamp,&timezone) && timestamp==1800000000u && timezone==480);
    restore();

    inktag_display_settings_t defaults={.week_start=1};
    state=(epd_display_state_t){EPD_DISPLAY_STATE_CALENDAR,255,0,0};
    ops=0;assert(epd_display_settings_cleanup(&state,&defaults,INKTAG_SETTINGS_RESET_SETTINGS_V2_MASK));
    unsigned boundaries=ops;
    assert(epd_device_settings_read(&settings) && !settings.name_length && !settings.label_length);
    assert(epd_display_settings_read(&display) && display.week_start==1 && !display.target_day);
    for(cut_mode=0;cut_mode<3;++cut_mode)for(unsigned at=0;at<boundaries;++at){
        restore();ops=0;cut=(int)at;
        (void)epd_display_settings_cleanup(&state,&defaults,INKTAG_SETTINGS_RESET_SETTINGS_V2_MASK);
        cold();
        assert(epd_device_settings_read(&settings) && epd_display_settings_read(&display));
        bool original=settings.name_length==12 && settings.label_length==4 && display.target_day==20000;
        bool reset=!settings.name_length && !settings.label_length && display.week_start==1 && !display.target_day;
        assert(original || reset);
        uint8_t verify[9];uint16_t length;
        assert(inktag_nrf_config_store_read(INKTAG_NRF_CONFIG_HEALTH,verify,sizeof(verify),&length));
        assert(length==sizeof(protection) && !memcmp(verify,protection,length));
    }
    restore();old=user_record();
    assert(epd_display_settings_read(&display));
    state=(epd_display_state_t){EPD_DISPLAY_STATE_PICTURE,255,display.flags,0};
    assert(epd_display_settings_cleanup(&state,&display,INKTAG_SETTINGS_CLEAR_IMAGES_MASK));
    malformed=user_record();assert(!memcmp(&old,&malformed,sizeof(old)));
    assert(epd_display_state_read(&state) && state.mode==EPD_DISPLAY_STATE_PICTURE && state.argument==255);
    read_error=true;changed=writes+erases;
    assert(!epd_device_settings_read(&settings) && !epd_device_settings_write(&settings));
    assert(writes+erases==changed);
    printf("PASS accepted independent native v1 ABI, sibling preservation, QR, time and %u coupled cut boundaries x3\n",boundaries);
    return 0;
}
