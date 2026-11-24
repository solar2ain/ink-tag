/* Included by the Goodix App. Common owns all session/wire state. This first
 * port batch is read-only; waveform fixture IDs are deliberately unregistered. */
#if INKTAG_PANEL_DIAGNOSTIC_ENABLED
#include <stddef.h>
static inktag_panel_diag_t s_panel_diag;
static epd_screen_config_t s_panel_diag_screen;
static epd_config_t s_panel_diag_pins;
static uint8_t s_panel_diag_owner = 0xffu, s_panel_diag_phase;
static uint32_t s_panel_diag_phase_ms;
static bool s_panel_diag_initialized;
static bool s_panel_diag_in_io;
static inktag_panel_diag_probe_t s_panel_diag_probe;

static uint8_t gr_panel_diag_model(void) {
#if defined(INKTAG_GOODIX_GR5515) && !defined(INKTAG_GR5513_INTERNAL_IMAGES)
    /* The two registered factory400x300 assemblies. Filename/PCB identity
     * does not silently select the controller or its OTP grammar. */
    if (s_screen_config.panel_recipe==1u && s_screen_config.width==400u && s_screen_config.height==300u &&
        s_screen_config.color_id==2u && !s_screen_config.transform &&
        !s_screen_config.visible_x && !s_screen_config.visible_y &&
        s_screen_config.visible_width==400u && s_screen_config.visible_height==300u &&
        !s_screen_config.native_ui_scale) {
        uint8_t board=inktag_gr_board_identity();
        if (board==INKTAG_GR_BOARD_ZKC42V && s_screen_config.driver_id==EPD_SCREEN_DRIVER_UC8276)
            return INKTAG_PANEL_DIAG_UC8276;
        if ((board==INKTAG_GR_BOARD_ZKC42V || board==INKTAG_GR_BOARD_ZKC42VM) &&
            s_screen_config.driver_id==EPD_SCREEN_DRIVER_SSD1683) return INKTAG_PANEL_DIAG_SSD1683;
    }
    return 0u;
#else
    /* Explicit diagnostic builds only. The internal-image GR5513 pilot uses
     * the exact 21V profile, not the unrelated 4.2-inch GR551x wiring. */
    if (inktag_gr_board_identity() != INKTAG_GR_BOARD_ZKC21V) return 0u;
    if (s_screen_config.driver_id == EPD_SCREEN_DRIVER_JD79665AA &&
        s_screen_config.panel_recipe == 1u &&
        s_screen_config.width == 768u && s_screen_config.height == 552u)
        return INKTAG_PANEL_DIAG_JD79665AA_A0;
    if (s_screen_config.driver_id == EPD_SCREEN_DRIVER_SSD1680 &&
        s_screen_config.panel_recipe == INKTAG_RECIPE_SSD1680_ZKC21V_FACTORY &&
        s_screen_config.width == 128u && s_screen_config.height == 250u)
        return INKTAG_PANEL_DIAG_SSD1680;
#if defined(INKTAG_GR5513_INTERNAL_IMAGES)
    /* This small-screen pilot does not admit UC or large-image assemblies. */
    return 0u;
#endif
    if (s_screen_config.panel_recipe != 0u) return 0u;
    if (s_screen_config.driver_id == EPD_SCREEN_DRIVER_UC8176 &&
        s_screen_config.width == 400u && s_screen_config.height == 300u)
        return INKTAG_PANEL_DIAG_UC8176;
    if (s_screen_config.driver_id == EPD_SCREEN_DRIVER_UC8179 &&
        s_screen_config.width == 800u && s_screen_config.height == 600u)
        return INKTAG_PANEL_DIAG_UC8179;
    /* Generic UC8151 is not proof of the C revision. UC8276/8251 plans are
     * not registered; never substitute UC8176's OTP capacity/dummy rules. */
    return 0u;
#endif
}

static bool gr_panel_diag_available(void) {
#if defined(INKTAG_GOODIX_GR5515) && !defined(INKTAG_GR5513_INTERNAL_IMAGES)
    static const epd_config_t pins5513 = INKTAG_BOARD_GR5513_EPD_CONFIG;
    static const epd_config_t pins5515 = INKTAG_BOARD_DEFAULT_EPD_CONFIG;
    const epd_config_t *pins=inktag_gr_board_identity()==INKTAG_GR_BOARD_ZKC42V?&pins5513:&pins5515;
    return gr_panel_diag_model()!=0u && !memcmp(pins,&s_config,offsetof(epd_config_t,display_mode));
#else
#if defined(INKTAG_GR5513_INTERNAL_IMAGES)
    static const epd_config_t pins = INKTAG_BOARD_GR5513_EPD_CONFIG;
#else
    static const epd_config_t pins = INKTAG_BOARD_DEFAULT_EPD_CONFIG;
#endif
    return gr_panel_diag_model() != 0u &&
        memcmp(&pins, &s_config, offsetof(epd_config_t, display_mode)) == 0;
#endif
}

static inktag_status_v2_t gr_panel_diag_read_plan(
    const inktag_panel_diag_request_t *request, inktag_panel_diag_read_plan_t *plan) {
    return inktag_panel_diag_read_plan(gr_panel_diag_model(), request, plan);
}

static bool gr_panel_diag_reset_io(void *ctx, bool high) {
    (void)ctx;digitalWrite(s_config.rst_pin,high ? HIGH : LOW);
    return digitalRead(s_config.rst_pin)==(high ? HIGH : LOW);
}
static bool gr_panel_diag_busy_io(void *ctx) {
    (void)ctx;return digitalRead(s_config.busy_pin)!=0;
}
static bool gr_panel_diag_write_io(void *ctx,uint8_t cmd,const uint8_t *p,uint8_t n) {
    (void)ctx;return inktag_gr_panel_diag_write(cmd,p,n);
}
static bool gr_panel_diag_read_io(void *ctx,uint8_t cmd,uint16_t skip,uint8_t n) {
    (void)ctx;return inktag_gr_panel_read_start(cmd,skip,n);
}
static bool gr_panel_diag_poll_io(void *ctx,uint8_t *p,uint8_t cap,uint8_t *n,bool *done) {
    (void)ctx;return inktag_gr_panel_read_poll(p,cap,n,done);
}
static const inktag_panel_diag_io_t s_panel_diag_io={NULL,gr_panel_diag_reset_io,
    gr_panel_diag_busy_io,gr_panel_diag_write_io,gr_panel_diag_read_io,gr_panel_diag_poll_io};

static bool gr_panel_diag_busy(void) {
    return s_panel_diag.token != 0u || s_panel_diag.acquired ||
           s_panel_diag.cleanup_failed;
}

static bool gr_panel_diag_running(void) {
    return s_panel_diag.result.state == INKTAG_PANEL_DIAG_RUNNING &&
           !s_panel_diag.cleanup_failed;
}

static inktag_status_v2_t gr_panel_diag_guard(void *context,
    const inktag_panel_diag_request_t *request) {
    inktag_panel_diag_read_plan_t plan;
    (void)context;
    if (!gr_panel_diag_available()) return INKTAG_STATUS_UNSUPPORTED;
    inktag_status_v2_t access = epd_control_access();
    if (access != INKTAG_STATUS_OK) return access;
    if (s_conn_idx == 0xffu || !s_notify_enabled || s_link_cleanup_due ||
        s_conn_idx != s_panel_diag_owner ||
        !inktag_device_password_allowed(&s_password, &s_password_config))
        return INKTAG_STATUS_DENIED;
    if (memcmp(&s_panel_diag_screen, &s_screen_config, sizeof(s_screen_config)) ||
        memcmp(&s_panel_diag_pins, &s_config, sizeof(s_config)))
        return INKTAG_STATUS_INVALID_STATE;
    /* Diagnostic reset/bus admission uses the existing 2.60V maintenance
     * floor. It does not change ordinary status/display voltage policy. */
    if (!inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_OTA, true))
        return INKTAG_STATUS_LOW_POWER;
    if (!request->operation) return INKTAG_STATUS_OK;
    if (request->operation == INKTAG_PANEL_DIAG_DESCRIBE) return INKTAG_STATUS_OK;
    if (request->operation == INKTAG_PANEL_DIAG_WAVEFORM)
        return INKTAG_STATUS_UNSUPPORTED;
    if (request->operation==INKTAG_PANEL_DIAG_RESET ||
        request->operation==INKTAG_PANEL_DIAG_POWER_CYCLE ||
        request->operation==INKTAG_PANEL_DIAG_RAM_WRITE)
        return gr_panel_diag_model()==INKTAG_PANEL_DIAG_JD79665AA_A0 ?
            INKTAG_STATUS_OK : INKTAG_STATUS_UNSUPPORTED;
    return gr_panel_diag_read_plan(request, &plan);
}

static bool gr_panel_diag_acquire(void *context) {
    (void)context;
    return !s_epd_work_active && !s_epd_work_pending &&
        !s_diag_scan_active && !s_picture_io_active && !s_v2_transfer.active &&
        !s_system_action_pending && !s_epd_gpio_ready &&
        !inktag_image_store_capture_active();
}

static inktag_status_v2_t gr_panel_diag_start(void *context,
    const inktag_panel_diag_request_t *request) {
    (void)context;
    (void)request;
    if(request->operation==INKTAG_PANEL_DIAG_DESCRIBE) return INKTAG_STATUS_OK;
    if (!epd_gpio_open_if_needed()) return epd_control_access();
    if(gr_panel_diag_model()==INKTAG_PANEL_DIAG_JD79665AA_A0)
        return inktag_panel_diag_probe_start(&s_panel_diag_probe,gr_panel_diag_model(),
            request,&s_panel_diag_io,s_status_now_ms);
    /* GPIO init holds reset low. Release without initializing a normal panel
     * recipe: a read must not load OTP/LUT, send PON/DRF or write panel RAM. */
    digitalWrite(s_config.rst_pin, HIGH);
    s_panel_diag_phase = 1u;
    s_panel_diag_phase_ms = s_status_now_ms;
    return INKTAG_STATUS_OK;
}

static void gr_panel_diag_poll(void *context, inktag_panel_diag_result_t *result) {
    inktag_panel_diag_read_plan_t plan;
    uint8_t written;
    bool done;
    (void)context;
    if(s_panel_diag.request.operation==INKTAG_PANEL_DIAG_DESCRIBE) {
        result->status=(uint8_t)inktag_panel_diag_describe(gr_panel_diag_model(),result->data);
        result->length=16u;result->state=INKTAG_PANEL_DIAG_DONE;return;
    }
    if(gr_panel_diag_model()==INKTAG_PANEL_DIAG_JD79665AA_A0) {
        /* SPI writes service maintenance callbacks; never recursively poll the
         * same common sequence while a command is only partly transmitted. */
        s_panel_diag_in_io=true;
        inktag_panel_diag_probe_poll(&s_panel_diag_probe,&s_panel_diag.request,
            &s_panel_diag_io,s_status_now_ms,result);
        s_panel_diag_in_io=false;return;
    }
    if (s_panel_diag_phase == 1u) {
        if ((uint32_t)(s_status_now_ms - s_panel_diag_phase_ms) < 10u) return;
        /* SSD1680/SSD1683 BUSY is active-high; UC candidates are active-low. */
        uint8_t model=gr_panel_diag_model();
        if (EPD_ReadBusy() == ((model==INKTAG_PANEL_DIAG_SSD1680 ||
                               model==INKTAG_PANEL_DIAG_SSD1683) ? HIGH : LOW)) {
            if ((uint32_t)(s_status_now_ms - s_panel_diag_phase_ms) < 500u) return;
            result->status = INKTAG_STATUS_TIMEOUT;
            result->state = INKTAG_PANEL_DIAG_FAILED;
            return;
        }
        result->status = (uint8_t)gr_panel_diag_read_plan(&s_panel_diag.request, &plan);
        if (result->status != INKTAG_STATUS_OK ||
            !inktag_gr_panel_read_start(plan.command, plan.skip, plan.length)) {
            result->status = INKTAG_STATUS_IO_ERROR;
            result->state = INKTAG_PANEL_DIAG_FAILED;
            return;
        }
        result->gates = 1u; /* reset only; never invent PON/DRF/POF edges. */
        s_panel_diag_phase = 2u;
        return;
    }
    if (!inktag_gr_panel_read_poll(result->data + result->length,
            (uint8_t)(INKTAG_PANEL_DIAG_MAX_DATA - result->length), &written, &done)) {
        result->status = INKTAG_STATUS_IO_ERROR;
        result->state = INKTAG_PANEL_DIAG_FAILED;
        return;
    }
    result->length = (uint8_t)(result->length + written);
    if (done) result->state = INKTAG_PANEL_DIAG_DONE;
}

static inktag_status_v2_t gr_panel_diag_cleanup(void *context) {
    (void)context;
    if(s_panel_diag.request.operation==INKTAG_PANEL_DIAG_DESCRIBE) return INKTAG_STATUS_OK;
#if defined(INKTAG_GR_CLOCK_PARTIAL) && INKTAG_GR_CLOCK_PARTIAL
    /* A diagnostic reset must never leave a valid clock history behind,
     * including when cutoff fails and the ownership latch remains held. */
    gr_clock_partial_invalidate();
#endif
    bool previous=s_panel_diag_in_io;
    s_panel_diag_in_io=true;
    if(gr_panel_diag_model()==INKTAG_PANEL_DIAG_JD79665AA_A0)
        (void)inktag_panel_diag_probe_stop(&s_panel_diag_probe,&s_panel_diag_io,&s_panel_diag.result);
    /* Always execute cutoff, even after POF/SPI failure; do not short-circuit. */
    bool off=inktag_gr_panel_read_power_off();
    s_panel_diag_in_io=previous;
    if(off && s_panel_diag.result.length==16u &&
        (s_panel_diag.request.operation==INKTAG_PANEL_DIAG_RESET ||
         s_panel_diag.request.operation==INKTAG_PANEL_DIAG_POWER_CYCLE ||
         s_panel_diag.request.operation==INKTAG_PANEL_DIAG_RAM_WRITE))
        s_panel_diag.result.data[3]|=32u;
    /* A failed POF remains in the observation/operation status. Verified EN
     * cutoff is the fallback safety action; do not latch a powered-off board
     * solely because an unresponsive controller could not acknowledge POF. */
    if(!off) return INKTAG_STATUS_IO_ERROR;
    epd_gpio_close_if_needed();
    s_epd = NULL; /* Reset invalidates controller state; next display re-inits. */
#if INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE
    s_panel_temperature_cache.valid = false;
#endif
    s_panel_diag_phase = 0u;
    return INKTAG_STATUS_OK;
}

static inktag_status_v2_t gr_panel_diag_close(void) {
    return inktag_panel_diag_close(&s_panel_diag);
}

static void gr_panel_diag_service(void) {
    if (s_panel_diag_in_io || !gr_panel_diag_busy()) return;
    if (s_conn_idx == 0xffu || s_link_cleanup_due) {
        /* The stack callback only marks disconnect; cleanup runs here once. */
        if (!s_panel_diag.cleanup_failed) (void)gr_panel_diag_close();
    } else inktag_panel_diag_tick(&s_panel_diag, s_status_now_ms);
}

static inktag_status_v2_t gr_panel_diag_dispatch(uint8_t opcode,
    const uint8_t *payload, uint16_t length, uint8_t *output, uint16_t *written) {
    if (!written) return INKTAG_STATUS_INVALID_REQUEST;
    *written = 0u;
    if (!payload || !length) return INKTAG_STATUS_INVALID_REQUEST;
    if (payload[0] != INKTAG_PANEL_DIAG_OPERATION) return INKTAG_STATUS_UNSUPPORTED;
    if (gr_panel_diag_busy() && s_conn_idx != s_panel_diag_owner)
        return INKTAG_STATUS_DENIED;
    if (!s_panel_diag_initialized) {
        const inktag_panel_diag_backend_t backend = {NULL, gr_panel_diag_guard,
            gr_panel_diag_acquire, gr_panel_diag_start, gr_panel_diag_poll,
            gr_panel_diag_cleanup};
        inktag_panel_diag_init(&s_panel_diag, &backend);
        s_panel_diag_initialized = true;
    }
    if (opcode == INKTAG_CMD_OPEN_DEBUG_SESSION && !gr_panel_diag_busy()) {
        s_panel_diag_owner = s_conn_idx;
        s_panel_diag_screen = s_screen_config;
        s_panel_diag_pins = s_config;
    }
    return inktag_panel_diag_dispatch(&s_panel_diag, opcode, payload, length,
        s_status_now_ms, output, 22u + INKTAG_PANEL_DIAG_MAX_DATA, written);
}
#endif
