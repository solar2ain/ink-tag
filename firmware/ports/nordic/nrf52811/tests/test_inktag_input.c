#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "inktag_input.h"

static void sample_until(inktag_button_state_t *state, bool pressed,
                         uint32_t *now, uint32_t end) {
    while (*now <= end) {
        inktag_button_sample(state, pressed, *now);
        *now += 10u;
    }
}

static void test_defaults_and_action_boundaries(void) {
    inktag_button_bindings_t bindings;
    inktag_button_bindings_default(&bindings);
    assert(bindings.single.action == INKTAG_INPUT_ACTION_NEXT_PAGE);
    assert(bindings.double_click.action == INKTAG_INPUT_ACTION_TOGGLE_CLEAR_PREVIOUS);
    assert(bindings.long_press.action == INKTAG_INPUT_ACTION_TOGGLE_SLIDESHOW);
    for (unsigned action = 0u; action <= UINT8_MAX; ++action)
        assert(!inktag_input_action_allowed(INKTAG_INPUT_SOURCE_NFC,
                                            (uint8_t)action));
    assert(!inktag_input_action_allowed(INKTAG_INPUT_SOURCE_NFC,
                                        INKTAG_INPUT_ACTION_TOGGLE_CLEAR_PREVIOUS));
    assert(!inktag_input_action_allowed(INKTAG_INPUT_SOURCE_BUTTON_SINGLE,
                                        INKTAG_INPUT_ACTION_CONTROL_URL));
}

static void test_single_click_waits_for_double_window(void) {
    inktag_button_state_t state;
    inktag_button_gesture_t gesture;
    uint32_t now = 0;
    inktag_button_state_init(&state);
    sample_until(&state, true, &now, 100);
    sample_until(&state, false, &now, 400);
    assert(!inktag_button_take_gesture(&state, &gesture));
    sample_until(&state, false, &now, 500);
    assert(inktag_button_take_gesture(&state, &gesture));
    assert(gesture == INKTAG_BUTTON_GESTURE_SINGLE);
}

static void test_double_click_suppresses_single(void) {
    inktag_button_state_t state;
    inktag_button_gesture_t gesture;
    uint32_t now = 0;
    inktag_button_state_init(&state);
    sample_until(&state, true, &now, 80);
    sample_until(&state, false, &now, 150);
    sample_until(&state, true, &now, 240);
    sample_until(&state, false, &now, 320);
    assert(inktag_button_take_gesture(&state, &gesture));
    assert(gesture == INKTAG_BUTTON_GESTURE_DOUBLE);
    sample_until(&state, false, &now, 800);
    assert(!inktag_button_take_gesture(&state, &gesture));
}

static void test_long_press_fires_once(void) {
    inktag_button_state_t state;
    inktag_button_gesture_t gesture;
    uint32_t now = 0;
    inktag_button_state_init(&state);
    sample_until(&state, true, &now, 1000);
    assert(inktag_button_take_gesture(&state, &gesture));
    assert(gesture == INKTAG_BUTTON_GESTURE_LONG);
    sample_until(&state, true, &now, 1600);
    sample_until(&state, false, &now, 2100);
    assert(!inktag_button_take_gesture(&state, &gesture));
}

static void test_bounce_does_not_create_gesture(void) {
    inktag_button_state_t state;
    inktag_button_gesture_t gesture;
    inktag_button_state_init(&state);
    inktag_button_sample(&state, true, 10);
    inktag_button_sample(&state, false, 20);
    inktag_button_sample(&state, true, 30);
    inktag_button_sample(&state, false, 40);
    inktag_button_sample(&state, false, 500);
    assert(!inktag_button_take_gesture(&state, &gesture));
}

int main(void) {
    test_defaults_and_action_boundaries();
    test_single_click_waits_for_double_window();
    test_double_click_suppresses_single();
    test_long_press_fires_once();
    test_bounce_does_not_create_gesture();
    puts("inktag input tests passed");
    return 0;
}
