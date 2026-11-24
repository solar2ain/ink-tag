#ifndef INKTAG_OM_DEVICE_SETTINGS_H
#define INKTAG_OM_DEVICE_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_epd_config.h"
#include "inktag_display_settings.h"
#include "inktag_input.h"
#include "inktag_protocol_core.h"
#include "inktag_status.h"
#include "inktag_device_password.h"
#include "inktag_aux_io.h"

#define INKTAG_OM_GPIO_UNUSED 255u
#define INKTAG_OM_DEVICE_NAME_MAX_BYTES 19u
#define INKTAG_OM_DEVICE_LABEL_MAX_BYTES 24u
#define INKTAG_OM_DEVICE_INFO_QR_SLOT_COUNT 1u
#define INKTAG_OM_DEVICE_INFO_QR_MAX_MODULES 57u
#define INKTAG_OM_DEVICE_INFO_QR_MAX_BYTES 407u

typedef struct {
    uint8_t modules;
    uint16_t length;
    uint8_t data[INKTAG_OM_DEVICE_INFO_QR_MAX_BYTES];
} inktag_om_device_info_qr_t;


typedef struct __attribute__((packed)) {
    uint8_t button_gpio;
    uint8_t button_flags;
    uint8_t reserved[3];
    uint16_t reserved_word;
} inktag_om_aux_io_config_t;

bool inktag_om_device_settings_get_aux3(inktag_aux_io_t *config);
bool inktag_om_device_settings_set_aux3(const inktag_aux_io_t *config,
                                      const epd_config_t *epd_config,
                                      const char **error);

void inktag_om_device_settings_init(const epd_config_t *epd_config,
                                    uint8_t display_flags);
void inktag_om_device_settings_process(void);

void inktag_om_device_settings_get_display(
    inktag_display_settings_t *settings);
bool inktag_om_device_settings_set_display(
    const inktag_display_settings_t *settings, const char **error);

void inktag_om_device_settings_get_aux(inktag_om_aux_io_config_t *config);
bool inktag_om_device_settings_set_aux(const inktag_om_aux_io_config_t *config,
                                      const epd_config_t *epd_config,
                                      const char **error);
bool inktag_om_device_settings_reset_aux(void);
bool inktag_om_device_settings_reset_user(void);
bool inktag_om_aux_pin_claimed(uint8_t pin);
bool inktag_om_device_settings_set_pins(bool present, const uint8_t *wire,
                                      const char **error);
bool inktag_om_device_settings_io_busy(void);

bool inktag_om_device_name_read(char name[INKTAG_OM_DEVICE_NAME_MAX_BYTES + 1u]);
bool inktag_om_device_name_write(const uint8_t *name, uint8_t length,
                                 const char **error);
bool inktag_om_device_label_read(char label[INKTAG_OM_DEVICE_LABEL_MAX_BYTES + 1u]);
bool inktag_om_device_label_write(const uint8_t *label, uint8_t length,
                                  const char **error);

bool inktag_om_device_info_qr_read(uint8_t slot, inktag_om_device_info_qr_t *qr);
bool inktag_om_device_info_qr_begin(uint8_t slot, uint8_t modules, uint16_t length,
                                uint32_t crc32, const char **error);
bool inktag_om_device_info_qr_write(uint8_t slot, uint16_t offset,
                                const uint8_t *data, uint16_t length,
                                const char **error);
bool inktag_om_device_info_qr_commit(uint8_t slot,
    inktag_device_password_config_t *config, const char **error);
bool inktag_om_password_load(inktag_device_password_config_t *config);
bool inktag_om_password_bundle_begin(const inktag_transfer_descriptor_t *descriptor);
inktag_status_v2_t inktag_om_password_bundle_commit(
    inktag_device_password_t *session, inktag_device_password_config_t *config,
    uint32_t now_ms);
void inktag_om_device_info_qr_abort(void);
bool inktag_om_password_reset_settings(inktag_device_password_config_t *actual);

void inktag_om_device_settings_get_led(inktag_status_config_t *config);
bool inktag_om_device_settings_set_led(const inktag_status_config_t *config,
                                      const char **error);

void inktag_om_device_settings_get_button_bindings(inktag_button_bindings_t *bindings);
bool inktag_om_device_settings_set_button_binding(inktag_input_source_t source,
                                                  const inktag_input_binding_t *binding,
                                                  const char **error);
bool inktag_om_device_settings_simulate_button(inktag_button_gesture_t gesture);
bool inktag_om_device_settings_take_button(inktag_button_gesture_t *gesture,
                                           inktag_input_binding_t *binding);


#endif
