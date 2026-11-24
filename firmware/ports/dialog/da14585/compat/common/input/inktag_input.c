#include "inktag_input.h"

#include <string.h>

static bool elapsed(uint32_t now, uint32_t then, uint32_t interval) {
    return (uint32_t)(now - then) >= interval;
}

static void queue_gesture(inktag_button_state_t *state, inktag_button_gesture_t gesture) {
    if (state->pending_gesture == INKTAG_BUTTON_GESTURE_NONE) {
        state->pending_gesture = (uint8_t)gesture;
    }
}

void inktag_button_bindings_default(inktag_button_bindings_t *bindings) {
    if (!bindings) return;
    memset(bindings, 0, sizeof(*bindings));
    bindings->single.action = INKTAG_INPUT_ACTION_NEXT_PAGE;
    bindings->double_click.action = INKTAG_INPUT_ACTION_PREVIOUS_PAGE;
    bindings->long_press.action = INKTAG_INPUT_ACTION_TOGGLE_CLEAR_PREVIOUS;
}

void inktag_button_state_init(inktag_button_state_t *state) {
    if (state) memset(state, 0, sizeof(*state));
}

void inktag_button_sample(inktag_button_state_t *state, bool pressed, uint32_t now_ms) {
    if (!state) return;
    if (pressed != state->raw_pressed) {
        state->raw_pressed = pressed;
        state->raw_changed_at = now_ms;
    }

    if (state->raw_pressed != state->stable_pressed &&
        elapsed(now_ms, state->raw_changed_at, INKTAG_BUTTON_DEBOUNCE_MS)) {
        state->stable_pressed = state->raw_pressed;
        if (state->stable_pressed) {
            state->pressed_at = now_ms;
            state->long_emitted = false;
        } else if (!state->long_emitted) {
            state->click_count++;
            state->released_at = now_ms;
            if (state->click_count >= 2u) {
                state->click_count = 0;
                queue_gesture(state, INKTAG_BUTTON_GESTURE_DOUBLE);
            }
        }
        return;
    }

    if (state->stable_pressed && !state->long_emitted &&
        elapsed(now_ms, state->pressed_at, INKTAG_BUTTON_LONG_PRESS_MS)) {
        state->long_emitted = true;
        state->click_count = 0;
        queue_gesture(state, INKTAG_BUTTON_GESTURE_LONG);
    } else if (!state->stable_pressed && state->click_count == 1u &&
               elapsed(now_ms, state->released_at, INKTAG_BUTTON_DOUBLE_CLICK_MS)) {
        state->click_count = 0;
        queue_gesture(state, INKTAG_BUTTON_GESTURE_SINGLE);
    }
}

bool inktag_button_take_gesture(inktag_button_state_t *state, inktag_button_gesture_t *gesture) {
    if (!state || !gesture || state->pending_gesture == INKTAG_BUTTON_GESTURE_NONE) return false;
    *gesture = (inktag_button_gesture_t)state->pending_gesture;
    state->pending_gesture = INKTAG_BUTTON_GESTURE_NONE;
    return true;
}
