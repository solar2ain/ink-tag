#include "main.h"

#include <string.h>
#include "inktag_epd_driver.h"
#if INKTAG_EPD_TEMPERATURE_OPTION
#include "inktag_nfc.h"
#endif
#include "inktag_gui_device_qr.h"
#include "inktag_feature_store.h"
#include "inktag_epd_wait_policy.h"
#include "inktag_epd_temperature.h"
#include "inktag_soak_telemetry.h"
#include "inktag_tlsr_adc.h"
#include "inktag_code_guard.h"
#include "inktag_tlsr_health.h"
#include "stack/ble/ble.h"
#include "tlsr8359_hsel.h"

#define INKTAG_TLSR_EPD_SERVICE_INTERVAL_MS 1u
#define INKTAG_TLSR_GUI_RENDER_BUFFER_BYTES 16384u
#ifndef INKTAG_TLSR_SW_SPI_HALF_US
#define INKTAG_TLSR_SW_SPI_HALF_US 1u
#endif
#if INKTAG_TLSR_SW_SPI_HALF_US != 1 && INKTAG_TLSR_SW_SPI_HALF_US != 2
#error "Software-SPI TX guard must be 1 or 2 us"
#endif
#define INKTAG_JD_TSC_COMMAND 0x40u
/* Getters never wake a panel. A foreground connection sample may refresh it. */
#define PANEL_TEMPERATURE_MAX_AGE_MS (15u * 60u * 1000u)
#if INKTAG_TLSR_EPD_SERVICE_INTERVAL_MS > INKTAG_EPD_WAIT_MAINTENANCE_MAX_MS || \
    INKTAG_TLSR_EPD_SERVICE_INTERVAL_MS > INKTAG_EPD_WAIT_CANCEL_MAX_MS
#error "TLSR EPD service interval violates the common BUSY-wait policy"
#endif

static RAM epd_screen_config_t s_screen;
static RAM epd_model_t *s_model;
static RAM uint8_t s_last_active_driver = 0xFFu;
static RAM bool s_initialized;
static RAM bool s_abort;
static RAM inktag_epd_result_t s_last_result;
static RAM uint8_t s_last_command, s_failed_command;
static RAM uint8_t s_controller_temperature;
static RAM uint8_t s_controller_temperature_fraction;
static RAM bool s_controller_temperature_valid;
static RAM int16_t s_controller_temperature_sixteenths;
static RAM uint32_t s_controller_temperature_sample_ms;
static RAM bool s_temperature_refresh_complete;
static bool s_busy_asserted;
static uint32_t s_busy_active_ms;
static void tlsr_clock_invalidate(void);
static RAM const inktag_tlsr_gpio_profile_t *s_profile;
static RAM bool s_panel_powered;
static RAM bool s_nebular_hw_spi_ready;
static RAM epd_abort_callback_t s_abort_callback;
/* Scratch pixels do not need retention: every GUI page is cleared before it
 * is drawn. Keeping this buffer out of the TLSR's low 32 KiB retention bank
 * leaves retained BLE/product state untouched. At 16 KiB the 768x552 packed
 * frame takes seven pages instead of fourteen. Check the linked BSS/stack
 * gap for each build: the gap is NOT a measured stack high-water margin. */
static uint8_t s_render_buffer[INKTAG_TLSR_GUI_RENDER_BUFFER_BYTES]
    __attribute__((aligned(4)));
#if INKTAG_TRANSFER_LZO1X_ENABLED || INKTAG_TLSR_NFC_FMSC_PILOT
uint8_t *inktag_epd_transfer_workspace(uint32_t *capacity) {
    *capacity = 4192u; /* Existing scratch; 2000 decoded + 2192 compressed. */
    return s_render_buffer;
}
#endif
static uint32_t s_gui_stream_ticks, s_gui_stream_bytes;
static uint32_t s_gui_gap_ticks, s_gui_service_end, s_gui_gap_bytes;
static bool s_gui_measure_stream;

/* Maximum compute/transfer interval between service turns, not accumulated
 * service time. Unsigned subtraction also covers one SDK timer wrap. */
static void gui_measure_gap(uint32_t now) {
    uint32_t gap = now - s_gui_service_end;
    if (gap > s_gui_gap_ticks) {
        s_gui_gap_ticks = gap;
        s_gui_gap_bytes = s_gui_stream_bytes;
    }
}

static void platform_cooperative_service(void) {
    if (s_gui_measure_stream) gui_measure_gap(clock_time());
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
    blt_sdk_main_loop();
    inktag_soak_telemetry_radio_maintenance();
    inktag_app_status_service();
    inktag_app_cooperative_snapshot();
    inktag_ble_process();
#if INKTAG_TLSR_NFC_FMSC_PILOT && INKTAG_BUILD_PRO
#include "inktag_epd_nfc_hook_1_pro.inc"
#endif
    wd_clear();
    if (s_gui_measure_stream) s_gui_service_end = clock_time();
}

#if INKTAG_EPD_ENABLE_OSPREY_E6
#include "inktag_epd_extended_1_pro.inc"
#endif

static void platform_cooperative_wait_ms(uint32_t duration_ms) {
    /* Status effects use absolute monotonic time, but a phase is visible only
     * if the cooperative host gets a chance to apply it.  Controller reset,
     * power and recipe guards can be 30-200 ms each; slicing them also keeps
     * BLE maintenance, basic snapshots and cancellation live without
     * re-entering display or mutating product work. */
    while (duration_ms-- != 0u) {
        platform_cooperative_service();
        WaitMs(1u);
    }
}

#if INKTAG_EPD_OSPREY_E6_TEMPERATURE_READ
static bool e6_read_transaction(uint8_t command, uint8_t length) {
    return ((command == 0x40u && length == 1u) ||
            (command == 0x70u && length == 7u)) &&
        (s_screen.driver_id == EPD_SCREEN_DRIVER_IST7513 ||
         s_screen.driver_id == EPD_SCREEN_DRIVER_IST7601);
}

static bool e6_tsc_transaction(uint8_t command, uint8_t length) {
    return command == 0x40u && e6_read_transaction(command,length);
}

/* Optional sensor failure must not overwrite the display's first result.
 * Keep CS held but service BLE/WDT and honour cancellation while converting. */
static bool e6_tsc_level(bool idle, uint16_t remaining) {
    for (;;) {
        if (EPD_AbortRequested()) return false;
        if (EPD_ReadBusy() == idle) return true;
        if (!remaining--) return false;
        platform_cooperative_wait_ms(1u);
    }
}
static bool e6_tsc_ready(void) {
    sleep_us(4u);
    return e6_tsc_level(false,100u) && e6_tsc_level(true,2000u);
}
#endif

void inktag_gui_cooperative_yield(void) {
    /* DrawGUIEx runs inside the application dispatcher. Basic snapshots are
     * bounded; never re-enter inktag_app_process() or another display. */
    platform_cooperative_service();
}

static void configure_output(uint32_t pin, bool high) {
    /* Load the inactive level before enabling the output driver so a wake-up
     * cannot create a spurious CS, clock or reset edge. */
    gpio_write(pin, high ? 1 : 0);
    gpio_set_func(pin, AS_GPIO);
    /* The factory direct-GPIO branch leaves the input buffer enabled even on
     * driven pins. Keep that detail because PB6 changes direction for reads. */
    gpio_set_input_en(pin, 1);
    gpio_set_output_en(pin, 1);
}

static void panel_power_on(void) {
    /* The first factory display-vtable entry drives the board-selected rail
     * control low: PB7 on Nowa and PB5 on Nebular profile 7.  Load the active
     * level before enabling the output so the external gate cannot glitch. */
    if (!s_profile) return;
    s_panel_powered = true;
    if (!s_profile->epd_enable) return;
    configure_output(s_profile->epd_enable, !s_profile->epd_enable_active_low);
    gpio_setup_up_down_resistor(s_profile->epd_enable, PM_PIN_UP_DOWN_FLOAT);
}

static void panel_power_off(void) {
    /* The paired factory parked entry releases the same active-low net and
     * applies a 10 kOhm pull-up.  This is distinct from the six signal pins,
     * whose Nebular parked recipe uses 100 kOhm pull-downs. */
    if (!s_profile) return;
    s_panel_powered = false;
    if (!s_profile->epd_enable) return;
    /* High-active boards must stop actively sourcing the gate before release.
     * Keep the previously qualified low-active release sequence unchanged. */
    if (!s_profile->epd_enable_active_low) gpio_write(s_profile->epd_enable, 0);
    gpio_set_func(s_profile->epd_enable, AS_GPIO);
    gpio_set_output_en(s_profile->epd_enable, 0);
    gpio_set_input_en(s_profile->epd_enable, 0);
    gpio_setup_up_down_resistor(s_profile->epd_enable,
        s_profile->epd_enable_active_low ? PM_PIN_PULLUP_10K : PM_PIN_PULLDOWN_100K);
}

static _attribute_ram_code_ void spi_byte(uint8_t value) {
    uint8_t bit;
    /* Same SDK gpio_write RMW operations, but decode each selected GPIO only
     * once per byte, not three times per bit through a Flash-resident helper.
     * Never retain a whole bank value: other pins may change between edges. */
    volatile uint8_t *clock_output=&reg_gpio_out(s_profile->epd_clk);
    volatile uint8_t *mosi_output=&reg_gpio_out(s_profile->epd_mosi);
    uint8_t clock_bit=(uint8_t)s_profile->epd_clk;
    uint8_t mosi_bit=(uint8_t)s_profile->epd_mosi;
    bool timed=!s_profile->epd_hardware_spi;
    for (bit = 0; bit < 8u; ++bit) {
        *clock_output &= (uint8_t)~clock_bit;
        if (value & 0x80u) *mosi_output |= mosi_bit;
        else *mosi_output &= (uint8_t)~mosi_bit;
        /* All builds default TX to 1us after the human-directed A0 trial;
         * explicit 2us is the conservative rollback, independent of signing.
         * Historical unrestricted-loop Nowa failures were not a controlled
         * 1us failure: OTP and BUSY-edge fixes were also required. Preserve
         * those gates and the separate 2us RX timing. Other panels still
         * require physical qualification. Hardware SPI bypasses this path. */
        if (timed)
              sleep_us(INKTAG_TLSR_SW_SPI_HALF_US);
        value <<= 1;
        *clock_output |= clock_bit;
        if (timed)
              sleep_us(INKTAG_TLSR_SW_SPI_HALF_US);
    }
}

static void nebular_hw_spi_enable(void) {
    if (s_nebular_hw_spi_ready) return;

    /* The factory profile-7 branch selects the TLSR B6/B7/D2/D7 SPI group,
     * preserves the factory3MHz wire rate (divider7 at48MHz), mode0, then enables
     * shared-data mode.  Shared mode moves the bidirectional wire to PB6 so
     * PB7 remains the independent command/data GPIO. */
    spi_master_init(INKTAG_TLSR_EPD_SPI_DIVIDER, SPI_MODE0);
    spi_master_gpio_set(SPI_GPIO_GROUP_B6B7D2D7);
    spi_set_share_mode_en();
    configure_output(s_profile->epd_dc, true);
    configure_output(s_profile->epd_cs, true);
    s_nebular_hw_spi_ready = true;
}

static _attribute_ram_code_ void nebular_hw_write_byte(bool data, uint8_t value) {
    nebular_hw_spi_enable();
    gpio_write(s_profile->epd_dc, data ? 1 : 0);
    gpio_write(s_profile->epd_cs, 0);
    reg_spi_ctrl &= (uint8_t)~(FLD_SPI_DATA_OUT_DIS | FLD_SPI_RD);
    reg_spi_data = value;
    while (reg_spi_ctrl & FLD_SPI_BUSY) { }
    gpio_write(s_profile->epd_cs, 1);
}

static void nebular_hw_read_active(uint8_t *value, uint8_t length) {
    uint8_t index;
    volatile uint8_t dummy;

    if (value == NULL || length == 0u) return;
    reg_spi_ctrl &= (uint8_t)~FLD_SPI_DATA_OUT_DIS;
    reg_spi_ctrl |= (FLD_SPI_DATA_OUT_DIS | FLD_SPI_RD);
    /* Match the vendor spi_read() primitive: one register read starts the
     * receive clock and is discarded, then each following register read
     * returns one sequential payload byte while clocking the next byte. */
    dummy = reg_spi_data;
    while (reg_spi_ctrl & FLD_SPI_BUSY) { }
    for (index = 0u; index < length; ++index) {
        value[index] = reg_spi_data;
        while (reg_spi_ctrl & FLD_SPI_BUSY) { }
    }
    (void)dummy;
}

static uint8_t nebular_hw_read_byte(void) {
    uint8_t value;

    nebular_hw_spi_enable();
    gpio_write(s_profile->epd_dc, 1);
    gpio_write(s_profile->epd_cs, 0);
    nebular_hw_read_active(&value, 1u);
    gpio_write(s_profile->epd_cs, 1);
    return value;
}

static void nebular_hw_read_tsc(uint8_t temperature[2]) {
    nebular_hw_spi_enable();
    gpio_write(s_profile->epd_dc, 1);
    gpio_write(s_profile->epd_cs, 0);
    /* TSC is one continuous two-byte read. The hardware engine requires one
     * transaction dummy, not one dummy before every payload byte. */
    nebular_hw_read_active(temperature, 2u);
    gpio_write(s_profile->epd_cs, 1);
}

static bool nebular_hw_command_read(uint8_t command, uint8_t *value,
                                     uint8_t length) {
    if (value == NULL || length == 0u) return false;

    nebular_hw_spi_enable();
    gpio_write(s_profile->epd_dc, 0);
    gpio_write(s_profile->epd_cs, 0);
    reg_spi_ctrl &= (uint8_t)~(FLD_SPI_DATA_OUT_DIS | FLD_SPI_RD);
    reg_spi_data = command;
    while (reg_spi_ctrl & FLD_SPI_BUSY) { }
    gpio_write(s_profile->epd_dc, 1);
#if INKTAG_EPD_OSPREY_E6_TEMPERATURE_READ
    if (e6_read_transaction(command,length)) {
        bool ok = true;
        /* Release shared SDA without clocking the receive dummy yet. */
        reg_spi_ctrl |= FLD_SPI_DATA_OUT_DIS;
        gpio_setup_up_down_resistor(s_profile->epd_mosi, PM_PIN_PULLUP_10K);
        if (e6_tsc_transaction(command,length)) ok = e6_tsc_ready();
        else sleep_us(4u); /* REV70 uses the same released, pulled-up SDA. */
        if (ok) nebular_hw_read_active(value,length);
        gpio_write(s_profile->epd_cs,1);
        reg_spi_ctrl &= (uint8_t)~(FLD_SPI_DATA_OUT_DIS | FLD_SPI_RD);
        gpio_setup_up_down_resistor(s_profile->epd_mosi, PM_PIN_UP_DOWN_FLOAT);
        return ok;
    }
#endif
    nebular_hw_read_active(value, length);
    gpio_write(s_profile->epd_cs, 1);
    return true;
}

static _attribute_ram_code_ void write_byte(bool data, uint8_t value) {
    if (s_profile && s_profile->epd_hardware_spi) {
        nebular_hw_write_byte(data, value);
        return;
    }
    /* Match the Nowa factory wrapper at overlay 0x08F8/0x0920 exactly.  It
     * lowers CS, selects DC, and lets the serializer finish with CLK high;
     * CS rises without an extra falling clock edge.  The next byte's first
     * serializer step returns CLK low after CS has already fallen. */
    gpio_write(s_profile->epd_cs, 0);
    gpio_write(s_profile->epd_dc, data ? 1 : 0);
    spi_byte(value);
    gpio_write(s_profile->epd_cs, 1);
}

void inktag_epd_command(uint8_t command) {
    s_last_command = command;
    write_byte(false, command);
}

static void data1(uint8_t value) {
    static uint8_t transferred;
    write_byte(true, value);
    /* Keep one byte budget across row, fill and single-byte API calls.
     * A per-call counter never fires for repeated 25/64-byte chunks. CS is
     * already high here, exactly as at the former bulk-only service point. */
    if ((++transferred & 0x7fu) == 0u)
        platform_cooperative_service();
}

void inktag_epd_data(const uint8_t *data, uint16_t length) {
    /* Telink's BLE host is cooperative. Panel bytes are independent CS
     * transactions, so service the radio between bounded groups without
     * changing the controller's RAM stream semantics. */
    uint32_t started = s_gui_measure_stream ? clock_time() : 0u;
    if (s_gui_measure_stream) s_gui_stream_bytes += length;
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
    while (length--) {
        data1(*data++);
        if (s_abort) break;
    }
    if (s_gui_measure_stream) s_gui_stream_ticks += clock_time() - started;
}

static void read_bytes(uint8_t *data, uint8_t length) {
    uint8_t bit;
    uint8_t value;

    /* Match Nowa's factory TSC reader at overlay 0x0838.  PB6 keeps the
     * active profile's floating pull while its output driver is disabled;
     * each bit is sampled with CLK low, then CLK is pulsed high and low. */
    gpio_set_output_en(s_profile->epd_mosi, 0);
    gpio_set_input_en(s_profile->epd_mosi, 1);
    gpio_setup_up_down_resistor(s_profile->epd_mosi, PM_PIN_UP_DOWN_FLOAT);
    gpio_write(s_profile->epd_cs, 0);
    gpio_write(s_profile->epd_clk, 0);
    gpio_write(s_profile->epd_dc, 1);
    while (length--) {
        value = 0u;
        for (bit = 0u; bit < 8u; ++bit) {
            value <<= 1u;
            if (!s_profile->epd_hardware_spi)
                sleep_us(2u);
            if (gpio_read(s_profile->epd_mosi)) value |= 1u;
            gpio_write(s_profile->epd_clk, 1);
            if (!s_profile->epd_hardware_spi)
                sleep_us(2u);
            gpio_write(s_profile->epd_clk, 0);
        }
        *data++ = value;
    }
    gpio_write(s_profile->epd_cs, 1);
    gpio_set_output_en(s_profile->epd_mosi, 1);
}

static bool gpio_command_read(uint8_t command, uint8_t *value,
                              uint8_t length) {
    uint8_t bit;
    uint8_t byte;
    bool ok = true;
#if INKTAG_EPD_OSPREY_E6_TEMPERATURE_READ
    bool conversion = e6_tsc_transaction(command,length);
    bool e6_read = e6_read_transaction(command,length);
#else
    bool e6_read = false;
#endif

    if (value == NULL || length == 0u || s_profile == NULL) return false;

    /* JD read commands keep CS asserted while the bidirectional data wire
     * changes from the command byte to the response.  This is the exact
     * framing used by the physically-qualified JD79665CA reference path. */
    gpio_set_output_en(s_profile->epd_mosi, 1);
    gpio_set_input_en(s_profile->epd_mosi, 1);
    gpio_write(s_profile->epd_dc, 0);
    gpio_write(s_profile->epd_cs, 0);
    spi_byte(command);
    if (!e6_read) gpio_write(s_profile->epd_clk, 0);
    gpio_write(s_profile->epd_dc, 1);
    gpio_set_output_en(s_profile->epd_mosi, 0);
    gpio_setup_up_down_resistor(s_profile->epd_mosi, PM_PIN_UP_DOWN_FLOAT);
#if INKTAG_EPD_OSPREY_E6_TEMPERATURE_READ
    if (e6_read) {
        gpio_setup_up_down_resistor(s_profile->epd_mosi, PM_PIN_PULLUP_10K);
        if (conversion) ok = e6_tsc_ready();
        else sleep_us(4u);
    }
#endif
    while (ok && length--) {
        byte = 0u;
        for (bit = 0u; bit < 8u; ++bit) {
            byte <<= 1u;
            /* EPD Lab's qualified E6 mode0 reader clocks low/high before
             * sampling. Keep the legacy low-sample reader for other ICs. */
            if (e6_read) gpio_write(s_profile->epd_clk, 0);
            sleep_us(2u);
            if (e6_read) {
                gpio_write(s_profile->epd_clk, 1);
                sleep_us(2u);
            }
            if (gpio_read(s_profile->epd_mosi)) byte |= 1u;
            if (!e6_read) {
                gpio_write(s_profile->epd_clk, 1);
                sleep_us(2u);
                gpio_write(s_profile->epd_clk, 0);
            }
        }
        *value++ = byte;
    }
    gpio_write(s_profile->epd_cs, 1);
#if INKTAG_EPD_OSPREY_E6_TEMPERATURE_READ
    if (e6_read) {
        gpio_setup_up_down_resistor(s_profile->epd_mosi, PM_PIN_UP_DOWN_FLOAT);
        gpio_write(s_profile->epd_mosi, 0);
    }
#endif
    gpio_set_output_en(s_profile->epd_mosi, 1);
    return ok;
}

static uint8_t read_byte(void) {
    uint8_t value;
    if (s_profile && s_profile->epd_hardware_spi)
        return nebular_hw_read_byte();
    read_bytes(&value, 1u);
    return value;
}

static void reset_panel(uint16_t duration_ms) {
    if (s_profile && s_profile->base->epd_reset_high_low_high) {
        /* The physically qualified Hanshow/JD79668 sequence holds every
         * high/low/high phase for 200 ms during normal initialization.  Keep
         * the caller-provided duration instead of collapsing this to the
         * factory GPIO helper's shorter electrical toggle. */
        gpio_write(s_profile->epd_reset, 1);
        platform_cooperative_wait_ms(duration_ms);
        gpio_write(s_profile->epd_reset, 0);
        platform_cooperative_wait_ms(duration_ms);
        gpio_write(s_profile->epd_reset, 1);
        platform_cooperative_wait_ms(duration_ms);
        return;
    }
    gpio_write(s_profile->epd_reset, 0);
    platform_cooperative_wait_ms(2u);
    gpio_write(s_profile->epd_reset, 1);
    platform_cooperative_wait_ms(2u);
}

static bool busy_active(void) {
    /* Controller polarity belongs to the selected common driver, not the
     * original panel fitted to a board. A user-selected SSD panel can share
     * Nebular GPIOs with its original active-low JD panel. */
    if (s_model && s_model->drv && s_model->drv->read_busy)
        return s_model->drv->read_busy(s_model);
    bool level = s_profile && gpio_read(s_profile->epd_busy) != 0;
    return s_profile && (s_profile->epd_busy_active_high ? level : !level);
}

/* Common EPD driver platform adapter.  Controller command sequences live in
 * firmware/common/epd; this file owns only TLSR8359 GPIO/SPI, BUSY pumping and
 * the board-specific electrical bus selection recovered from stock. */
void pinMode(uint32_t pin, uint32_t mode) {
    gpio_set_func(pin, AS_GPIO);
    if (mode == OUTPUT) {
        gpio_set_input_en(pin, 1);
        gpio_set_output_en(pin, 1);
    } else if (mode == INPUT || mode == INPUT_PULLUP || mode == INPUT_PULLDOWN) {
        gpio_set_output_en(pin, 0);
        gpio_set_input_en(pin, 1);
        gpio_setup_up_down_resistor(pin,
            mode == INPUT_PULLUP ? PM_PIN_PULLUP_10K :
            mode == INPUT_PULLDOWN ? PM_PIN_PULLDOWN_100K : PM_PIN_UP_DOWN_FLOAT);
    } else {
        gpio_set_output_en(pin, 0);
        gpio_set_input_en(pin, 0);
        gpio_setup_up_down_resistor(pin, PM_PIN_UP_DOWN_FLOAT);
    }
}

void digitalWrite(uint32_t pin, uint32_t value) { gpio_write(pin, value != 0u); }
uint32_t digitalRead(uint32_t pin) { return gpio_read(pin) != 0u; }
void delay(uint32_t ms) { platform_cooperative_wait_ms(ms); }

void EPD_GPIO_Load(epd_config_t *config) { (void)config; }
void EPD_GPIO_Init(void) { inktag_epd_restore_gpio(); }
void inktag_epd_resume(void) {
    if (!s_panel_powered && s_profile) {
        /* Retention reloads digital inputs. These panel roles are not wake
         * sources; restore input-disable while retaining their analog bias. */
        uint32_t pins[] = {s_profile->epd_reset, s_profile->epd_dc,
            s_profile->epd_cs, s_profile->epd_clk, s_profile->epd_mosi,
            s_profile->epd_busy, s_profile->epd_enable};
        for (uint8_t i = 0u; i < sizeof(pins) / sizeof(pins[0]); ++i)
            if (pins[i]) gpio_set_input_en(pins[i], 0);
    }
}

void EPD_GPIO_Uninit(void) {
    uint32_t pins[6];
    uint8_t i;
    if (!s_profile) return;
    pins[0] = s_profile->epd_reset;
    pins[1] = s_profile->epd_dc;
    pins[2] = s_profile->epd_cs;
    pins[3] = s_profile->epd_clk;
    pins[4] = s_profile->epd_mosi;
    pins[5] = s_profile->epd_busy;

    /* Releasing EN alone does not establish that panel logic is unpowered.
     * Use43D2's defined-high signal bias for every controller/board; the
     * original panel's board-specific down/float recipe is not transferable.
     * Preserve the qualified BUSY exceptions below and each PCB's EN route. */
    s_nebular_hw_spi_ready = false;
    for (i = 0u; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        uint8_t pull = PM_PIN_PULLUP_10K;
        bool bias_first = i == 0u || i == 2u;
        /* E6 idle BUSY can sink a board's original-panel pull. The human
         * measured1.54 at2uA with this release; round/large are the next A/B.
         * Change that pull only; retain input sampling,
         * RESET/CS and every other role/rail. Analog pull state survives
         * retention, and restore_gpio() already floats BUSY before active IO.
         * This is an A/B hypothesis, not a measured low-current qualification. */
        if (i == 5u &&
            ((s_screen.driver_id == EPD_SCREEN_DRIVER_IST7513 &&
              s_screen.panel_recipe == INKTAG_RECIPE_IST7513_OSPREY_HT0154A10_E6) ||
             (s_screen.driver_id == EPD_SCREEN_DRIVER_IST7601 &&
              (s_screen.panel_recipe == INKTAG_RECIPE_IST7601_OSPREY_HT0169A06_E6 ||
               s_screen.panel_recipe == INKTAG_RECIPE_IST7601_OSPREY_HT037A02_E6))))
            pull = PM_PIN_UP_DOWN_FLOAT;
        gpio_set_func(pins[i], AS_GPIO);
        /* Install the existing RESET/CS bias before releasing the drivers;
         * retain the same final pulls and all other recipes' ordering. */
        if (bias_first) gpio_setup_up_down_resistor(pins[i], pull);
        gpio_set_output_en(pins[i], 0);
        gpio_set_input_en(pins[i], 0);
        if (!bias_first) gpio_setup_up_down_resistor(pins[i], pull);
    }
    if (s_profile->base->epd_busy_park_float) {
        /* Exact-board idle-current pilot: the unpowered BUSY_N output may
         * conduct through a 10 kOhm pull-up. BUSY is not a wake source and
         * restore_gpio() re-enables its input before the next panel command. */
        gpio_set_input_en(s_profile->epd_busy, 0);
        gpio_setup_up_down_resistor(s_profile->epd_busy,
                                    PM_PIN_UP_DOWN_FLOAT);
    }
    panel_power_off();
    inktag_epd_resume();
    /* PC5 is a stock input/wakeup signal. It is deliberately never driven. */
    gpio_set_func(GPIO_PC5, AS_GPIO);
    gpio_set_output_en(GPIO_PC5, 0);
    gpio_set_input_en(GPIO_PC5, 1);
}

void EPD_SPI_Write(uint8_t *value, uint8_t length) {
    if (value != NULL) inktag_epd_data(value, length);
}

void EPD_SPI_Read(uint8_t *value, uint8_t length) {
    if (value == NULL || length == 0u) return;
    if (s_profile && s_profile->epd_hardware_spi && length == 2u)
        nebular_hw_read_tsc(value);
    else
        read_bytes(value, length);
}

void EPD_WriteCmd(uint8_t command) { inktag_epd_command(command); }
void EPD_WriteData(uint8_t *value, uint8_t length) {
    if (value != NULL) inktag_epd_data(value, length);
}

static void cache_controller_temperature(const uint8_t value[2]) {
    s_controller_temperature = value[0];
    s_controller_temperature_fraction = value[1];
    s_temperature_refresh_complete = false;
    s_controller_temperature_sample_ms = inktag_app_monotonic_ms();
    /* SSD R1B uses the high nibble, JD R40 uses quarter degrees, and UC
     * internal R40 has no fractional byte. Keep their raw diagnostics. */
    switch (s_screen.driver_id) {
#if defined(INKTAG_TLSR_CLOCK_SSD1683) && INKTAG_TLSR_CLOCK_SSD1683
    case EPD_SCREEN_DRIVER_SSD1683:
        /* SSD1683 R1B is one signed whole-degree byte, never SSD1680's
         * two-byte fractional encoding. Its0xff is valid -1C. */
        s_controller_temperature_valid =
            inktag_ssd1683_r1b_temperature_sixteenths(
                s_panel_powered,value,&s_controller_temperature_sixteenths);
        break;
#endif
#if INKTAG_EPD_OSPREY_E6_TEMPERATURE_READ
    case EPD_SCREEN_DRIVER_IST7513:
    case EPD_SCREEN_DRIVER_IST7601:
        s_controller_temperature_valid =
            inktag_osprey_r40_temperature_sixteenths(
                s_panel_powered,value[0],&s_controller_temperature_sixteenths);
        break;
#endif
    case EPD_SCREEN_DRIVER_SSD1XXX:
        /* Exact _02 stock recipe reads the two-byte R1B during powered init.
         * Do not qualify another generic SSD1XXX recipe from that evidence. */
        if (s_screen.panel_recipe !=
                INKTAG_RECIPE_SSD1XXX_HANSHOW_HSEL5_154M02_NATIVE_OTP) {
            s_controller_temperature_valid = false;
            break;
        }
        /* fall through */
    case EPD_SCREEN_DRIVER_SSD1675:
    case EPD_SCREEN_DRIVER_SSD1677:
    case EPD_SCREEN_DRIVER_SSD1680:
    case EPD_SCREEN_DRIVER_SSD1619:
        s_controller_temperature_valid =
            inktag_ssd1680_r1b_temperature_sixteenths(
                s_panel_powered, value, &s_controller_temperature_sixteenths);
        break;
    case EPD_SCREEN_DRIVER_UC8176:
    case EPD_SCREEN_DRIVER_UC8276:
        s_controller_temperature_valid =
            inktag_uc_internal_r40_temperature_sixteenths(
                s_panel_powered, value, &s_controller_temperature_sixteenths);
        break;
    case EPD_SCREEN_DRIVER_UC8XXX:
        /* Nowa213's recovered two-byte reader qualifies only signed whole
         * degrees; its exact UC COG suffix/fraction format is still unknown. */
        s_controller_temperature_valid =
            inktag_uc_internal_r40_temperature_sixteenths(
                s_panel_powered && s_screen.panel_recipe ==
                    INKTAG_RECIPE_UC8XXX_HANSHOW_NOWA_213R_N,
                value, &s_controller_temperature_sixteenths);
        break;
    case EPD_SCREEN_DRIVER_JD79661:
    case EPD_SCREEN_DRIVER_JD79653A:
    case EPD_SCREEN_DRIVER_JD79665AA:
    case EPD_SCREEN_DRIVER_JD79665CA:
    case EPD_SCREEN_DRIVER_JD79668AA:
    case EPD_SCREEN_DRIVER_JD79XXX:
        s_controller_temperature_valid =
            inktag_jd_r40_temperature_sixteenths(
                s_panel_powered, value, &s_controller_temperature_sixteenths);
        break;
    default:
        s_controller_temperature_valid = false;
        break;
    }
}

void EPD_ReadData(uint8_t *value, uint8_t length) {
    s_controller_temperature_valid = false;
    s_temperature_refresh_complete = false;
    EPD_SPI_Read(value, length);
    if (value != NULL && length != 0u) {
        s_controller_temperature = value[0];
        if (length > 1u) {
            cache_controller_temperature(value);
        }
    }
}
bool EPD_ReadCommand(uint8_t command, uint8_t *value, uint8_t length) {
    bool ok = false;
    if (s_profile != NULL && value != NULL && length != 0u) {
        if (s_profile->epd_hardware_spi)
            ok = nebular_hw_command_read(command, value, length);
        else
            ok = gpio_command_read(command, value, length);
    }
    /* Status/revision reads do not replace, complete or refresh a TSC sample.
     * Failed IO/invalid arguments and a new TSC still invalidate the old one. */
    if (!ok || command == INKTAG_JD_TSC_COMMAND) {
        s_controller_temperature_valid = false;
        s_temperature_refresh_complete = false;
    }
    if (ok && command == INKTAG_JD_TSC_COMMAND) {
#if INKTAG_EPD_OSPREY_E6_TEMPERATURE_READ
        if (e6_tsc_transaction(command,length)) {
            uint8_t whole[2] = {value[0],0u};
            cache_controller_temperature(whole);
        } else
#endif
        if (length >= 2u) cache_controller_temperature(value);
    }
    return ok;
}

#if INKTAG_EPD_ENABLE_HANSHOW_SMALL_Q
static bool small_q_spi_wait(void) {
    uint32_t started = clock_time();
    while (reg_spi_ctrl & FLD_SPI_BUSY)
        if (clock_time_exceed(started, 1000u)) return false;
    return true;
}

bool EPD_ReadCommandWindow(uint8_t command, uint16_t skip,
                           uint8_t *value, uint8_t length) {
    uint16_t index, total;
    uint8_t byte;
    volatile uint8_t dummy;
    bool ok = false;
    /* Only the recovered read-only stock trim check is admitted. No public
     * arbitrary register/OTP primitive or MCU-family GPIO fallback. */
    if (s_profile == NULL || !s_profile->epd_hardware_spi ||
        command != 0x92u || (skip != 3945u && skip != 5993u) ||
        value == NULL || length != 7u || EPD_AbortRequested()) return false;
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
    nebular_hw_write_byte(false, command);
    gpio_write(s_profile->epd_dc, 1);
    gpio_write(s_profile->epd_cs, 0);
    reg_spi_ctrl &= (uint8_t)~FLD_SPI_DATA_OUT_DIS;
    reg_spi_ctrl |= (FLD_SPI_DATA_OUT_DIS | FLD_SPI_RD);
    dummy = reg_spi_data;
    if (!small_q_spi_wait()) goto cleanup;
    /* Stock 3C2BC consumes its inclusive prefix, seven tested bytes, and
     * one closing byte. Clear automatic read before that final byte, just
     * as the selected vendor spi_read callback does. */
    total = (uint16_t)(skip + length + 1u);
    for (index = 0u; index < total; ++index) {
        if (index == total - 1u) reg_spi_ctrl &= (uint8_t)~FLD_SPI_RD;
        byte = reg_spi_data;
        if (!small_q_spi_wait()) goto cleanup;
        if (index >= skip && index < total - 1u) value[index - skip] = byte;
        if ((index & 63u) == 63u) {
            platform_cooperative_service();
            if (EPD_AbortRequested()) goto cleanup;
        }
    }
    ok = !EPD_AbortRequested();
cleanup:
    reg_spi_ctrl &= (uint8_t)~FLD_SPI_RD;
    gpio_write(s_profile->epd_cs, 1);
    (void)dummy;
    return ok;
}
#endif
void EPD_WriteByte(uint8_t value) { data1(value); }
uint8_t EPD_ReadByte(void) { return read_byte(); }

void EPD_FillRAM(uint8_t command, uint8_t value, uint32_t length) {
    uint8_t buffer[64];
    memset(buffer, value, sizeof(buffer));
    EPD_WriteCmd(command);
    while (length > 0u) {
        uint8_t chunk = length > sizeof(buffer) ? sizeof(buffer) : (uint8_t)length;
        EPD_WriteData(buffer, chunk);
        length -= chunk;
    }
}

void EPD_Reset(bool status, uint16_t duration) {
    (void)status;
    /* reset_panel() preserves the two recovered recipes: Nowa low/high and
     * Nebular high/low/high.  Nebular additionally preserves the duration
     * required by the physically qualified JD79668 panel sequence. */
    reset_panel(duration);
}

void EPD_ResetPulse(uint16_t high_before_ms, uint16_t low_ms,
                    uint16_t high_after_ms) {
    if (!s_profile) return;
    gpio_write(s_profile->epd_reset, 1);
    platform_cooperative_wait_ms(high_before_ms);
    gpio_write(s_profile->epd_reset, 0);
    platform_cooperative_wait_ms(low_ms);
    gpio_write(s_profile->epd_reset, 1);
    platform_cooperative_wait_ms(high_after_ms);
}

void EPD_ResetDualPulse(uint16_t duration) {
    if (!s_profile) return;
    gpio_write(s_profile->epd_reset, 0);
    platform_cooperative_wait_ms(duration);
    gpio_write(s_profile->epd_reset, 1);
    platform_cooperative_wait_ms(duration);
    gpio_write(s_profile->epd_reset, 0);
    platform_cooperative_wait_ms(duration);
    gpio_write(s_profile->epd_reset, 1);
    platform_cooperative_wait_ms(duration);
}

bool EPD_ReadBusy(void) {
    /* Normalize the configured physical BUSY polarity into the selected
     * common driver's convention. Raw diagnostics remain genuinely raw. */
    bool raw=inktag_epd_busy_raw();
    bool active=s_profile && (s_profile->epd_busy_active_high?raw:!raw);
    if (s_screen.driver_id == EPD_SCREEN_DRIVER_SSD1680 &&
        s_screen.color_id == EPD_SCREEN_COLOR_BW) return !active;
    return (s_screen.driver_id&0xf0u)==0x20u ? active : !active;
}
void EPD_SetAbortCallback(epd_abort_callback_t callback) { s_abort_callback = callback; }
bool EPD_AbortRequested(void) {
    return s_abort || (s_abort_callback != NULL && s_abort_callback())
#if INKTAG_TLSR_NFC_FMSC_PILOT && INKTAG_BUILD_PRO
#include "inktag_epd_nfc_hook_2_pro.inc"
#endif
        ;
}
/* The common driver ABI retains these hooks, but the former per-refresh
 * counters were bring-up telemetry only. Normal control flow uses the return
 * value and the port-level result enum from EPD_WaitBusyEdge(). */
bool EPD_BusyDiagAsserted(void) { return s_busy_asserted; }
uint32_t EPD_BusyDiagAssertDelayMs(void) { return 0u; }
uint32_t EPD_BusyDiagActiveMs(void) { return s_busy_active_ms; }
void EPD_BusyDiagReset(void) {s_busy_asserted=false;s_busy_active_ms=0u;}

static void latch_wait_failure(inktag_epd_result_t result) {
    if (s_last_result == INKTAG_EPD_RESULT_OK) {
        s_last_result = result;
        s_failed_command = s_last_command;
    }
}

uint8_t inktag_epd_failed_command(void) { return s_failed_command; }

static bool common_busy_wait_step(void) {
    if (EPD_AbortRequested()) {
        latch_wait_failure(INKTAG_EPD_RESULT_STOPPED);
        return false;
    }
    platform_cooperative_service();
    WaitMs(INKTAG_TLSR_EPD_SERVICE_INTERVAL_MS);
    return true;
}

bool EPD_WaitBusyEdge(bool status, uint16_t assert_timeout,
                      uint16_t release_timeout) {
    while (EPD_ReadBusy() != status && assert_timeout > 0u) {
        if (!common_busy_wait_step()) return false;
        --assert_timeout;
    }
    if (EPD_ReadBusy() != status) {
        latch_wait_failure(INKTAG_EPD_RESULT_NO_BUSY_EDGE);
        return false;
    }
    s_busy_asserted=true;
    uint32_t asserted_at=clock_time();
    while (EPD_ReadBusy() == status && release_timeout > 0u) {
        if (!common_busy_wait_step()) return false;
        --release_timeout;
    }
    s_busy_active_ms=(clock_time()-asserted_at)/CLOCK_16M_SYS_TIMER_CLK_1MS;
    if (EPD_ReadBusy() == status) {
        latch_wait_failure(INKTAG_EPD_RESULT_TIMEOUT);
        return false;
    }
    return true;
}

void EPD_WaitBusy(bool status, uint16_t timeout) {
    /* Preserve the legacy/recovered waiter contract: if BUSY is already at
     * its idle level, return immediately; otherwise wait for release.  The
     * strict assertion+release check is reserved for PON/DRF/POF and final
     * refresh paths that call EPD_WaitBusyEdge explicitly.  Hanshow stock's
     * SSD1675 initialization at 0x03D948 has exactly this release-only loop. */
    uint32_t asserted_at=clock_time();
    if (EPD_ReadBusy()==status) s_busy_asserted=true;
    while (EPD_ReadBusy() == status && timeout > 0u) {
        if (!common_busy_wait_step()) return;
        --timeout;
    }
    s_busy_active_ms=(clock_time()-asserted_at)/CLOCK_16M_SYS_TIMER_CLK_1MS;
    if (EPD_ReadBusy() == status)
        latch_wait_failure(INKTAG_EPD_RESULT_TIMEOUT);
}

void EPD_LED_ON(void) {}
void EPD_LED_OFF(void) {}
void EPD_LED_Toggle(void) {}
void EPD_LED_BLINK(void) {}
uint16_t EPD_ReadVoltage(void) { return inktag_epd_voltage(); }

void inktag_epd_restore_gpio(void) {
    bool cold_panel_start = !s_panel_powered;
    if (inktag_epd_control_access(&s_screen) != INKTAG_STATUS_OK) return;
    s_nebular_hw_spi_ready = false;
    /* Nowa's active factory initializer at overlay 0x03CC enables RESET,
     * DC, CS, CLK and MOSI as outputs, leaves BUSY as an input, and applies
     * pull mode 0 (floating) to all six signals.  RESET starts low, CS high
     * and CLK low.  Nebular's profile-7 hardware SPI branch likewise owns
     * the active drive state; its 100 kOhm pull-down routine is the parked
     * state, not a substitute for output drive. */
    panel_power_on();
    /* The only 9903 sequence that has visibly changed the panel first held
     * the recovered PB7 active-low control for 20 ms, then allowed the bus
     * levels to settle for another 50 ms before hardware RESET.  The former
     * normal path collapsed both intervals to zero and could race the board
     * power/control circuit even though the following SSD1675 transcript was
     * correct. Keep this timing board-local; it is not part of SSD1675 or of
     * Nebular's separately qualified JD79668 panel recipe. */
    if (!s_profile) return;
    if (cold_panel_start && s_profile->base->epd_power_settle_ms != 0u)
        platform_cooperative_wait_ms(s_profile->base->epd_power_settle_ms);
    configure_output(s_profile->epd_reset, false);
    gpio_setup_up_down_resistor(s_profile->epd_reset, PM_PIN_UP_DOWN_FLOAT);
    configure_output(s_profile->epd_dc, true);
    gpio_setup_up_down_resistor(s_profile->epd_dc, PM_PIN_UP_DOWN_FLOAT);
    configure_output(s_profile->epd_cs, true);
    gpio_setup_up_down_resistor(s_profile->epd_cs, PM_PIN_UP_DOWN_FLOAT);
    configure_output(s_profile->epd_clk, false);
    gpio_setup_up_down_resistor(s_profile->epd_clk, PM_PIN_UP_DOWN_FLOAT);
    configure_output(s_profile->epd_mosi, false);
    gpio_setup_up_down_resistor(s_profile->epd_mosi, PM_PIN_UP_DOWN_FLOAT);
    gpio_set_func(s_profile->epd_busy, AS_GPIO);
    gpio_set_output_en(s_profile->epd_busy, 0);
    gpio_set_input_en(s_profile->epd_busy, 1);
    /* Defined active idle-high for the round receiver; park removes this
     * pull and disables the receiver. Other panels keep their active bias. */
    gpio_setup_up_down_resistor(s_profile->epd_busy,
        s_screen.driver_id == EPD_SCREEN_DRIVER_IST7601 &&
        s_screen.panel_recipe == INKTAG_RECIPE_IST7601_OSPREY_HT0169A06_E6 ?
        PM_PIN_PULLUP_10K : PM_PIN_UP_DOWN_FLOAT);
    /* PC5 is a stock input/wakeup signal. It is deliberately never driven. */
    gpio_set_func(GPIO_PC5, AS_GPIO);
    gpio_set_output_en(GPIO_PC5, 0);
    gpio_set_input_en(GPIO_PC5, 1);
    if (cold_panel_start && s_profile->base->epd_bus_settle_ms != 0u)
        platform_cooperative_wait_ms(s_profile->base->epd_bus_settle_ms);
}

static bool screen_is_supported(const epd_screen_config_t *screen) {
    epd_screen_t resolved;
    return screen != NULL &&
           epd_screen_resolve(screen, &resolved) == EPD_SCREEN_STATUS_OK;
}

inktag_status_v2_t inktag_epd_control_access(const epd_screen_config_t *screen) {
    return inktag_app_screen_access(screen,
        s_profile != NULL && s_profile->epd_busy_known &&
        inktag_settings_screen_valid() && screen_is_supported(screen));
}

static bool admit_panel_work(void) {
    inktag_status_v2_t status = inktag_epd_control_access(&s_screen);
    if (status == INKTAG_STATUS_OK) return true;
    /* Keep a prior electrical/cancellation failure, but never fabricate OK.
     * Cleanup below is deliberately not gated by current entitlement. */
    if (s_last_result == INKTAG_EPD_RESULT_OK ||
        s_last_result == INKTAG_EPD_RESULT_NOT_READY)
        s_last_result = status == INKTAG_STATUS_DENIED ?
            INKTAG_EPD_RESULT_DENIED : INKTAG_EPD_RESULT_UNSUPPORTED;
    return false;
}

static bool driver_owns_failure_sleep(void) {
    if (!s_model) return false;
    if (s_model->driver_id == EPD_SCREEN_DRIVER_SSD1675 &&
        s_model->panel_recipe == INKTAG_RECIPE_SSD1675_HANSHOW_NOWA_154R_N &&
        s_model->width == 152u && s_model->height == 152u) return true;
#if INKTAG_EPD_ENABLE_OSPREY_E6
#include "inktag_epd_extended_2_pro.inc"
#endif
    return s_model->driver_id == EPD_SCREEN_DRIVER_JD79665AA &&
        (s_model->panel_recipe == INKTAG_RECIPE_JD79665AA_HUAWEI_398_A0 ||
         s_model->panel_recipe == INKTAG_RECIPE_JD79665AA_HUAWEI_398_A1);
}

void inktag_epd_configure(inktag_board_id_t board) {
    epd_screen_config_t decoded;
    memset(&s_screen, 0, sizeof(s_screen));
    s_initialized = false;
    (void)board;
    s_profile = inktag_tlsr_gpio_profile();
    s_panel_powered = false;
    s_model = NULL;
    s_abort = false;
    s_last_result = INKTAG_EPD_RESULT_NOT_READY;
    /* A missing/corrupt selector must fail closed. Nowa's red LED is PD2 on
     * Nebular's panel CS, so silently choosing either board can toggle a live
     * display bus. Fixed Recovery remains available without EPD GPIO. */
    /* Configuration metadata remains readable when panel routes are absent.
     * GPIO readiness gates hardware access, not the validated saved record. */
    if (inktag_settings_screen_valid() && epd_screen_config_decode(inktag_settings()->screen_config,
            EPD_SCREEN_WIRE_SIZE, &decoded) == EPD_SCREEN_STATUS_OK) {
        if (screen_is_supported(&decoded)) {
            s_screen = decoded;
        }
    }
    if (!s_profile || !s_profile->epd_mosi) { s_profile=NULL; return; }
    EPD_GPIO_Uninit();
}

const epd_screen_config_t *inktag_epd_screen(void) { return &s_screen; }

void inktag_epd_cold_boot_sleep(inktag_board_id_t board) {
    bool needs_sleep =
        s_screen.driver_id == EPD_SCREEN_DRIVER_SSD1675 &&
        s_screen.panel_recipe == INKTAG_RECIPE_SSD1675_HANSHOW_NOWA_154R_N &&
        s_screen.width == 152u && s_screen.height == 152u;
    (void)board;
#if INKTAG_EPD_ENABLE_OSPREY_E6
#include "inktag_epd_extended_3_pro.inc"
#endif
    /* A released rail/GPIO bus does not guarantee controller deep sleep.
     * Nowa152 needs the same init -> 10,01 -> park path already used by the
     * connection-temperature sampler; its B1/A1 OTP loads never request a
     * display update. Keep E6's existing init-only cleanup. No RAM writes,
     * refresh or new per-board feature switch; current screen/routes and the
     * ordinary power/Pro admission still decide whether hardware is touched. */
    if (s_profile && s_profile->epd_busy_known &&
        inktag_settings_screen_valid() &&
        needs_sleep &&
        screen_is_supported(&s_screen)) {
        inktag_epd_init();
        inktag_epd_sleep();
    }
}

inktag_status_v2_t inktag_epd_set_screen(const epd_screen_config_t *screen) {
    tlsr_clock_invalidate();
    epd_screen_t resolved;
    inktag_image_store_profile_view_t images;
    inktag_tlsr_pin_candidate_t routes;
    inktag_image_store_result_t result;
    if (!screen || epd_screen_resolve(screen, &resolved) != EPD_SCREEN_STATUS_OK ||
        resolved.config.panel_recipe!=screen->panel_recipe)
        return INKTAG_STATUS_INCOMPATIBLE;
    {
        /* A new profile can supply the currently missing BUSY convention;
         * do not require the old profile's routing to admit configuration. */
        inktag_status_v2_t access = inktag_app_screen_access(screen, s_profile != NULL);
        if (access != INKTAG_STATUS_OK) return access;
    }
    if (!inktag_settings_screen_valid()) return INKTAG_STATUS_IO_ERROR;
    if (!inktag_tlsr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,true))
        return INKTAG_STATUS_LOW_POWER;
    if (inktag_image_store_busy()) return INKTAG_STATUS_BUSY;
    if (!inktag_image_store_profile_allowed(screen) ||
        !inktag_tlsr_pin_routes_prepare(screen,&routes) || !routes.busy_known)
        return INKTAG_STATUS_INCOMPATIBLE;
    result=inktag_image_store_prepare_profile(screen,&images);
    if (result==INKTAG_IMAGE_STORE_BUSY) return INKTAG_STATUS_BUSY;
    if (result==INKTAG_IMAGE_STORE_IO_ERROR) return INKTAG_STATUS_IO_ERROR;
    if (result!=INKTAG_IMAGE_STORE_OK && result!=INKTAG_IMAGE_STORE_NO_SPACE)
        return INKTAG_STATUS_INCOMPATIBLE;
    /* Foreground ownership remains held by the existing V2 dispatcher. No
     * candidate has changed live Settings, slots, routes or GPIO so far. */
    EPD_GPIO_Uninit();
    s_initialized=false;
    s_model=NULL;
    s_last_result=INKTAG_EPD_RESULT_NOT_READY;
    s_controller_temperature_valid = false;
    s_temperature_refresh_complete = false;
    if (!inktag_settings_screen_commit(screen)) {
        inktag_image_store_invalidate();
        inktag_tlsr_pin_routes_invalidate();
        s_profile=NULL;
        /* Report only the record observed by the commit adapter, never an
         * optimistic rollback. Hardware stays unavailable until normal init. */
        memset(&s_screen,0,sizeof(s_screen));
        if (inktag_settings_screen_valid())
            (void)epd_screen_config_decode(inktag_settings()->screen_config,
                EPD_SCREEN_WIRE_SIZE,&s_screen);
        return INKTAG_STATUS_PERSISTENCE_ERROR;
    }
    /* Both publications are no-IO/no-failure copies of preflight candidates.
     * In particular, do not rescan Flash or refresh routes after durability. */
    inktag_tlsr_pin_routes_apply(&routes);
    inktag_image_store_publish_profile(&images);
    s_screen = *screen;
    s_profile=inktag_tlsr_gpio_profile();
    return INKTAG_STATUS_OK;
}

#if INKTAG_EPD_TEMPERATURE_OPTION
void inktag_epd_init_with_temperature(bool read_temperature)
#else
void inktag_epd_init(void)
#endif
{
    /* gpio_init(false) rewrites the digital GPIO registers on every deep
     * retention wake.  Restore PA1 input and the panel bus before relying on
     * BUSY or emitting any SPI edge. */
    s_abort = false;
    EPD_BusyDiagReset();
    s_last_result = INKTAG_EPD_RESULT_OK;
    s_failed_command = 0u;
    s_controller_temperature = 0xFFu;
    s_controller_temperature_fraction = 0xFFu;
    s_controller_temperature_valid = false;
    s_temperature_refresh_complete = false;
    if (!s_profile || !s_profile->epd_busy_known || !inktag_settings_screen_valid()) {
        s_last_result = INKTAG_EPD_RESULT_NOT_READY;
        return;
    }
    if (!admit_panel_work()) {
        inktag_epd_sleep();
        return;
    }
    inktag_epd_restore_gpio();
#if INKTAG_EPD_TEMPERATURE_OPTION
    s_model = epd_init_with_temperature(&s_screen, read_temperature);
#else
    s_model = epd_init(&s_screen);
#endif
    if (s_model == NULL) s_last_result = INKTAG_EPD_RESULT_NOT_READY;
    else s_last_active_driver = s_model->driver_id;
    s_initialized = s_model != NULL && s_last_result == INKTAG_EPD_RESULT_OK;
#if INKTAG_EPD_OSPREY_E6_TEMPERATURE_READ && INKTAG_EPD_ENABLE_OSPREY_E6
#include "inktag_epd_extended_4_pro.inc"
#endif
    if (!s_initialized && s_panel_powered) {
        if (driver_owns_failure_sleep()) s_model->drv->sleep(s_model);
        EPD_GPIO_Uninit();
    }
}

#if INKTAG_EPD_TEMPERATURE_OPTION
void inktag_epd_init(void) {
    inktag_epd_init_with_temperature(true);
}
#endif

void inktag_epd_write_ram(bool begin, bool black, const uint8_t *data, uint16_t length) {
    tlsr_clock_invalidate();
    if (!s_initialized || s_model == NULL || data == NULL) return;
    if (!admit_panel_work()) return;
    if (s_last_result != INKTAG_EPD_RESULT_OK) return;
    while (length > 0u) {
        uint8_t chunk = length > UINT8_MAX ? UINT8_MAX : (uint8_t)length;
        if (!EPD_DriverWriteRam(s_model, begin, black,
                                (uint8_t *)data, chunk)) return;
        begin = false;
        data += chunk;
        length -= chunk;
    }
}

#if INKTAG_TLSR_NFC_FMSC_PILOT && INKTAG_BUILD_PRO
#include "inktag_epd_nfc_hook_3_pro.inc"
#endif

static void fill_canonical_plane(bool black, uint8_t value, uint32_t length) {
    uint8_t buffer[64];
    bool begin = true;
    memset(buffer, value, sizeof(buffer));
    while (length > 0u && !s_abort) {
        uint8_t chunk = length > sizeof(buffer) ? sizeof(buffer) : (uint8_t)length;
        inktag_epd_write_ram(begin, black, buffer, chunk);
        begin = false;
        length -= chunk;
    }
}

bool inktag_epd_clear(uint8_t color) {
    bool ok = inktag_epd_clear_pass(color);
    inktag_epd_sleep();
    return ok && s_last_result == INKTAG_EPD_RESULT_OK;
}

bool inktag_epd_clear_pass(uint8_t color) {
    tlsr_clock_invalidate();
    epd_screen_t resolved;
    uint32_t plane_bytes;
    bool hanshow_jd_powered_off = s_initialized && s_model != NULL &&
        s_model->driver_id == EPD_SCREEN_DRIVER_JD79668AA &&
        s_model->panel_recipe ==
            INKTAG_RECIPE_JD79668AA_HANSHOW_NEBULAR_420R_N;
    if (!admit_panel_work()) return false;

    /* Hanshow JD refresh finishes with POF. The next white/black/white pass
     * must repeat the exact reset/register/PON sequence before DTM1; keeping
     * s_initialized true only means the GPIO bus is open, not that the
     * controller's high-voltage rails are on. Other recipes keep their
     * existing multi-pass state and do not pay for an extra init. */
    if (!s_initialized || hanshow_jd_powered_off) inktag_epd_init();
    else s_abort = false;
    if (!s_initialized ||
        epd_screen_resolve(&s_screen, &resolved) != EPD_SCREEN_STATUS_OK) {
        if (s_last_result == INKTAG_EPD_RESULT_OK)
            s_last_result = INKTAG_EPD_RESULT_NOT_READY;
        return false;
    }
#if INKTAG_EPD_ENABLE_OSPREY_E6
#include "inktag_epd_extended_5_pro.inc"
#endif
    if (resolved.transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2BPP) {
        /* Public color IDs use black=0, white=1 and red=2. JD packed pixels
         * use codes black=0, white=1 and red=3. */
        fill_canonical_plane(true, color == 0u ? 0x00u :
                                   color == 2u ? 0xFFu : 0x55u,
                             resolved.frame_bytes);
    } else if (resolved.transfer_format == EPD_SCREEN_TRANSFER_DUAL_1BPP) {
        plane_bytes = resolved.frame_bytes / 2u;
        fill_canonical_plane(true, color == 0u ? 0x00u : 0xFFu, plane_bytes);
        fill_canonical_plane(false, color == 2u ? 0x00u : 0xFFu, plane_bytes);
    } else {
        fill_canonical_plane(true, color == 0u ? 0x00u : 0xFFu,
                             resolved.frame_bytes);
    }
    return inktag_epd_refresh();
}

bool inktag_epd_refresh(void) {
    tlsr_clock_invalidate();
    s_temperature_refresh_complete = false;
    if (!admit_panel_work()) return false;
    if (!s_initialized || s_model == NULL) {
        s_controller_temperature_valid = false;
        if (s_last_result == INKTAG_EPD_RESULT_OK)
            s_last_result = INKTAG_EPD_RESULT_NOT_READY;
        return false;
    }
    if (s_last_result != INKTAG_EPD_RESULT_OK) return false;
    s_model->drv->refresh(s_model);
    if (!EPD_DriverOperationOK(s_model) && s_last_result == INKTAG_EPD_RESULT_OK)
        s_last_result = INKTAG_EPD_RESULT_IO_ERROR;
    if (s_last_result != INKTAG_EPD_RESULT_OK) {
        s_controller_temperature_valid = false;
        if (driver_owns_failure_sleep())
            s_model->drv->sleep(s_model);
        else
            reset_panel(10u);
        s_initialized = false;
        return false;
    }
    s_temperature_refresh_complete = s_controller_temperature_valid;
    return true;
}

void inktag_epd_sleep(void) {
    if (s_initialized && s_model != NULL) {
        s_model->drv->sleep(s_model);
        if (!EPD_DriverOperationOK(s_model) && s_last_result == INKTAG_EPD_RESULT_OK)
            s_last_result = INKTAG_EPD_RESULT_IO_ERROR;
    }
    if (s_panel_powered) EPD_GPIO_Uninit();
    s_initialized = false;
    s_model = NULL;
}

bool inktag_epd_sample_temperature(void) {
    epd_model_t *model;
    /* Never reset a panel owned by a display/image transaction. */
    if (s_panel_powered) return false;
#if INKTAG_EPD_TEMPERATURE_OPTION
    if (inktag_nfc_image_active())
        return inktag_epd_controller_temperature_valid();
#endif
    /* Resolve metadata only: an unavailable sensor must not initialize a
     * removable panel merely because BLE notifications became usable. */
    model = epd_find(&s_screen);
    if (model == NULL || model->drv->read_temp == NULL) {
        s_controller_temperature_valid = false;
        s_temperature_refresh_complete = false;
        return false;
    }
#if INKTAG_EPD_TEMPERATURE_OPTION
    inktag_epd_init_with_temperature(true);
#else
    inktag_epd_init();
#endif
    if (s_initialized && !s_controller_temperature_valid &&
#if INKTAG_EPD_OSPREY_E6_TEMPERATURE_READ && INKTAG_EPD_ENABLE_OSPREY_E6
#include "inktag_epd_extended_6_pro.inc"
#endif
        s_model->drv->read_temp != NULL)
        (void)s_model->drv->read_temp(s_model);
    /* Ignore fixed/failure-blind driver scalar returns: only a checked wire
     * read may set the cache valid. Init may already have acquired it. */
    if (!s_initialized || !EPD_DriverOperationOK(s_model))
        s_controller_temperature_valid = false;
    inktag_epd_sleep();
    if (s_last_result != INKTAG_EPD_RESULT_OK)
        s_controller_temperature_valid = false;
    /* A completed acquisition, just like a completed refresh, can publish
     * the cache only after the panel has been returned to its idle state. */
    s_temperature_refresh_complete = s_controller_temperature_valid;
    return s_temperature_refresh_complete;
}

void inktag_epd_stop(void) {
    tlsr_clock_invalidate(); s_abort = true; }
bool inktag_epd_busy(void) { return s_panel_powered && busy_active(); }
bool inktag_epd_busy_raw(void) {
    return s_profile && gpio_read(s_profile->epd_busy) != 0;
}
uint8_t inktag_epd_active_driver_id(void) { return s_last_active_driver; }

inktag_epd_result_t inktag_epd_last_result(void) { return s_last_result; }
uint8_t inktag_epd_controller_temperature(void) { return s_controller_temperature; }
uint8_t inktag_epd_controller_temperature_fraction(void) {
    return s_controller_temperature_fraction;
}
bool inktag_epd_controller_temperature_valid(void) {
    if ((uint32_t)(inktag_app_monotonic_ms() -
                   s_controller_temperature_sample_ms) >
        PANEL_TEMPERATURE_MAX_AGE_MS)
        s_temperature_refresh_complete = false;
    return s_temperature_refresh_complete && s_controller_temperature_valid;
}

bool inktag_epd_panel_temperature_sixteenths(int16_t *value) {
    if (value == NULL || !inktag_epd_controller_temperature_valid()) return false;
    *value = s_controller_temperature_sixteenths;
    return true;
}

bool inktag_epd_effective_temperature(int8_t *celsius) {
    int8_t mcu_temperature = 0;
    bool mcu_valid = inktag_tlsr_read_die_temperature(&mcu_temperature, NULL);
    /* GUI uses this during the current powered init, before refresh has
     * completed. Status instead uses the completed-cache accessor above. */
    bool panel_valid = s_panel_powered ? s_controller_temperature_valid :
                                       inktag_epd_controller_temperature_valid();
    return inktag_temperature_select_lower(
        mcu_valid, (int16_t)mcu_temperature * 16,
        panel_valid, s_controller_temperature_sixteenths, celsius);
}
uint16_t inktag_epd_voltage(void) {
    uint16_t millivolts = 0u;

    /* Do not disturb the ADC GPIO while a panel transaction owns the board's
     * analog/power state. Destructive Flash paths use the same shared sampler
     * at their execution site after the display operation has completed. */
    if (s_panel_powered) return 0u;
    return inktag_tlsr_supply_voltage(&millivolts) ? millivolts : 0u;
}

typedef struct {
    bool black;
    bool begin;
    bool invert;
} gui_plane_t;

static void gui_plane(void *context, uint8_t *black, uint8_t *color,
                      uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    gui_plane_t *plane = (gui_plane_t *)context;
    uint32_t length = ((uint32_t)width + 7u) / 8u * height;
    (void)color; (void)x; (void)y;
    while (length && !s_abort) {
        uint16_t count = length > 244u ? 244u : (uint16_t)length;
        if (plane->invert) {
            for (uint16_t i = 0u; i < count; ++i) black[i] = (uint8_t)~black[i];
        }
        inktag_epd_write_ram(plane->begin, plane->black, black, count);
        plane->begin = false;
        black += count;
        length -= count;
    }
}

typedef struct {
    bool black;
    bool begin;
} gui_packed_plane_t;

static void gui_packed_plane(void *context, uint8_t *black, uint8_t *color,
                             uint16_t x, uint16_t y, uint16_t width,
                             uint16_t height) {
    gui_packed_plane_t *plane = (gui_packed_plane_t *)context;
    uint32_t length = (((uint32_t)width + 3u) / 4u) * height;
    (void)color; (void)x; (void)y;
    /* Packed indexed profiles stream one already-encoded 2-bpp page.  The
     * JD79665CA E6 recipe uses the same callback twice: Area0 is the driver's
     * black=true segment and Area6 is its black=false segment. */
    while (length > 0u && !s_abort) {
        uint8_t count = length > UINT8_MAX ? UINT8_MAX : (uint8_t)length;
        inktag_epd_write_ram(plane->begin, plane->black, black, count);
        plane->begin = false;
        black += count;
        length -= count;
    }
}

#if INKTAG_EPD_ENABLE_OSPREY_E6
#include "inktag_epd_extended_7_pro.inc"
#endif

// Qualified GR transaction adapted to TLSR bus, power and retained-history rules.
#include "inktag_tlsr_clock_400x300.inc"

bool inktag_epd_show_gui(display_mode_t mode, uint32_t timestamp, uint8_t week_start,
                         const gui_countdown_t *countdown,
                         const char *device_name) {
    gui_data_t data;
    gui_options_t options;
    epd_screen_t resolved;
    uint8_t device_info_qr[INKTAG_DEVICE_INFO_QR_MAX_BYTES];
    bool ok;
    uint32_t prepare_started;
    char screen_info[24];
    if (mode!=MODE_CLOCK) tlsr_clock_invalidate();
    if (!admit_panel_work()) return false;
    memset(&data, 0, sizeof(data));
    memset(&options, 0, sizeof(options));
    data.mode = mode;
    data.color = s_screen.color_id;
    data.width = s_screen.width;
    data.height = s_screen.height;
    data.visible_x = s_screen.visible_x;
    data.visible_y = s_screen.visible_y;
    data.visible_width = s_screen.visible_width;
    data.visible_height = s_screen.visible_height;
    data.force_rotation = true;
    data.rotation = (GFX_Rotate)(s_screen.transform & EPD_SCREEN_TRANSFORM_ROTATION_MASK);
    data.mirror_x = (s_screen.transform & EPD_SCREEN_TRANSFORM_MIRROR_X) != 0u;
    data.mirror_y = (s_screen.transform & EPD_SCREEN_TRANSFORM_MIRROR_Y) != 0u;
    data.native_ui_scale = s_screen.native_ui_scale;
    data.timestamp = timestamp;
    data.week_start = week_start;
    data.countdown = countdown;
    data.activated = inktag_app_activated();
    data.brand_hidden =
        inktag_app_brand_edition() == INKTAG_BRAND_EDITION_HIDDEN;
    data.temperature = GUI_TEMPERATURE_UNAVAILABLE;
    data.voltage = inktag_epd_voltage();
    GFX_format(screen_info, sizeof(screen_info), "%ux%u / %s", s_screen.width,
               s_screen.height, GUI_EpdColorName(s_screen.color_id));
    data.screen_info = screen_info;
    /* Keep actual GAP separate from the independent display Name. The caller
     * owns this snapshot for every synchronous paged render below. */
    data.bluetooth_name = device_name && device_name[0] ? device_name : "--";
    const uint8_t *mac = inktag_ble_mac();
    if (!GUI_SetDeviceName(&data, inktag_settings()->label,
            inktag_settings()->label_length,
            ((uint16_t)mac[1] << 8) | mac[0])) return false;
    data.device_name = data.ssid;
    data.mcu_name = "TLSR8359";
    if (mode == MODE_DEVICE_INFO) {
        if (inktag_tlsr_device_info_qr_read(
                device_info_qr, sizeof(device_info_qr),
                &data.device_info_qr_modules[0])) {
            data.device_info_qr_data[0] = device_info_qr;
        } else {
            data.device_info_qr_data[0] = inktag_default_device_qr;
            data.device_info_qr_modules[0] = INKTAG_DEFAULT_DEVICE_QR_MODULES;
        }
    }
    options.render_buffer = s_render_buffer;
    options.render_buffer_size = sizeof(s_render_buffer);
    s_abort = false;
    if (epd_screen_resolve(&s_screen, &resolved) != EPD_SCREEN_STATUS_OK) return false;
    s_gui_measure_stream = false;
    inktag_epd_init();
    if (!s_initialized || s_model == NULL) return false;
    /* Use the colder of the independently qualified MCU and panel sensors.
     * A failed source is ignored; the GUI sentinel remains when both fail. */
    (void)inktag_epd_effective_temperature(&data.temperature);
#if INKTAG_BUILD_PRO
#include "inktag_epd_clock_dispatch_pro.inc"
#endif
    tlsr_clock_invalidate();
    prepare_started = clock_time();
    s_gui_stream_ticks = s_gui_stream_bytes = s_gui_gap_ticks = s_gui_gap_bytes = 0u;
    s_gui_service_end = prepare_started;
    s_gui_measure_stream = true;
#if INKTAG_EPD_ENABLE_OSPREY_E6
#include "inktag_epd_extended_8_pro.inc"
#endif
    if (s_screen.driver_id == EPD_SCREEN_DRIVER_SSD1675) {
        gui_plane_t plane = {true, true, false};
        options.color_plane = GFX_COLOR_PLANE_BLACK;
        options.force_buffer_color = true;
        options.buffer_color = EPD_SCREEN_COLOR_BW;
        /* One 152x152 one-bit plane is 2888 bytes, so the shared 8 KiB
         * scratch buffer renders each black/red pass exactly once. */
        options.prefer_full_frame = true;
        ok = DrawGUIEx(&data, gui_plane, &plane, &options);
        if (ok && !s_abort) {
            plane.black = false; plane.begin = true;
            options.color_plane = GFX_COLOR_PLANE_RED;
            ok = DrawGUIEx(&data, gui_plane, &plane, &options);
        }
#if INKTAG_EPD_ENABLE_JD79665CA
#include "inktag_epd_extended_9_pro.inc"
#endif
    } else if (resolved.transfer_format == EPD_SCREEN_TRANSFER_PLANAR_2X1BPP) {
        /* SSD1677 gray RAM uses the inverted low bit in RAM1 and inverted high
         * bit in RAM2. Native-page accent colours follow the common one-bit
         * fallback and become pure black, so both bits use the same ALL-colour
         * render: non-white=00 and white=11. Render the complete stream twice
         * because each plane must remain contiguous and 48 KiB exceeds SRAM. */
        gui_plane_t plane = {true, true, true};
        options.color_plane = GFX_COLOR_PLANE_ALL;
        options.force_buffer_color = true;
        options.buffer_color = EPD_SCREEN_COLOR_BW;
        options.prefer_largest_page = true;
        ok = DrawGUIEx(&data, gui_plane, &plane, &options);
        if (ok && !s_abort) {
            plane.black = false; plane.begin = true;
            ok = DrawGUIEx(&data, gui_plane, &plane, &options);
        }
    } else if (resolved.transfer_format == EPD_SCREEN_TRANSFER_DUAL_1BPP) {
        gui_plane_t plane = {true, true, false};
        options.color_plane = GFX_COLOR_PLANE_BLACK;
        options.force_buffer_color = true;
        options.buffer_color = EPD_SCREEN_COLOR_BW;
        options.prefer_largest_page = true;
        ok = DrawGUIEx(&data, gui_plane, &plane, &options);
        if (ok && !s_abort) {
            plane.black = false; plane.begin = true;
            options.color_plane = GFX_COLOR_PLANE_RED;
            ok = DrawGUIEx(&data, gui_plane, &plane, &options);
        }
    } else if (resolved.transfer_format == EPD_SCREEN_TRANSFER_MONO_1BPP) {
        gui_plane_t plane = {true, true, false};
        options.color_plane = GFX_COLOR_PLANE_BLACK;
        options.force_buffer_color = true;
        options.buffer_color = EPD_SCREEN_COLOR_BW;
        options.prefer_largest_page = true;
        ok = DrawGUIEx(&data, gui_plane, &plane, &options);
    } else {
        gui_packed_plane_t plane = {true, true};
        options.color_plane = GFX_COLOR_PLANE_ALL;
        /* A BWR profile still needs a packed two-bit render for a JD DTM1
         * stream. The default three-colour renderer instead exposes two
         * separate one-bit buffers; sending only its first buffer truncated
         * the frame and decoded the background as red on Nebular-213R-N. */
        if (resolved.transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2BPP &&
            s_screen.color_id == EPD_SCREEN_COLOR_BWR) {
            options.force_buffer_color = true;
            options.buffer_color = EPD_SCREEN_COLOR_BWRY;
        }
        /* A 400x300 packed-2bpp frame is 30 KiB and would leave unsafe stack
         * headroom on TLSR8359. Use the full 8 KiB scratch page instead. */
        options.prefer_largest_page = true;
        ok = DrawGUIEx(&data, gui_packed_plane, &plane, &options);
    }
    gui_measure_gap(clock_time());
    s_gui_measure_stream = false;
    if (ok && !s_abort) {
        uint32_t prepare_ticks = clock_time() - prepare_started;
        /* Publish and drain the standard phase boundary before DRF starts.
         * The GUI stage is shared by calendar, clock and device-info because
         * all three enter through this renderer. */
        inktag_app_gui_refresh_phase(false);
        platform_cooperative_service();
        /* Give the established phase event its original drain opportunity
         * before optional timing telemetry competes for notification space. */
        inktag_app_gui_prepared(s_gui_gap_bytes,
            prepare_ticks / CLOCK_16M_SYS_TIMER_CLK_1MS,
            s_gui_stream_ticks / CLOCK_16M_SYS_TIMER_CLK_1MS, s_gui_stream_bytes,
            s_gui_gap_ticks / CLOCK_16M_SYS_TIMER_CLK_1MS);
        platform_cooperative_service();
        ok = inktag_epd_refresh();
    }
    if (!ok && s_last_result == INKTAG_EPD_RESULT_OK)
        s_last_result = INKTAG_EPD_RESULT_IO_ERROR;
    inktag_epd_sleep();
    return ok && !s_abort && s_last_result == INKTAG_EPD_RESULT_OK;
}

INKTAG_GUARD_C bool inktag_epd_show_stored(uint8_t record_id,
                            inktag_brand_edition_t edition) {
    tlsr_clock_invalidate();
    inktag_image_store_header_t header;
    uint8_t chunk[244];
    uint32_t offset = 0;
    uint8_t segment = 0;
    uint32_t segment_end;
    if (!admit_panel_work()) return false;
    if (inktag_image_store_get(record_id, &header) != INKTAG_IMAGE_STORE_OK ||
        !epd_screen_wire_image_compatible(
            header.screen_config, inktag_settings()->screen_config)) return false;
    s_abort = false;
    inktag_epd_init();
    if (!s_initialized || s_model == NULL) return false;
    segment_end = header.segment_size[0];
    while (offset < header.payload_size && !s_abort) {
        uint32_t segment_start = segment == 0u ? 0u : header.segment_size[0];
        uint16_t length = header.payload_size - offset > sizeof(chunk) ?
                          sizeof(chunk) : (uint16_t)(header.payload_size - offset);
        if (segment == 0u && offset + length > segment_end) length = (uint16_t)(segment_end - offset);
        if (inktag_image_store_read(record_id, offset, chunk, length) != INKTAG_IMAGE_STORE_OK) {
            s_last_result = INKTAG_EPD_RESULT_IO_ERROR;
            inktag_epd_sleep();
            return false;
        }
        /* The committed record and its CRC remain byte-exact. Apply the
         * product badge only to this writable Flash-read chunk immediately
         * before it is sent to controller RAM. */
        if (!inktag_brand_overlay_apply_oriented(
                &s_screen, header.format, segment, offset - segment_start,
                chunk, length, edition,
                inktag_image_reading_rotation(header.black_mask))) {
            s_last_result = INKTAG_EPD_RESULT_IO_ERROR;
            inktag_epd_sleep();
            return false;
        }
        inktag_epd_write_ram(offset == 0u || offset == segment_end,
                             segment == 0u, chunk, length);
        offset += length;
        if (offset == segment_end && header.segment_count > 1u) segment = 1u;
    }
    if (!s_abort && inktag_epd_refresh()) {
        inktag_epd_sleep();
        return s_last_result == INKTAG_EPD_RESULT_OK;
    }
    inktag_epd_sleep();
    return false;
}
