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

static void test_stock_defaults(void) {
    inktag_button_bindings_t defaults;
    inktag_button_bindings_default(&defaults);
    assert(defaults.single.action == INKTAG_INPUT_ACTION_NEXT_PAGE);
    assert(defaults.double_click.action == INKTAG_INPUT_ACTION_PREVIOUS_PAGE);
    assert(defaults.long_press.action == INKTAG_INPUT_ACTION_TOGGLE_CLEAR_PREVIOUS);
    assert(!defaults.single.slot_id && !defaults.double_click.slot_id && !defaults.long_press.slot_id);
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

static void test_bounce_only(void) {
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
    test_stock_defaults();
    test_single_click_waits_for_double_window();
    test_double_click_suppresses_single();
    test_long_press_fires_once();
    test_bounce_only();
    puts("Free stock default/debounce/single/double/long/bounce tests passed");
    return 0;
}
