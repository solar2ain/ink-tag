#ifndef INKTAG_STATUS_H
#define INKTAG_STATUS_H

#include <stdbool.h>
#include <stdint.h>

#define INKTAG_STATUS_PROTOCOL_VERSION 2u
#define INKTAG_STATUS_SCENE_COUNT 11u
#define INKTAG_STATUS_WIRE_SIZE 2u
#define INKTAG_STATUS_STARTUP_COLOR_MS 360u
#define INKTAG_STATUS_READY_HALF_MS 120u
#define INKTAG_STATUS_LOCATE_COLOR_MS 240u
#define INKTAG_STATUS_LOCATE_ROUNDS 100u
#define INKTAG_STATUS_LOCATE_DURATION_MS \
    (INKTAG_STATUS_LOCATE_COLOR_MS * 3u * INKTAG_STATUS_LOCATE_ROUNDS)

typedef enum {
    INKTAG_STATUS_POLICY_OFF = 0,
    INKTAG_STATUS_POLICY_ALL = 1,
    INKTAG_STATUS_POLICY_DEFAULT = INKTAG_STATUS_POLICY_ALL,
    INKTAG_STATUS_POLICY_CRITICAL = 3,
} inktag_status_policy_t;

typedef enum {
    INKTAG_STATUS_SCENE_STARTUP = 0,
    INKTAG_STATUS_SCENE_ADVERTISING = 1,
    INKTAG_STATUS_SCENE_CONNECTED = 2,
    INKTAG_STATUS_SCENE_TRANSFER = 3,
    INKTAG_STATUS_SCENE_REFRESH = 4,
    INKTAG_STATUS_SCENE_OTA = 5,
    INKTAG_STATUS_SCENE_OTA_INSTALL = 6,
    INKTAG_STATUS_SCENE_SUCCESS = 7,
    INKTAG_STATUS_SCENE_ERROR = 8,
    INKTAG_STATUS_SCENE_LOW_BATTERY = 9,
    INKTAG_STATUS_SCENE_FIND = 10,
} inktag_status_scene_t;

typedef enum {
    INKTAG_STATUS_READY_APPLICATION = 0,
    INKTAG_STATUS_READY_RECOVERY = 1,
} inktag_status_ready_role_t;

typedef enum {
    INKTAG_STATUS_EFFECT_OFF = 0,
    INKTAG_STATUS_EFFECT_SOLID = 1,
    INKTAG_STATUS_EFFECT_SLOW_FLASH = 2,
    INKTAG_STATUS_EFFECT_FAST_FLASH = 3,
    INKTAG_STATUS_EFFECT_FLASH_ONCE = 4,
    INKTAG_STATUS_EFFECT_FLASH_TWICE = 5,
    INKTAG_STATUS_EFFECT_FLASH_THREE = 6,
    /* Startup uses this wire value for a fixed fast RGB sequence; other
     * scenes retain the slow alternating sequence. */
    INKTAG_STATUS_EFFECT_SEQUENCE_SLOW = 7,
} inktag_status_effect_t;

enum {
    INKTAG_STATUS_COLOR_OFF = 0,
    INKTAG_STATUS_COLOR_RED = 1u << 0,
    INKTAG_STATUS_COLOR_GREEN = 1u << 1,
    INKTAG_STATUS_COLOR_BLUE = 1u << 2,
    INKTAG_STATUS_COLOR_YELLOW = INKTAG_STATUS_COLOR_RED | INKTAG_STATUS_COLOR_GREEN,
    INKTAG_STATUS_COLOR_MAGENTA = INKTAG_STATUS_COLOR_RED | INKTAG_STATUS_COLOR_BLUE,
    INKTAG_STATUS_COLOR_CYAN = INKTAG_STATUS_COLOR_GREEN | INKTAG_STATUS_COLOR_BLUE,
    INKTAG_STATUS_COLOR_WHITE = INKTAG_STATUS_COLOR_RED | INKTAG_STATUS_COLOR_GREEN |
                                INKTAG_STATUS_COLOR_BLUE,
};

#define INKTAG_STATUS_SCENE_VALUE(color, effect) \
    ((uint8_t)(((uint8_t)(color) & 0x07u) | (((uint8_t)(effect) & 0x07u) << 3)))
#define INKTAG_STATUS_SCENE_COLOR(value) ((uint8_t)(value) & 0x07u)
#define INKTAG_STATUS_SCENE_EFFECT(value) (((uint8_t)(value) >> 3) & 0x07u)

typedef struct {
    uint8_t version;
    uint8_t policy;
} inktag_status_config_t;

typedef void (*inktag_status_apply_fn)(void *context, uint8_t color_mask);

typedef struct {
    inktag_status_config_t config;
    inktag_status_apply_fn apply;
    void *context;
    uint16_t active_mask;
    uint32_t scene_started_at;
    uint32_t transient_until;
    uint8_t current_scene;
    uint8_t transient_scene;
    uint8_t available_colors;
    uint8_t last_output;
} inktag_status_runtime_t;

void inktag_status_config_default(inktag_status_config_t *config);
uint8_t inktag_status_default_scene(inktag_status_scene_t scene);
bool inktag_status_config_valid(const inktag_status_config_t *config);
bool inktag_status_config_encode(const inktag_status_config_t *config,
                                 uint8_t wire[INKTAG_STATUS_WIRE_SIZE]);
bool inktag_status_config_decode(inktag_status_config_t *config,
                                 const uint8_t *wire, uint16_t length);

/* Select the colder of two independently qualified temperatures. Inputs use
 * signed 1/16 degree Celsius so panel fractions are compared before the
 * integer wire value is rounded toward the colder whole degree. */
bool inktag_temperature_select_lower(bool first_valid,
                                     int16_t first_sixteenths,
                                     bool second_valid,
                                     int16_t second_sixteenths,
                                     int8_t *celsius);

void inktag_status_init(inktag_status_runtime_t *runtime,
                        const inktag_status_config_t *config,
                        uint8_t available_colors,
                        inktag_status_apply_fn apply, void *context,
                        uint32_t now_ms);
void inktag_status_set_config(inktag_status_runtime_t *runtime,
                              const inktag_status_config_t *config,
                              uint32_t now_ms);
void inktag_status_set_available_colors(inktag_status_runtime_t *runtime,
                                        uint8_t available_colors,
                                        uint32_t now_ms);
void inktag_status_set_active(inktag_status_runtime_t *runtime,
                              inktag_status_scene_t scene, bool active,
                              uint32_t now_ms);
void inktag_status_trigger(inktag_status_runtime_t *runtime,
                           inktag_status_scene_t scene, uint32_t now_ms);
/* Queue the fixed boot-role tail behind STARTUP, or start it when role
 * readiness is reported after STARTUP. A 120 ms off separator precedes its
 * three pulses. It is deliberately not a wire scene or persistent setting. */
void inktag_status_role_ready(inktag_status_runtime_t *runtime,
                              inktag_status_ready_role_t role,
                              uint32_t now_ms);
/* Run the Free, local physical locator. It is deliberately separate from the
 * persistent wire scenes and from Apple Find: qualified colours rotate
 * red/green/blue at 240 ms per colour for 100 rounds (72 seconds). The
 * explicit action remains visible even when the ordinary indicator preset is
 * OFF; LED-less boards may still use its bounded lifetime for a buzzer. */
void inktag_status_locate_start(inktag_status_runtime_t *runtime,
                                uint32_t now_ms);
void inktag_status_locate_stop(inktag_status_runtime_t *runtime,
                               uint32_t now_ms);
bool inktag_status_locate_active(const inktag_status_runtime_t *runtime);
/* True only while visible status output can still change with sub-second
 * elapsed time. Ports use this to stop their status timer when the selected
 * scene is absent, off or stable. This query never mutates the runtime. */
bool inktag_status_needs_periodic_tick(const inktag_status_runtime_t *runtime,
                                       uint32_t now_ms);
void inktag_status_tick(inktag_status_runtime_t *runtime, uint32_t now_ms);

#endif
