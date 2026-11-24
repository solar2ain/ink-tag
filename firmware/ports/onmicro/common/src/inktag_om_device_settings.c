#include "inktag_om_device_settings.h"

#include <stddef.h>
#include <string.h>

#include "inktag_gui.h"
#include "inktag_board.h"
#if defined(INKTAG_BOARD_STATUS_PWM_SHARES_AUX_TIMER)
#include "pm.h"
#endif
#include "inktag_ble_identity.h"
#include "inktag_status.h"
#include "nvds.h"
#include "om.h"
#include "om_driver.h"
#include "inktag_epd_driver.h"
#include "inktag_om_nvds.h"
#include "inktag_protocol_core.h"
#include "inktag_om_pins.h"
#include "inktag_om_app.h"
#include "inktag_om_board_identity.h"
#include "inktag_om_external_flash_board.h"

#define INKTAG_SETTINGS_NVDS_TAG ((nvds_tag_id_t)(NVDS_TAG_USER_BEGIN + 4))
#define INKTAG_NAME_NVDS_TAG ((nvds_tag_id_t)(NVDS_TAG_USER_BEGIN + 5))
#define INKTAG_DEVICE_LABEL_NVDS_TAG ((nvds_tag_id_t)(NVDS_TAG_USER_BEGIN + 9))
#define INKTAG_DISPLAY_SETTINGS_NVDS_TAG ((nvds_tag_id_t)(NVDS_TAG_USER_BEGIN + 16))
#define INKTAG_SETTINGS_MAGIC INKTAG_OM_PREF_MAGIC
#define INKTAG_SETTINGS_VERSION INKTAG_OM_PREF_VERSION
#define INKTAG_DEVICE_LABEL_MAGIC 0x314C424Cu

_Static_assert(INKTAG_OM_DEVICE_INFO_QR_SLOT_COUNT ==
                   INKTAG_DEVICE_INFO_QR_SLOT_COUNT,
               "OM QR slot count must match the shared V2 contract");
_Static_assert(INKTAG_OM_DEVICE_INFO_QR_MAX_MODULES ==
                   INKTAG_DEVICE_INFO_QR_MAX_MODULES,
               "OM QR module bound must match the shared V2 contract");
_Static_assert(INKTAG_OM_DEVICE_INFO_QR_MAX_BYTES ==
                   INKTAG_DEVICE_INFO_QR_MAX_BYTES,
               "OM QR payload bound must match the shared V2 contract");

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    inktag_button_bindings_t button;
    uint8_t aux[INKTAG_AUX_IO_WIRE_SIZE];
    inktag_status_config_t led;
    uint8_t pin_override;
    uint8_t pins[16];
    uint8_t checksum;
} inktag_om_settings_record_t;
_Static_assert(sizeof(inktag_om_settings_record_t)==INKTAG_OM_PREF_SIZE,"OM preferences wire size");
static bool s_preferences_valid, s_pin_override;
static uint8_t s_pin_wire[16];



typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t length;
    uint8_t label[INKTAG_OM_DEVICE_LABEL_MAX_BYTES];
    uint8_t checksum;
} inktag_om_device_label_record_t;

static inktag_button_bindings_t s_button_bindings;
/* The maintained OM profiles register no fixed stock input GPIO tuple.
 * Do not reinterpret a saved custom pin as board hardware. */
static const inktag_aux_io_t s_aux = {
    .buttons={{255u,0u},{255u,0u},{255u,0u}},
    .reserved={0},.reserved_word=0,
};
/* Preserve old customization bytes through unrelated base preference saves. */
static uint8_t s_retained_custom[sizeof(inktag_button_bindings_t)+INKTAG_AUX_IO_WIRE_SIZE];
static inktag_status_config_t s_led;
static inktag_display_settings_t s_display_settings;

static uint8_t record_checksum(const void *record, uint16_t length) {
    const uint8_t *bytes = (const uint8_t *)record;
    uint8_t value = 0;
    for (uint16_t i = 0; i + 1u < length; i++) value ^= bytes[i];
    return value;
}

static void defaults(void) {
    s_pin_override=false;memset(s_pin_wire,0,sizeof(s_pin_wire));
    inktag_button_bindings_default(&s_button_bindings);
    inktag_status_config_default(&s_led);
}

static bool save_settings(void) {
    if(!s_preferences_valid)return false;
    inktag_om_settings_record_t record;
    memset(&record, 0, sizeof(record));
    record.magic = INKTAG_SETTINGS_MAGIC;
    record.version = INKTAG_SETTINGS_VERSION;
    memcpy(&record.button,s_retained_custom,sizeof(s_retained_custom));
    record.led = s_led;
    record.pin_override=s_pin_override?1u:0u;
    memcpy(record.pins,s_pin_wire,sizeof(record.pins));
    record.checksum = record_checksum(&record, sizeof(record));
    inktag_om_settings_record_t actual;
    nvds_tag_len_t n=sizeof(actual);
    if(inktag_om_nvds_put(INKTAG_SETTINGS_NVDS_TAG,sizeof(record),&record)!=NVDS_OK ||
       nvds_get(INKTAG_SETTINGS_NVDS_TAG,&n,&actual)!=NVDS_OK || n!=sizeof(actual) ||
       memcmp(&record,&actual,n)) {
        s_preferences_valid=false;inktag_om_pins_invalidate();return false;
    }
    return true;
}

static void load_settings(void) {
    inktag_om_settings_record_t record;
    inktag_status_config_t loaded_led;
    nvds_tag_len_t length = sizeof(record);
    defaults();
    memcpy(s_retained_custom,&s_button_bindings,sizeof(s_button_bindings));
    (void)inktag_aux_io_encode(&s_aux,s_retained_custom+sizeof(s_button_bindings),INKTAG_AUX_IO_WIRE_SIZE);
    s_preferences_valid=false;
    memset(&record, 0, sizeof(record));
    uint8_t result=nvds_get(INKTAG_SETTINGS_NVDS_TAG,&length,&record);
    /* Confirmed absence uses only trusted factory defaults in RAM. A read
     * error or an old/invalid record never enables default configuration. */
    if(result==NVDS_TAG_NOT_DEFINED && inktag_om_pins_available()) {
        s_preferences_valid=true;return;
    }
    inktag_pin_config_t pins;bool present=false;
    if(result!=NVDS_OK || !inktag_om_pin_pref_decode((const uint8_t *)&record,length,
        &present,&pins))return;
    const uint8_t *raw=(const uint8_t *)&record;
    memcpy(&loaded_led,raw+23u,sizeof(loaded_led));
    if (inktag_status_config_valid(&loaded_led)) {
        s_preferences_valid=true;
        s_pin_override=present;
        if(present)memcpy(s_pin_wire,raw+length-17u,sizeof(s_pin_wire));
        memcpy(s_retained_custom,&record.button,sizeof(s_retained_custom));
        s_led = loaded_led;
    }
}

static void load_display_settings(uint8_t week_start, uint8_t flags) {
    uint8_t wire[INKTAG_DISPLAY_SETTINGS_BASE_SIZE +
                 INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES];
    inktag_display_settings_t decoded;
    nvds_tag_len_t length = sizeof(wire);
    memset(&s_display_settings, 0, sizeof(s_display_settings));
    s_display_settings.week_start = week_start <= 1u ? week_start : 0u;
    s_display_settings.flags = flags & INKTAG_DISPLAY_SETTINGS_KNOWN_FLAGS;
    if (nvds_get(INKTAG_DISPLAY_SETTINGS_NVDS_TAG, &length, wire) != NVDS_OK ||
        !inktag_display_settings_decode(wire, length, &decoded) ||
        (decoded.name_length > 0u &&
         !GUI_CountdownNameSupported(decoded.name, decoded.name_length))) return;
    s_display_settings = decoded;
}


static bool utf8_text_valid(const uint8_t *text, uint8_t length,
                            uint8_t max_length, bool allow_empty) {
    if (!text || length > max_length || (!allow_empty && length == 0u)) return false;
    uint8_t i = 0u;
    while (i < length) {
        uint8_t byte = text[i++];
        if (byte < 0x20u || byte == 0x7Fu) return false;
        if (byte < 0x80u) continue;
        uint8_t continuation = byte >= 0xC2u && byte <= 0xDFu ? 1u :
                               byte >= 0xE0u && byte <= 0xEFu ? 2u :
                               byte >= 0xF0u && byte <= 0xF4u ? 3u : 0xFFu;
        if (continuation == 0xFFu || continuation > length - i) return false;
        uint8_t first_continuation = text[i];
        if ((byte == 0xE0u && first_continuation < 0xA0u) ||
            (byte == 0xEDu && first_continuation >= 0xA0u) ||
            (byte == 0xF0u && first_continuation < 0x90u) ||
            (byte == 0xF4u && first_continuation >= 0x90u)) return false;
        while (continuation-- > 0u) {
            if ((text[i++] & 0xC0u) != 0x80u) return false;
        }
    }
    return true;
}



bool inktag_om_device_name_read(char name[INKTAG_OM_DEVICE_NAME_MAX_BYTES + 1u]) {
    if (name) name[0] = '\0';
    return false;
}

bool inktag_om_device_name_write(const uint8_t *name, uint8_t length,
                                 const char **error) {
    (void)name; (void)length;
    if (error) *error = "unsupported";
    return false;
}

bool inktag_om_device_label_read(char label[INKTAG_OM_DEVICE_LABEL_MAX_BYTES + 1u]) {
    inktag_om_device_label_record_t record;
    nvds_tag_len_t length = sizeof(record);
    if (!label) return false;
    label[0] = '\0';
    if (nvds_get(INKTAG_DEVICE_LABEL_NVDS_TAG, &length, &record) != NVDS_OK ||
        length != sizeof(record) || record.magic != INKTAG_DEVICE_LABEL_MAGIC ||
        record.version != 1u ||
        !utf8_text_valid(record.label, record.length,
                         INKTAG_OM_DEVICE_LABEL_MAX_BYTES, true) ||
        record.checksum != record_checksum(&record, sizeof(record))) return false;
    memcpy(label, record.label, record.length);
    label[record.length] = '\0';
    return true;
}

bool inktag_om_device_label_write(const uint8_t *label, uint8_t length,
                                  const char **error) {
    if (!inktag_device_label_write_valid(label, length)) {
        if (error) *error = "name";
        return false;
    }
    inktag_om_device_label_record_t record;
    memset(&record, 0, sizeof(record));
    record.magic = INKTAG_DEVICE_LABEL_MAGIC;
    record.version = 1u;
    record.length = length;
    memcpy(record.label, label, length);
    record.checksum = record_checksum(&record, sizeof(record));
    if (inktag_om_nvds_put(INKTAG_DEVICE_LABEL_NVDS_TAG,
                           sizeof(record), &record) != NVDS_OK) {
        if (error) *error = "flash";
        return false;
    }
    if (error) *error = NULL;
    return true;
}


void inktag_om_device_settings_init(const epd_config_t *epd_config,
                                    uint8_t display_flags) {
    load_settings();
    load_display_settings(epd_config ? epd_config->week_start : 0u,
                          display_flags);

}

void inktag_om_device_settings_get_display(
    inktag_display_settings_t *settings) {
    if (settings) *settings = s_display_settings;
}

bool inktag_om_device_settings_set_display(
    const inktag_display_settings_t *settings, const char **error) {
    inktag_display_settings_t retained;
    if (!settings || (settings->flags & INKTAG_DISPLAY_SETTINGS_FLAG_BRAND_HIDDEN)) {
        if (error) *error = "unsupported";
        return false;
    }
    retained = *settings;
    /* Preserve an old Pro preference as opaque data on an ordinary base save. */
    retained.flags |= s_display_settings.flags & INKTAG_DISPLAY_SETTINGS_FLAG_BRAND_HIDDEN;
    settings = &retained;

    uint8_t wire[INKTAG_DISPLAY_SETTINGS_BASE_SIZE +
                 INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES];
    size_t length;
    if (!settings ||
        (settings->name_length > 0u &&
         !GUI_CountdownNameSupported(settings->name, settings->name_length)) ||
        (length = inktag_display_settings_encode(settings, wire,
                                                  sizeof(wire))) == 0u) {
        if (error) *error = "font";
        return false;
    }
    if (inktag_om_nvds_put(INKTAG_DISPLAY_SETTINGS_NVDS_TAG,
                           (nvds_tag_len_t)length, wire) != NVDS_OK) {
        if (error) *error = "flash";
        return false;
    }
    s_display_settings = *settings;
    if (error) *error = NULL;
    return true;
}

void inktag_om_device_settings_process(void) {
    /* No custom GPIO or gesture timer backend in the base implementation. */
}

void inktag_om_device_settings_get_aux(inktag_om_aux_io_config_t *config) {
    if(config) {
        config->button_gpio=s_aux.buttons[0].gpio;
        config->button_flags=s_aux.buttons[0].flags;
        memcpy(config->reserved,s_aux.reserved,sizeof(config->reserved));
        config->reserved_word=s_aux.reserved_word;
    }
}

bool inktag_om_device_settings_get_aux3(inktag_aux_io_t *config) {
    if(!config || !s_preferences_valid || !inktag_om_pins_available() ||
       !inktag_aux_io_validate(&s_aux))return false;
    *config=s_aux;return true;
}

bool inktag_om_device_settings_set_aux(const inktag_om_aux_io_config_t *config,
                                      const epd_config_t *epd_config,
                                      const char **error) {
    (void)config; (void)epd_config; if(error)*error="unsupported"; return false;
}

bool inktag_om_device_settings_set_aux3(const inktag_aux_io_t *config,
                                      const epd_config_t *epd_config,
                                      const char **error) {
    (void)config; (void)epd_config; if(error)*error="unsupported"; return false;
}

bool inktag_om_device_settings_reset_aux(void) {
    return false;
}

bool inktag_om_device_settings_reset_user(void) {
    const nvds_tag_id_t tags[] = {
        INKTAG_NAME_NVDS_TAG,
        INKTAG_DEVICE_LABEL_NVDS_TAG,
        INKTAG_DISPLAY_SETTINGS_NVDS_TAG,
    };
    for (uint8_t i = 0u; i < sizeof(tags) / sizeof(tags[0]); i++) {
        if (!inktag_om_nvds_delete_verified(tags[i])) return false;
    }
    defaults();
    s_preferences_valid=true;
    if(!save_settings())return false;
    inktag_om_pins_reload();
    load_display_settings(INKTAG_DISPLAY_SETTINGS_DEFAULT_WEEK_START,0u);
    return true;
}

bool inktag_om_aux_pin_claimed(uint8_t pin) {
    (void)pin; return false;
}
bool inktag_om_device_settings_io_busy(void) {
    return false;
}

bool inktag_om_device_settings_set_pins(bool present,const uint8_t *wire,const char **error) {
    inktag_om_pin_candidate_t candidate;
    if(!s_preferences_valid || !inktag_om_pins_available()) {
        if(error)*error="unknown-routing";
        return false;
    }
    if(!inktag_om_pins_prepare_override(present,wire,&candidate)) {
        if(error)*error="pin-backend";
        return false;
    }
    for(unsigned i=0;i<12u;++i)if(inktag_om_aux_pin_claimed(candidate.effective.pins[i])) {
        if(error)*error="aux-conflict";
        return false;
    }
    inktag_om_app_configuration_park();
    s_pin_override=present;memset(s_pin_wire,0,16u);
    if(present)memcpy(s_pin_wire,wire,16u);
    if(!save_settings()) {
        if(error)*error="flash";
        return false;
    }
    inktag_om_pins_publish(&candidate);
    return true;
}

void inktag_om_device_settings_get_led(inktag_status_config_t *config) {
    if (config) *config = s_led;
}

bool inktag_om_device_settings_set_led(const inktag_status_config_t *config,
                                      const char **error) {
    if (!inktag_status_config_valid(config)) {
        if (error) *error = "value";
        return false;
    }
    inktag_status_config_t previous = s_led;
    s_led = *config;
    if (!save_settings()) {
        s_led = previous;
        if (error) *error = "flash";
        return false;
    }
    if (error) *error = NULL;
    return true;
}

void inktag_om_device_settings_get_button_bindings(inktag_button_bindings_t *bindings) {
    if (bindings) *bindings = s_button_bindings;
}

bool inktag_om_device_settings_set_button_binding(inktag_input_source_t source,
                                                  const inktag_input_binding_t *binding,
                                                  const char **error) {
    (void)source; (void)binding; if(error)*error="unsupported"; return false;
}

bool inktag_om_device_settings_simulate_button(inktag_button_gesture_t gesture) {
    (void)gesture; return false;
}

bool inktag_om_device_settings_take_button(inktag_button_gesture_t *gesture,
                                           inktag_input_binding_t *binding) {
    (void)gesture; (void)binding; return false;
}
