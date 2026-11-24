#include "main.h"

#include <stddef.h>
#include <string.h>

#include "inktag_epd_config.h"
#include "inktag_gui.h"
#include "inktag_page_cycle.h"
#include "inktag_settings_records.h"
#include "inktag_tlsr_settings_backend.h"
#include "inktag_tlsr_health.h"
#include "inktag_feature_store.h"
#include "inktag_pin_config.h"
#include "inktag_tlsr_pin_layout.h"
#define INKTAG_TLSR_PIN_RECORD_PILOT 1
#include "inktag_tlsr_pin_record.h"

#if INKTAG_BUILD_PRO
#define SETTINGS_RECORD_COUNT 6u
#define SETTINGS_ADVERTISING_RECORDS INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_ADVERTISING)
#else
#define SETTINGS_RECORD_COUNT 5u
#define SETTINGS_ADVERTISING_RECORDS 0u
#endif
#define SETTINGS_READ_MAX_PAYLOAD 128u
#define SETTINGS_ALLOWED_RECORDS \
    (SETTINGS_ADVERTISING_RECORDS | INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_TIME) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_SCREEN_PROFILE) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DISPLAY_STATE) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_ACTIVATION) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_FIND_CONFIG) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_INDICATOR) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_NFC_BINDING) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_IMAGE_INDEX))
#define SETTINGS_TIME_VERSION 1u
#define SETTINGS_TIME_SIZE 8u
#define SETTINGS_DISPLAY_VERSION 1u
#define SETTINGS_DISPLAY_SIZE 5u
#define SETTINGS_IDENTITY_VERSION 1u
#define SETTINGS_IDENTITY_SIZE 46u
#define SETTINGS_DEVICE_VERSION INKTAG_TLSR_PIN_SETTINGS_VERSION
#define SETTINGS_DEVICE_DISPLAY_OFFSET 18u
#define SETTINGS_DEVICE_BASE_SIZE (SETTINGS_DEVICE_DISPLAY_OFFSET + \
    INKTAG_DISPLAY_SETTINGS_BASE_SIZE + INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES)
#define SETTINGS_DEVICE_SIZE INKTAG_TLSR_PIN_SETTINGS_BYTES
#if INKTAG_BUILD_PRO
#include "inktag_settings_nfc_constants_pro.inc"
#endif

typedef struct {
    uint8_t time[SETTINGS_TIME_SIZE];
    uint8_t display[SETTINGS_DISPLAY_SIZE];
    uint8_t identity[SETTINGS_IDENTITY_SIZE];
    uint8_t device[SETTINGS_DEVICE_SIZE];
#if INKTAG_BUILD_PRO
#include "inktag_settings_nfc_field_pro.inc"
#else
    uint8_t reserved_input[3];
#endif
    uint8_t indicator[INKTAG_STATUS_WIRE_SIZE];
} settings_payloads_t;

static RAM inktag_settings_t s_settings;
static RAM inktag_settings_t s_persisted;
static RAM inktag_tlsr_settings_backend_t s_backend;
static RAM bool s_store_usable;
static RAM bool s_store_empty;
static RAM bool s_time_record_valid;
static RAM bool s_device_record_valid;
static RAM bool s_screen_record_valid;

#if INKTAG_BUILD_PRO
#include "inktag_settings_aux_validate_pro.inc"
#endif

static void put_u16_le(uint8_t *output, uint16_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

static void put_u32_le(uint8_t *output, uint32_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static uint16_t get_u16_le(const uint8_t *input) {
    return (uint16_t)input[0] | ((uint16_t)input[1] << 8);
}

static uint32_t get_u32_le(const uint8_t *input) {
    return (uint32_t)input[0] | ((uint32_t)input[1] << 8) |
           ((uint32_t)input[2] << 16) | ((uint32_t)input[3] << 24);
}

#if INKTAG_BUILD_PRO
#include "inktag_settings_button_validate_pro.inc"
#endif

static bool device_name_valid(const uint8_t *name, uint8_t length) {
    if (length == 0u) return true;
    return inktag_ble_device_name_valid(name, length);
}

static bool device_label_valid(const uint8_t *label, uint8_t length) {
    uint8_t index = 0u;
    if (length > EPD_DEVICE_LABEL_MAX_BYTES || (length != 0u && !label))
        return false;
    while (index < length) {
        uint8_t byte = label[index++];
        uint8_t continuation;
        if (byte < 0x20u || byte == 0x7Fu) return false;
        if (byte < 0x80u) continue;
        continuation = byte >= 0xC2u && byte <= 0xDFu ? 1u :
                       byte >= 0xE0u && byte <= 0xEFu ? 2u :
                       byte >= 0xF0u && byte <= 0xF4u ? 3u : 0xFFu;
        if (continuation == 0xFFu || continuation > length - index)
            return false;
        if ((byte == 0xE0u && label[index] < 0xA0u) ||
            (byte == 0xEDu && label[index] >= 0xA0u) ||
            (byte == 0xF0u && label[index] < 0x90u) ||
            (byte == 0xF4u && label[index] >= 0x90u)) return false;
        while (continuation-- != 0u) {
            if ((label[index++] & 0xC0u) != 0x80u) return false;
        }
    }
    return true;
}

static bool encode_time(const inktag_settings_t *settings,
                        uint8_t wire[SETTINGS_TIME_SIZE]) {
    if (!settings || settings->timestamp < 1704067200u ||
        settings->timestamp > 4102444800u ||
        settings->timezone_minutes < -840 || settings->timezone_minutes > 840)
        return false;
    wire[0] = SETTINGS_TIME_VERSION;
    wire[1] = 0u;
    put_u16_le(&wire[2], (uint16_t)settings->timezone_minutes);
    put_u32_le(&wire[4], settings->timestamp);
    return true;
}

static bool decode_time(const uint8_t *wire, uint16_t length,
                        inktag_settings_t *settings) {
    uint32_t timestamp;
    int16_t timezone;
    if (!wire || !settings || length != SETTINGS_TIME_SIZE ||
        wire[0] != SETTINGS_TIME_VERSION || wire[1] != 0u) return false;
    timezone = (int16_t)get_u16_le(&wire[2]);
    timestamp = get_u32_le(&wire[4]);
    if (timestamp < 1704067200u || timestamp > 4102444800u ||
        timezone < -840 || timezone > 840) return false;
    settings->timestamp = timestamp;
    settings->timezone_minutes = timezone;
    return true;
}

static bool display_state_valid(uint8_t mode, uint8_t argument, uint16_t flags) {
    return mode <= 6u &&
        (flags & ~EPD_DISPLAY_STATE_FLAG_AUTO_REFRESH_SUSPENDED) == 0u &&
        (mode != 6u || argument == EPD_DISPLAY_STATE_NO_SOURCE);
}

static bool display_preferences_valid(const inktag_display_settings_t *display,
                                      uint32_t interval) {
    return (display->name_length == 0u ||
            GUI_CountdownNameSupported(display->name, display->name_length)) &&
        (interval == 0u || (interval >= 30u && interval <= 86400u));
}

static bool encode_display(const inktag_settings_t *settings,
                           uint8_t wire[SETTINGS_DISPLAY_SIZE]) {
    if (!settings || !display_state_valid(settings->display_mode,
            settings->display_argument, settings->display_internal_flags))
        return false;
    /* ID4 contains only page/content state; ordinary preferences live in ID7. */
    wire[0] = SETTINGS_DISPLAY_VERSION;
    wire[1] = settings->display_mode;
    wire[2] = settings->display_argument;
    put_u16_le(&wire[3], settings->display_internal_flags);
    return true;
}

static bool decode_display(const uint8_t *wire, uint16_t length,
                           inktag_settings_t *settings) {
    uint16_t flags;
    if (!wire || !settings || length != SETTINGS_DISPLAY_SIZE ||
        wire[0] != SETTINGS_DISPLAY_VERSION) return false;
    flags = get_u16_le(&wire[3]);
    if (!display_state_valid(wire[1], wire[2], flags)) return false;
    settings->display_mode = wire[1];
    settings->display_argument = wire[2];
    settings->display_internal_flags = flags;
    return true;
}

static bool encode_identity(const inktag_settings_t *settings,
                            uint8_t wire[SETTINGS_IDENTITY_SIZE]) {
    if (!settings || settings->name_length > sizeof(settings->name) ||
        settings->label_length > sizeof(settings->label) ||
        !device_name_valid(settings->name, settings->name_length) ||
        !device_label_valid(settings->label, settings->label_length))
        return false;
    memset(wire, 0, SETTINGS_IDENTITY_SIZE);
    wire[0] = SETTINGS_IDENTITY_VERSION;
    wire[1] = settings->name_length;
    wire[2] = settings->label_length;
    memcpy(&wire[3], settings->name, sizeof(settings->name));
    memcpy(&wire[3 + sizeof(settings->name)], settings->label,
           sizeof(settings->label));
    return true;
}

static bool decode_identity(const uint8_t *wire, uint16_t length,
                            inktag_settings_t *settings) {
    uint8_t name_length;
    uint8_t label_length;
    if (!wire || !settings || length != SETTINGS_IDENTITY_SIZE ||
        wire[0] != SETTINGS_IDENTITY_VERSION) return false;
    name_length = wire[1];
    label_length = wire[2];
    if (name_length > sizeof(settings->name) ||
        label_length > sizeof(settings->label) ||
        !device_name_valid(&wire[3], name_length) ||
        !device_label_valid(&wire[3 + sizeof(settings->name)], label_length))
        return false;
    settings->name_length = name_length;
    settings->label_length = label_length;
    memcpy(settings->name, &wire[3], sizeof(settings->name));
    memcpy(settings->label, &wire[3 + sizeof(settings->name)],
           sizeof(settings->label));
    return true;
}

/* Structural storage check only. Exact-package/protected-pin/polarity/mux
 * admission belongs to the routing backend before either commit or GPIO use. */
#if INKTAG_BUILD_PRO
#include "inktag_settings_pin_validate_pro.inc"
#else
static bool device_pins_valid(const uint8_t *wire, bool present,
                              const inktag_tlsr_aux_io_config_t *aux) {
    inktag_pin_config_t pins;
    (void)aux; /* Retired custom input bytes cannot reserve a InkTag GPIO. */
    if (!present) {
        for (uint8_t i=0u;i<16u;++i) if (wire[i]) return false;
        return true;
    }
    if (!inktag_pin_config_decode(wire,16u,&pins) ||
        pins.pins[INKTAG_PIN_TXD]!=255u || pins.pins[INKTAG_PIN_RXD]!=255u) return false;
    for (uint8_t i=0u;i<INKTAG_PIN_ROLE_COUNT;++i)
        if (pins.pins[i]!=255u && pins.pins[i]>=32u) return false;
    return true;
}
#endif

#if INKTAG_BUILD_PRO
#include "inktag_settings_custom_encode_pro.inc"
#else
static bool encode_device(const inktag_settings_t *settings,
                          uint8_t wire[SETTINGS_DEVICE_SIZE]) {
    inktag_display_settings_t display;
    if (!settings || !device_pins_valid(settings->pin_override,
        settings->pin_override_present,NULL)) return false;
    display=settings->display_settings;
    if (!display_preferences_valid(&display,settings->slideshow_interval)) return false;
    memset(wire,0,SETTINGS_DEVICE_SIZE);
    wire[0]=SETTINGS_DEVICE_VERSION;
    memcpy(wire+1u,settings->legacy_input,13u);
    memcpy(wire+56u,settings->legacy_input+13u,4u);
    put_u32_le(wire+14u,settings->slideshow_interval);
    wire[39]=settings->pin_override_present?1u:0u;
    memcpy(wire+40u,settings->pin_override,16u);
    return inktag_display_settings_encode(&display,
        wire+SETTINGS_DEVICE_DISPLAY_OFFSET,
        SETTINGS_DEVICE_BASE_SIZE-SETTINGS_DEVICE_DISPLAY_OFFSET)!=0u;
}
#endif

#if INKTAG_BUILD_PRO
#include "inktag_settings_custom_decode_pro.inc"
#else
static bool decode_device(const uint8_t *wire,uint16_t length,
                          inktag_settings_t *settings) {
    inktag_display_settings_t display={0};
    uint32_t interval;
    if (!wire || !settings || length!=SETTINGS_DEVICE_SIZE ||
        wire[0]!=SETTINGS_DEVICE_VERSION) return false;
    for (uint8_t i=INKTAG_TLSR_PIN_SETTINGS_RESERVED_OFFSET;i<64u;++i)
        if (wire[i]) return false;
    if (wire[39]>1u || !device_pins_valid(wire+40u,wire[39]!=0u,NULL)) return false;
    interval=get_u32_le(wire+14u);
    if (wire[SETTINGS_DEVICE_DISPLAY_OFFSET+8u]>INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES ||
        !inktag_display_settings_decode(wire+SETTINGS_DEVICE_DISPLAY_OFFSET,
            INKTAG_DISPLAY_SETTINGS_BASE_SIZE+wire[SETTINGS_DEVICE_DISPLAY_OFFSET+8u],&display) ||
        !display_preferences_valid(&display,interval)) return false;
    settings->display_settings=display;
    settings->slideshow_interval=interval;
    memcpy(settings->legacy_input,wire+1u,13u);
    memcpy(settings->legacy_input+13u,wire+56u,4u);
    settings->pin_override_present=wire[39]!=0u;
    memset(settings->pin_override,0,16u);
    if (settings->pin_override_present) memcpy(settings->pin_override,wire+40u,16u);
    return true;
}
#endif

#if INKTAG_BUILD_PRO
#include "inktag_settings_nfc_codec_pro.inc"
#endif
static void settings_defaults(inktag_board_id_t board,
                              inktag_settings_t *settings) {
    epd_display_state_t display;
    memset(settings, 0, sizeof(*settings));
    settings->timestamp = 1735689600u;
    settings->slideshow_interval = 3600u;
    inktag_display_state_default(&display);
    settings->display_mode = display.mode;
    settings->display_argument = display.argument;
    settings->display_settings.week_start =
        INKTAG_DISPLAY_SETTINGS_DEFAULT_WEEK_START;
    settings->display_settings.flags = (uint8_t)(
        display.reserved & (EPD_DISPLAY_STATE_USER_FLAGS_MASK |
                            EPD_DISPLAY_STATE_PRO_FLAGS_MASK));
    settings->display_internal_flags =
        display.reserved & EPD_DISPLAY_STATE_FLAG_AUTO_REFRESH_SUSPENDED;
#if INKTAG_BUILD_PRO
    settings->nfc_action = INKTAG_INPUT_ACTION_DISABLED;
#endif
    inktag_status_config_default(&settings->led);
#if INKTAG_BUILD_PRO
    inktag_button_bindings_default(&settings->button);
    inktag_tlsr_aux_io_defaults(&settings->aux);
#else
    /* Preserve the historical on-wire default as inert bytes only. */
    static const uint8_t legacy_default[17]={
        INKTAG_INPUT_ACTION_NEXT_PAGE,0u,INKTAG_INPUT_ACTION_PREVIOUS_PAGE,0u,
        INKTAG_INPUT_ACTION_TOGGLE_CLEAR_PREVIOUS,0u,255u,0u,0u,255u,255u,0xd0u,7u,
        255u,0u,255u,0u};
    memcpy(settings->legacy_input,legacy_default,sizeof(legacy_default));
#endif
    if (!inktag_tlsr_board_profile(board)) return;
    /* The same PCB may carry different panels. Reset restores the immutable
     * Full configuration, never a guessed compiled-board screen. A corrupt
     * envelope leaves the zero screen invalid and prevents a default repair. */
    (void)inktag_tlsr_factory_screen(settings->screen_config);
    /* Unknown defaults cannot be repaired by an unrelated Settings save. */
#if INKTAG_BUILD_PRO
    if (!inktag_tlsr_factory_aux(&settings->aux)) settings->aux.buttons[0].flags=255u;
#endif
}

static bool build_payloads(const inktag_settings_t *settings,
                           settings_payloads_t *payloads) {
    if (!settings || !payloads
#if INKTAG_BUILD_PRO
        || !aux_structural_valid(&settings->aux)
#endif
       )
        return false;
    return encode_time(settings, payloads->time) &&
        encode_display(settings, payloads->display) &&
        encode_identity(settings, payloads->identity) &&
        encode_device(settings, payloads->device) &&
#if INKTAG_BUILD_PRO
        encode_nfc(settings, payloads->nfc) &&
#endif
        inktag_status_config_encode(&settings->led, payloads->indicator);
}

static void load_records(void) {
    uint8_t wire[SETTINGS_READ_MAX_PAYLOAD];
    uint16_t length;
    epd_screen_record_t screen_record;
    epd_screen_config_t screen;
    inktag_status_config_t indicator;
    if (inktag_settings_journal_read(&s_backend.journal,
            INKTAG_SETTINGS_RECORD_TIME, wire, sizeof(wire), &length) &&
        decode_time(wire, length, &s_settings))
        s_time_record_valid = true;
    inktag_settings_journal_read_result_t screen_result =
        inktag_settings_journal_read_result(&s_backend.journal,
            INKTAG_SETTINGS_RECORD_SCREEN_PROFILE,
            &screen_record, sizeof(screen_record), &length);
    s_screen_record_valid = screen_result == INKTAG_SETTINGS_JOURNAL_READ_MISSING;
    if (screen_result == INKTAG_SETTINGS_JOURNAL_READ_FOUND &&
        length == sizeof(screen_record) && epd_screen_record_decode_exact(&screen_record, &screen) == EPD_SCREEN_STATUS_OK) {
        (void)epd_screen_config_encode(&screen, s_settings.screen_config);
        s_screen_record_valid = true;
    }
    if (inktag_settings_journal_read(&s_backend.journal,
            INKTAG_SETTINGS_RECORD_DISPLAY_STATE,
            wire, sizeof(wire), &length))
        (void)decode_display(wire, length, &s_settings);
    if (inktag_settings_journal_read(&s_backend.journal,
            INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY,
            wire, sizeof(wire), &length))
        (void)decode_identity(wire, length, &s_settings);
    inktag_settings_journal_read_result_t device_result =
        inktag_settings_journal_read_result(&s_backend.journal,
            INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS,
            wire, sizeof(wire), &length);
    s_device_record_valid = device_result == INKTAG_SETTINGS_JOURNAL_READ_MISSING ||
        (device_result == INKTAG_SETTINGS_JOURNAL_READ_FOUND &&
         decode_device(wire, length, &s_settings));
#if INKTAG_BUILD_PRO
#include "inktag_settings_nfc_load_pro.inc"
#endif
    if (inktag_settings_journal_read(&s_backend.journal,
            INKTAG_SETTINGS_RECORD_INDICATOR, wire, sizeof(wire), &length) &&
        inktag_status_config_decode(&indicator, wire, length)) {
        s_settings.led = indicator;
    }
}

void inktag_settings_init(inktag_board_id_t board) {
    settings_defaults(board, &s_settings);
    s_persisted = s_settings;
    s_store_empty = false;
    s_time_record_valid = false;
    s_device_record_valid = false;
    s_screen_record_valid = false;
    s_store_usable = inktag_tlsr_settings_backend_configure(
        &s_backend, INKTAG_SETTINGS_SECTOR, SETTINGS_ALLOWED_RECORDS);
#if INKTAG_BUILD_PRO
    if (!aux_structural_valid(&s_settings.aux)) s_store_usable=false;
#endif
    if (!s_store_usable) return;
    if (inktag_settings_journal_init(
            &s_backend.journal, &s_backend.io, false)) {
        load_records();
        s_persisted = s_settings;
    } else if (inktag_tlsr_settings_backend_pair_erased(&s_backend)) {
        /* Keep erased first boot write-free. The first changed record lazily
         * creates the common journal after the ADC/power path is stable. */
        s_store_empty = true;
        s_device_record_valid = true;
        s_screen_record_valid = true;
    } else {
        s_store_usable = false;
    }
}

inktag_settings_t *inktag_settings(void) { return &s_settings; }

bool inktag_settings_time_valid(void) { return s_time_record_valid; }
bool inktag_settings_device_valid(void) { return s_store_usable && s_device_record_valid; }
bool inktag_settings_screen_valid(void) { return s_store_usable && s_screen_record_valid; }
const inktag_settings_journal_io_t *inktag_settings_gpio_readonly_io(void) {
    return inktag_settings_device_valid()?&s_backend.io:NULL;
}

static bool protected_record_id(uint16_t record_id) {
    return
#if INKTAG_BUILD_PRO
           record_id == INKTAG_SETTINGS_RECORD_ADVERTISING ||
#endif
           record_id == INKTAG_SETTINGS_RECORD_ACTIVATION ||
           record_id == INKTAG_SETTINGS_RECORD_FIND_CONFIG ||
           record_id == INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED;
}

static bool settings_ensure_journal(void) {
    if (!s_store_usable) return false;
    if (!s_store_empty) return true;
    if (!inktag_settings_journal_init(
            &s_backend.journal, &s_backend.io, true)) return false;
    s_store_empty = false;
    return true;
}

static bool settings_device_commit(const inktag_settings_t *candidate) {
    uint8_t wire[SETTINGS_DEVICE_SIZE], observed[SETTINGS_DEVICE_SIZE];
    uint16_t length;
    bool ok;
    /* Refuse unsaved ordinary preferences instead of bundling unrelated
     * changes into a route update. Other Settings/Find/password records are
     * copied by the existing whole-generation atomic operation, not decoded. */
    if (!s_store_usable || !s_device_record_valid ||
        !encode_device(&s_settings, wire) ||
        !encode_device(&s_persisted, observed) ||
        memcmp(wire, observed, sizeof(wire)) != 0 ||
        !encode_device(candidate,wire) ||
        !inktag_tlsr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true) ||
        !settings_ensure_journal()) return false;
    ok = inktag_settings_journal_put_record_atomic(&s_backend.journal,
        INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS, wire, sizeof(wire));
    /* Read the complete current generation even on false-after-commit.
     * No rollback write, default repair or GPIO change on uncertain media. */
    if (inktag_settings_journal_readonly_result(&s_backend.io,
            INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS,
            observed, sizeof(observed), &length) != INKTAG_SETTINGS_JOURNAL_READ_FOUND ||
        !decode_device(observed, length, &s_settings) ||
        !decode_device(observed, length, &s_persisted)) {
        s_device_record_valid = false;
        return false;
    }
    return ok && memcmp(wire,observed,sizeof(wire))==0;
}

bool inktag_settings_pin_commit(const uint8_t effective[16], bool override) {
    inktag_settings_t candidate=s_settings;
#if INKTAG_BUILD_PRO
    if (!device_pins_valid(effective,true,&candidate.aux)) return false;
#else
    if (!device_pins_valid(effective,true,NULL)) return false;
#endif
    candidate.pin_override_present=override;
    memset(candidate.pin_override,0,16u);
    if (override) memcpy(candidate.pin_override,effective,16u);
    return settings_device_commit(&candidate);
}
#if INKTAG_BUILD_PRO
#include "inktag_settings_aux_commit_pro.inc"
#endif

bool inktag_settings_screen_commit(const epd_screen_config_t *screen) {
    epd_screen_record_t requested, observed;
    epd_screen_config_t actual;
    uint16_t length;
    if (!inktag_settings_screen_valid() || !s_device_record_valid ||
        epd_screen_record_encode(screen,&requested)!=EPD_SCREEN_STATUS_OK ||
        requested.bytes[6]!=screen->panel_recipe ||
        !inktag_tlsr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,true) ||
        !settings_ensure_journal()) return false;
    bool ok=inktag_settings_journal_put_record_atomic(&s_backend.journal,
        INKTAG_SETTINGS_RECORD_SCREEN_PROFILE,&requested,sizeof(requested));
    /* A false-after-commit is not a rollback. Observe without compensation;
     * callers park/invalidate hardware and never claim old-state preservation. */
    if (inktag_settings_journal_readonly_result(&s_backend.io,
            INKTAG_SETTINGS_RECORD_SCREEN_PROFILE,&observed,sizeof(observed),&length)!=INKTAG_SETTINGS_JOURNAL_READ_FOUND ||
        length!=sizeof(observed) || epd_screen_record_decode_exact(&observed,&actual)!=EPD_SCREEN_STATUS_OK) {
        s_screen_record_valid=false;
        return false;
    }
    (void)epd_screen_config_encode(&actual,s_settings.screen_config);
    memcpy(s_persisted.screen_config,s_settings.screen_config,EPD_SCREEN_WIRE_SIZE);
    return ok && memcmp(&requested,&observed,sizeof(observed))==0;
}

#if INKTAG_BUILD_PRO
#include "inktag_settings_nfc_store_pro.inc"
#endif
inktag_settings_record_read_result_t inktag_settings_protected_read(
    uint16_t record_id, void *data, uint16_t capacity, uint16_t *length) {
    if (!protected_record_id(record_id) || data == NULL || length == NULL ||
        capacity == 0u || !s_store_usable) {
        return INKTAG_SETTINGS_RECORD_READ_ERROR;
    }
    *length = 0u;
    if (s_store_empty) return INKTAG_SETTINGS_RECORD_READ_MISSING;
    switch (inktag_settings_journal_read_result(
            &s_backend.journal, record_id, data, capacity, length)) {
        case INKTAG_SETTINGS_JOURNAL_READ_FOUND:
            return INKTAG_SETTINGS_RECORD_READ_FOUND;
        case INKTAG_SETTINGS_JOURNAL_READ_MISSING:
            return INKTAG_SETTINGS_RECORD_READ_MISSING;
        default:
            return INKTAG_SETTINGS_RECORD_READ_ERROR;
    }
}

bool inktag_settings_protected_write(
    uint16_t record_id, const void *data, uint16_t length) {
    return protected_record_id(record_id) && data != NULL && length != 0u &&
           settings_ensure_journal() &&
           inktag_settings_journal_write(
               &s_backend.journal, record_id, data, length);
}

bool inktag_settings_protected_delete(uint16_t record_id) {
    if (!protected_record_id(record_id) || !s_store_usable) return false;
    if (s_store_empty) return true;
    return inktag_settings_journal_delete(&s_backend.journal, record_id);
}

bool inktag_settings_find_public_provision_safe(void) {
    return s_store_usable &&
        (s_store_empty || (s_backend.journal.initialized &&
                           s_backend.journal.tail_clean));
}

bool inktag_settings_find_public_storage_safe(void) {
    /* A committed clean generation is authoritative even if a power cut
     * prevented erasing the old page. Never write Flash during a Find read. */
    return s_store_usable && !s_store_empty &&
        s_backend.journal.initialized && s_backend.journal.tail_clean;
}

bool inktag_settings_find_public_replace(
    const void *record, uint16_t length) {
    return record != NULL && length != 0u && s_store_usable &&
        !s_store_empty && s_backend.journal.initialized &&
        s_backend.journal.tail_clean &&
        inktag_tlsr_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true) &&
        inktag_settings_journal_replace_record_atomic(
            &s_backend.journal,
            INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED,
            record, length);
}

#if INKTAG_BUILD_PRO
bool inktag_settings_find_public_unregister(void) {
    return inktag_settings_find_public_storage_safe() &&
        inktag_settings_journal_clear_find_owner(&s_backend.journal);
}
#endif

bool inktag_settings_device_info_qr_available(void) {
    return s_store_usable;
}

inktag_device_password_store_result_t inktag_settings_password_load(
    uint8_t *scratch, inktag_device_password_config_t *config,
    uint8_t *modules, uint16_t *offset, uint16_t *length) {
    memset(config, 0, sizeof(*config));
    *modules = 0u;
    *offset = *length = 0u;
    if (!s_store_usable) return INKTAG_PASSWORD_STORE_ERROR;
    /* s_store_empty is established by exact erased-pair inspection at boot,
     * not by a failed record read. Do not create a journal during a read. */
    if (s_store_empty) {
        config->valid = true;
        return INKTAG_PASSWORD_STORE_MISSING;
    }
    return inktag_device_password_store_load(
        &s_backend.journal, scratch, config, modules, offset, length);
}

bool inktag_settings_password_commit(uint8_t *scratch, uint16_t length,
    inktag_device_password_config_t *actual) {
    if (!settings_ensure_journal()) {
        uint8_t modules;
        uint16_t offset, stored_length;
        (void)inktag_settings_password_load(
            scratch, actual, &modules, &offset, &stored_length);
        return false;
    }
    return inktag_device_password_store_commit(
        &s_backend.journal, scratch, length, actual);
}

bool inktag_settings_apply_default_name(const uint8_t *name, uint8_t length) {
    if (!name || length == 0u || length > sizeof(s_settings.name)) return false;
    if (s_settings.name_length != 0u) return false;
    memset(s_settings.name, 0, sizeof(s_settings.name));
    memcpy(s_settings.name, name, length);
    s_settings.name_length = length;
    return true;
}

static const uint8_t *payload_at(const settings_payloads_t *payloads,
                                 uint8_t index, uint16_t *id,
                                 uint16_t *length) {
    if (index == 0u) {
        *id = INKTAG_SETTINGS_RECORD_TIME;
        *length = sizeof(payloads->time);
        return payloads->time;
    }
    if (index == 1u) {
        /* Current ordinary preferences and pin extension share one record. */
        *id = INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS;
        *length = sizeof(payloads->device);
        return payloads->device;
    }
    if (index == 2u) {
        *id = INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY;
        *length = sizeof(payloads->identity);
        return payloads->identity;
    }
    if (index == 3u) {
        *id = INKTAG_SETTINGS_RECORD_DISPLAY_STATE;
        *length = sizeof(payloads->display);
        return payloads->display;
    }
#if INKTAG_BUILD_PRO
#include "inktag_settings_nfc_payload_pro.inc"
#endif
    *id = INKTAG_SETTINGS_RECORD_INDICATOR;
    *length = sizeof(payloads->indicator);
    return payloads->indicator;
}

static bool settings_save(bool include_time) {
    settings_payloads_t current;
    settings_payloads_t persisted;
    uint8_t changed = 0u;
    uint8_t index;
    bool time_written = false;
    uint32_t previous_timestamp = s_persisted.timestamp;
    int16_t previous_timezone = s_persisted.timezone_minutes;
    /* Screen persistence has its own atomic ID3 adapter. Unrelated LED/name
     * saves neither repair an invalid screen nor depend on its decoder. */
    if (!s_store_usable || !s_device_record_valid ||
        memcmp(s_settings.screen_config,s_persisted.screen_config,EPD_SCREEN_WIRE_SIZE)!=0 ||
        !build_payloads(&s_settings, &current) ||
        !build_payloads(&s_persisted, &persisted)) return false;
    if (!include_time) memcpy(current.time, persisted.time, sizeof(current.time));
    for (index = 0u; index < SETTINGS_RECORD_COUNT; ++index) {
        uint16_t current_id, current_length, persisted_id, persisted_length;
        const uint8_t *current_payload = payload_at(
            &current, index, &current_id, &current_length);
        const uint8_t *persisted_payload = payload_at(
            &persisted, index, &persisted_id, &persisted_length);
#if INKTAG_BUILD_PRO
#include "inktag_settings_nfc_save_guard_pro.inc"
#endif
        if ((include_time && !s_time_record_valid && index == 0u) ||
            current_id != persisted_id || current_length != persisted_length ||
            memcmp(current_payload, persisted_payload, current_length) != 0) {
            changed++;
        }
    }
    if (changed == 0u) return true;
    if (!settings_ensure_journal()) return false;
    for (index = 0u; index < SETTINGS_RECORD_COUNT; ++index) {
        uint16_t id, length;
        const uint8_t *payload = payload_at(
            &current, index, &id, &length);
        const uint8_t *old_payload = payload_at(
            &persisted, index, &id, &length);
        if ((include_time && !s_time_record_valid && index == 0u) ||
            memcmp(payload, old_payload, length) != 0) {
            if (!inktag_settings_journal_write(
                    &s_backend.journal, id, payload, length)) return false;
            if (id == INKTAG_SETTINGS_RECORD_TIME) time_written = true;
        }
    }
    s_persisted = s_settings;
    if (!include_time) {
        s_persisted.timestamp = previous_timestamp;
        s_persisted.timezone_minutes = previous_timezone;
    }
    /* Record presence is not cold-boot clock trust. SET_TIME separately marks
     * the running App trusted only after verified persistence succeeds. */
    if (time_written) s_time_record_valid = true;
    return true;
}

bool inktag_settings_save(void) { return settings_save(false); }
bool inktag_settings_save_time(void) { return settings_save(true); }

bool inktag_settings_time_checkpoint_ready(void) {
    return s_store_usable && s_device_record_valid && (s_store_empty ||
        (s_backend.journal.initialized && s_backend.journal.tail_clean));
}

bool inktag_settings_checkpoint_time(void) {
    uint8_t wire[SETTINGS_TIME_SIZE];
    if (!inktag_settings_time_checkpoint_ready() ||
        !encode_time(&s_settings, wire) || !settings_ensure_journal() ||
        !inktag_settings_journal_write(&s_backend.journal,
            INKTAG_SETTINGS_RECORD_TIME, wire, sizeof(wire))) return false;
    s_persisted.timestamp = s_settings.timestamp;
    s_persisted.timezone_minutes = s_settings.timezone_minutes;
    s_time_record_valid = true;
    return true;
}

bool inktag_settings_clear(inktag_board_id_t board,
                           inktag_settings_clear_action_t action) {
    if (action != INKTAG_SETTINGS_CLEAR_RESET_SETTINGS &&
        action != INKTAG_SETTINGS_CLEAR_USER_DATA) return false;
    inktag_settings_t previous = s_settings;
    bool previous_time_valid = s_time_record_valid;
    inktag_settings_t defaults;
    if (!s_store_usable) return false;
    /* Only two fixed scopes remain. Map their runtime fields together rather
     * than testing the same selected scope for every individual record.
     * Password/QR stays outside this ordinary phase; OFF commits last. */
    bool reset_settings = action == INKTAG_SETTINGS_CLEAR_RESET_SETTINGS;
    settings_defaults(board, &defaults);
#if INKTAG_BUILD_PRO
    if (!aux_structural_valid(&defaults.aux)) return false;
#endif
    if (reset_settings) {
        epd_screen_config_t profile;
        if (epd_screen_config_decode(defaults.screen_config, EPD_SCREEN_WIRE_SIZE,
                &profile) != EPD_SCREEN_STATUS_OK ||
            !inktag_image_store_profile_allowed(&profile)) return false;
    }
    if (reset_settings) {
        s_settings.timestamp = defaults.timestamp;
        s_settings.timezone_minutes = defaults.timezone_minutes;
        memcpy(s_settings.screen_config, defaults.screen_config,
               sizeof(s_settings.screen_config));
        s_settings.name_length = defaults.name_length;
        s_settings.label_length = defaults.label_length;
        memcpy(s_settings.name, defaults.name, sizeof(s_settings.name));
        memcpy(s_settings.label, defaults.label, sizeof(s_settings.label));
#if INKTAG_BUILD_PRO
        s_settings.button = defaults.button;
        s_settings.aux = defaults.aux;
#endif
        s_settings.pin_override_present = false;
        memset(s_settings.pin_override, 0, sizeof(s_settings.pin_override));
#if INKTAG_BUILD_PRO
#include "inktag_settings_nfc_reset_pro.inc"
#endif
        s_settings.led = defaults.led;
        s_settings.display_settings = defaults.display_settings;
        s_settings.slideshow_interval = defaults.slideshow_interval;
    } else {
        s_settings.display_mode = defaults.display_mode;
        s_settings.display_argument = defaults.display_argument;
        s_settings.display_internal_flags = defaults.display_internal_flags;
    }
    if (s_store_empty || (reset_settings ?
            inktag_settings_journal_clear_ordinary_settings_v2(&s_backend.journal) :
            inktag_settings_journal_clear_images(&s_backend.journal))) {
        if (reset_settings) s_time_record_valid = false;
        if (reset_settings) s_device_record_valid = true;
        if (reset_settings) s_screen_record_valid = true;
        s_persisted = s_settings;
        if (reset_settings && !inktag_image_store_init()) return false;
        return true;
    }
    s_settings = previous;
    s_time_record_valid = previous_time_valid;
    return false;
}

bool inktag_settings_password_clear(void) {
    return s_store_usable && (s_store_empty ||
        (inktag_settings_journal_reset_settings_v2(&s_backend.journal) &&
         inktag_settings_journal_scrub_inactive(&s_backend.journal)));
}
