#ifndef INKTAG_INPUT_H
#define INKTAG_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#define INKTAG_BUTTON_DEBOUNCE_MS 30u
#define INKTAG_BUTTON_DOUBLE_CLICK_MS 350u
#define INKTAG_BUTTON_LONG_PRESS_MS 800u

typedef enum {
    INKTAG_INPUT_SOURCE_NFC = 1,
    INKTAG_INPUT_SOURCE_BUTTON_SINGLE = 2,
    INKTAG_INPUT_SOURCE_BUTTON_DOUBLE = 3,
    INKTAG_INPUT_SOURCE_BUTTON_LONG = 4,
} inktag_input_source_t;

typedef enum {
    INKTAG_INPUT_ACTION_DISABLED = 0,
    INKTAG_INPUT_ACTION_NEXT_PAGE = 1,
    INKTAG_INPUT_ACTION_SHOW_SLOT = 2,
    INKTAG_INPUT_ACTION_SHOW_DEVICE_INFO = 3,
    INKTAG_INPUT_ACTION_SHOW_CALENDAR = 4,
    INKTAG_INPUT_ACTION_SHOW_CLOCK = 5,
    INKTAG_INPUT_ACTION_TOGGLE_SLIDESHOW = 6,
    INKTAG_INPUT_ACTION_STOP = 7,
    INKTAG_INPUT_ACTION_CONTROL_URL = 8,
    INKTAG_INPUT_ACTION_TOGGLE_CLEAR_PREVIOUS = 9,
    INKTAG_INPUT_ACTION_PREVIOUS_PAGE = 10,
} inktag_input_action_t;

typedef enum {
    INKTAG_BUTTON_GESTURE_NONE = 0,
    INKTAG_BUTTON_GESTURE_SINGLE = 1,
    INKTAG_BUTTON_GESTURE_DOUBLE = 2,
    INKTAG_BUTTON_GESTURE_LONG = 3,
} inktag_button_gesture_t;

typedef struct {
    uint8_t action;
    uint8_t slot_id;
} inktag_input_binding_t;

typedef struct {
    inktag_input_binding_t single;
    inktag_input_binding_t double_click;
    inktag_input_binding_t long_press;
} inktag_button_bindings_t;

typedef struct {
    bool raw_pressed;
    bool stable_pressed;
    bool long_emitted;
    uint8_t click_count;
    uint8_t pending_gesture;
    uint32_t raw_changed_at;
    uint32_t pressed_at;
    uint32_t released_at;
} inktag_button_state_t;

bool inktag_input_action_allowed(inktag_input_source_t source, uint8_t action);
void inktag_button_bindings_default(inktag_button_bindings_t *bindings);
/* Migrate only the exact former factory tuple; retain user-defined bindings. */
bool inktag_button_bindings_migrate_default(inktag_button_bindings_t *bindings);
const inktag_input_binding_t *inktag_button_binding(
    const inktag_button_bindings_t *bindings, inktag_button_gesture_t gesture);

void inktag_button_state_init(inktag_button_state_t *state);
void inktag_button_sample(inktag_button_state_t *state, bool pressed, uint32_t now_ms);
bool inktag_button_simulate(inktag_button_state_t *state, inktag_button_gesture_t gesture);
bool inktag_button_take_gesture(inktag_button_state_t *state, inktag_button_gesture_t *gesture);

#endif
