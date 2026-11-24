/* Peripheral execution only: common owns selection, colour and cadence.
 * Exact-board route is opt-in; only currently claimed input pins preempt it. */
#if defined(INKTAG_BOARD_STATUS_PWM_TIMER)
static bool s_status_pwm_active;
static bool s_status_pwm_attempted;
static uint16_t s_status_pwm_half_ms;
static uint32_t s_status_pwm_anchor;

static void status_pwm_stop(void) {
    if (s_status_pwm_active) {
        drv_tim_pwm_output_stop(INKTAG_BOARD_STATUS_PWM_TIMER, TIM_CHAN_ALL);
        (void)drv_tim_uninit(INKTAG_BOARD_STATUS_PWM_TIMER);
        board_led_apply(INKTAG_BOARD_LED_BLUE_PIN, false);
        pm_sleep_allow(INKTAG_BOARD_STATUS_PWM_PM_ID);
        s_status_pwm_active = false;
    }
    s_status_pwm_attempted = false;
}

static bool status_pwm_update(void) {
    uint8_t color;
    uint16_t half_ms = inktag_status_continuous_pulse(&s_status, &color);
    if (inktag_om_pins_led(2u) != INKTAG_BOARD_LED_BLUE_PIN ||
        s_led_test_active || color != INKTAG_STATUS_COLOR_BLUE || !half_ms ||
        inktag_om_aux_pin_claimed(INKTAG_BOARD_LED_BLUE_PIN)) {
        status_pwm_stop();
        return false;
    }
    if (s_status_pwm_attempted && s_status_pwm_half_ms == half_ms &&
        s_status_pwm_anchor == s_status.scene_started_at)
        return s_status_pwm_active;
    status_pwm_stop();
    s_status_pwm_attempted = true;
    s_status_pwm_half_ms = half_ms;
    s_status_pwm_anchor = s_status.scene_started_at;
    tim_pwm_complementary_output_config_t config = {
        .cnt_freq = 1000u, .period_cnt = (uint32_t)half_ms * 2u,
    };
    pin_config_t pin = {
        INKTAG_BOARD_LED_BLUE_PIN, {INKTAG_BOARD_STATUS_PWM_PINMUX},
        PMU_PIN_MODE_PP, PMU_PIN_DRIVER_CURRENT_NORMAL,
    };
    config.chan[INKTAG_BOARD_STATUS_PWM_CHANNEL].en = 1u;
    config.chan[INKTAG_BOARD_STATUS_PWM_CHANNEL].cfg.pol =
        inktag_om_pins_active_low(INKTAG_PIN_LED_BLUE)?TIM_PWM_POL_ACTIVE_HIGH:TIM_PWM_POL_ACTIVE_LOW;
    config.chan[INKTAG_BOARD_STATUS_PWM_CHANNEL].cfg.oc_val = half_ms;
    config.chan[INKTAG_BOARD_STATUS_PWM_CHANNEL].cfg.complementary_output_enable = true;
    if (drv_tim_init(INKTAG_BOARD_STATUS_PWM_TIMER) != OM_ERROR_OK) return false;
    if (drv_tim_pwm_complementary_output_start(INKTAG_BOARD_STATUS_PWM_TIMER,
                                               &config) != OM_ERROR_OK) {
        (void)drv_tim_uninit(INKTAG_BOARD_STATUS_PWM_TIMER);
        return false;
    }
    pm_sleep_prevent(INKTAG_BOARD_STATUS_PWM_PM_ID);
    drv_pin_init(&pin, 1u);
    s_status_pwm_active = true;
    return true;
}
#else
static void status_pwm_stop(void) {}
static bool status_pwm_update(void) { return false; }
#endif
