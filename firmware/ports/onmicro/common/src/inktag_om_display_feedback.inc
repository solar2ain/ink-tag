/* Common owns colours, priorities and pulse durations. This vendor adapter
 * anchors completion only after synchronous persistence and panel cleanup. */
static void status_led_finish_display(bool failed, bool low_power_abort) {
    runtime_clock_update();
    inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_REFRESH,
                             false, s_status_now_ms);
    inktag_status_trigger(&s_status,
                          low_power_abort ? INKTAG_STATUS_SCENE_LOW_BATTERY :
                          failed ? INKTAG_STATUS_SCENE_ERROR :
                                   INKTAG_STATUS_SCENE_SUCCESS,
                          s_status_now_ms);
    status_timer_update();
}
