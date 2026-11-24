/* DA normal source projection v1 */
#include "user_custs1_impl.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "app.h"
#include "app_api.h"
#include "arch_system.h"
#include "custs1.h"
#include "custs1_task.h"
#include "arch_wdg.h"
#include "gap.h"
#include "gattc.h"
#include "ke_mem.h"
#include "lld_evt.h"
#include "prf_utils.h"
#include "rwip.h"
#include "user_custs1_def.h"
#include "user_peripheral.h"
#include "inktag_epd_config.h"
#include "inktag_epd_driver.h"
#include "inktag_gui.h"
#include "inktag_gui_assets.h"
#include "inktag_da_default_qr.h"
#include "inktag_protocol_v2.h"
#include "inktag_version.h"
#include "inktag_epd_wait_policy.h"
#include "inktag_epd_temperature.h"
#include "inktag_temperature_wire.h"
#include "inktag_board.h"
#include "inktag_brand_overlay.h"
#include "inktag_device_status.h"
#include "inktag_da_health.h"
#include "inktag_da_image_store.h"
#include "inktag_da_recovery.h"
#include "inktag_da_ota_policy.h"
#include "inktag_da_runtime_scratch.h"
#include "inktag_da_capabilities.h"
#include "inktag_da_transfer.h"
#include "inktag_packbits.h"
#include "inktag_lifecycle.h"
#include "inktag_display_settings.h"
#include "inktag_page_cycle.h"
#include "inktag_protocol_core.h"
#include "inktag_image_upload_v5.h"
#include "inktag_da_clock.h"
#include "inktag_da_pins.h"

/* Dialog persists the common display-settings wire record alongside the
 * common display state. Keep this port-only adapter out of inktag_epd_config.h so
 * MCU-independent EPD consumers do not inherit a protocol-header dependency. */
bool epd_dialog_display_settings_read(inktag_display_settings_t *settings);
bool epd_dialog_pin_defaults_load(epd_config_t *config);
bool epd_dialog_display_settings_write(
    const epd_display_state_t *state,
    const inktag_display_settings_t *settings);

#ifndef INKTAG_BOARD_DEFAULT_SCREEN_CONFIG
#define INKTAG_BOARD_DEFAULT_SCREEN_CONFIG \
    {EPD_SCREEN_SCHEMA_VERSION, EPD_SCREEN_DRIVER_UC8176, INKTAG_RECIPE_UC8176_STANDARD, EPD_SCREEN_COLOR_BWR, \
     0u, 400u, 300u, 0u, 0u, 400u, 300u}
#endif
#ifndef INKTAG_BOARD_DEFAULT_EPD_CONFIG
#define INKTAG_BOARD_DEFAULT_EPD_CONFIG \
    {0x00,0x04,0x01,0x02,0x05,0x06,0x07,0xFF,0xFF,0xFF,0xFF}
#endif
#ifndef INKTAG_BOARD_GUI_FRAME_BUFFER_BYTES
#define INKTAG_BOARD_GUI_FRAME_BUFFER_BYTES 3200
#endif
#ifndef INKTAG_BOARD_BATTERY_TELEMETRY_QUALIFIED
#define INKTAG_BOARD_BATTERY_TELEMETRY_QUALIFIED 0
#endif
#ifndef INKTAG_BOARD_DA_AESL42_BATTERY_TELEMETRY_QUALIFIED
#define INKTAG_BOARD_DA_AESL42_BATTERY_TELEMETRY_QUALIFIED 0
#endif
#ifndef INKTAG_BOARD_EXTENDED_SLEEP_QUALIFIED
#define INKTAG_BOARD_EXTENDED_SLEEP_QUALIFIED 0
#endif

/* Every public command runs from the outer SDK loop. This preserves the
 * DA14585 constraint that a GATT callback must not recurse rwip_schedule(). */
#define INKTAG_DA_IMAGE_QUEUE_DEPTH 1
#define INKTAG_DA_EPD_TRACE_DEPTH 8
#define INKTAG_DA_PROTOCOL_PACKET_BYTES 220u
#define INKTAG_DA_TRANSFER_WRITE_BYTES (INKTAG_DA_PROTOCOL_PACKET_BYTES - 7u)
/* SDK6 can lose a sustained run of near-MTU notifications while an external-
 * Flash read is active. Keep READ_RESOURCE at the previously proven 128-byte
 * notification size; upload still uses the full negotiated ingress packet. */
#define INKTAG_DA_READ_RESOURCE_BYTES 116u
#define INKTAG_DA_TRANSFER_LEASE_MS 30000u
#define INKTAG_DA_REFRESH_ASSERT_TIMEOUT_MS 3000u
#define INKTAG_DA_REFRESH_RELEASE_TIMEOUT_MS 210000u
#define INKTAG_DA_CLOCK_TICKS_PER_SECOND 1600u
/* DEVICE_INFO is the largest fixed control response. Keep a compile-time
 * lower bound when tuning the queue; transfer chunks may be smaller and are
 * negotiated through HELLO/CAPABILITIES. */
_Static_assert(INKTAG_DA_PROTOCOL_PACKET_BYTES >=
                   INKTAG_PROTOCOL_V2_HEADER_SIZE + 112u,
               "DA protocol packet cannot carry DEVICE_INFO");
_Static_assert(INKTAG_DA_READ_RESOURCE_BYTES <=
                   INKTAG_DA_PROTOCOL_PACKET_BYTES - 12u,
               "DA resource-read response exceeds protocol packet");

typedef enum __attribute__((packed)) {
    EPD_WORK_NONE = 0,
    EPD_WORK_CLEAR,
    EPD_WORK_REFRESH,
    EPD_WORK_GUI,
    EPD_WORK_IMAGE_SHOW,
    EPD_WORK_GHOST_CLEAN,
    EPD_WORK_TEMPERATURE,
} epd_work_type_t;

typedef struct {
    uint16_t length;
    uint8_t data[INKTAG_DA_PROTOCOL_PACKET_BYTES];
} packet_t;


/* V2 responses and events stay within the port's advertised 220-byte packet
 * limit. SDK6 owns the notification FIFO: CUSTS1_VAL_NTF_REQ returns
 * KE_MSG_SAVED while the profile is busy and resumes the saved request after
 * GATTC completion. Keeping a second application FIFO would duplicate the
 * same payloads in DA14585's code-in-SRAM window. */
static uint8_t s_connection_idx = GAP_INVALID_CONIDX;
static bool s_notify_enabled;
static bool s_in_gatt_callback;
static uint8_t s_notify_outstanding;
/* inktag_epd_driver.c emits exactly four BUSY stages on Dialog. Preserve the
 * full eight-event ring as two-bit codes instead of eight 32-bit pointers. */
static uint16_t s_epd_trace_queue;
static uint8_t s_epd_trace_head;
static uint8_t s_epd_trace_tail;
static uint8_t s_epd_trace_count;
static uint8_t s_runtime_board_kind;
/* Keep the single inbound packet in its own 222-byte application section. */
static packet_t s_image_queue[INKTAG_DA_IMAGE_QUEUE_DEPTH]
    __attribute__((section("free_area_3"), aligned(4)));
static uint8_t s_image_head;
static uint8_t s_image_tail;
static uint8_t s_image_count;
static bool s_image_processing;
/* Native GUI work is serialized. Keep its descriptive text outside the
 * already tight worker stack while retaining the complete common page. */
/* "65535x65535 / BWRYGB" plus NUL fits in 24 bytes. Keep this persistent
 * label bounded so the recovered SRAM remains available to the call stack. */
/* The SDK always retains the final RAM bank containing BLE/ROM data. Use its
 * bounded post-exchange pure-ZI gap, never the live SDK heap, for these App
 * objects. The linker enforces capacity and pure ZI so OTA has no high tail. */
static char s_gui_screen_info[24]
    __attribute__((section(".bss.inktag_free_area"), aligned(4)));
static int8_t s_panel_temperature;
static bool s_panel_temperature_valid;
static uint32_t s_panel_temperature_tick;
static uint32_t s_timestamp = 1735689600;
static int16_t s_timezone_minutes;
static epd_config_t s_config
    __attribute__((section(".bss.inktag_free_area"), aligned(4)));
static epd_config_t s_pin_defaults;
static bool s_pin_override;
static bool s_pin_unconfirmed;
static bool s_pin_reconcile_pending;
static bool s_pin_writes_allowed;
static epd_screen_config_t s_screen_config
    __attribute__((section(".bss.inktag_free_area"), aligned(4)));
static epd_display_state_t s_display_state
    __attribute__((section(".bss.inktag_free_area"), aligned(4)));
static inktag_display_settings_t s_display_settings
    __attribute__((section(".bss.inktag_free_area"), aligned(4)));
static bool s_display_state_loaded;
static bool s_display_state_persisted;
static bool s_boot_restore_handled;
static bool s_first_display_requested;
static bool s_ota_install_boot;
static epd_model_t *s_epd;
static bool s_epd_gpio_ready;
static epd_work_type_t s_work_type;
static uint8_t s_work_arg;
static uint32_t s_work_timestamp;
static bool s_work_pending;
static bool s_work_background;
static bool s_work_active;
static bool s_active_work_background;
static volatile bool s_cancel_requested;
static epd_work_type_t s_active_work_type;
static uint8_t s_active_work_arg;
static uint32_t s_active_work_timestamp;
static uint8_t s_ghost_pass;
static uint8_t s_ghost_result;
static bool s_refresh_waiting;
static bool s_refresh_seen_busy;
static uint32_t s_refresh_start_tick;
static uint32_t s_refresh_assert_ms;
static uint32_t s_refresh_busy_ms;
static bool s_picture_session_active;
static uint8_t s_pending_picture_store_id = UINT8_MAX;
static bool s_slideshow_active;
static uint32_t s_slideshow_interval = 3600u;
static uint32_t s_slideshow_next_at;
static uint8_t s_slideshow_current_id = UINT8_MAX;
static inktag_wall_clock_t s_wall_clock
    __attribute__((section(".bss.inktag_free_area"), aligned(4)));
/* Usable estimated time must keep normal pages/checkpoints running. UTC
 * calibration is separate and never inherited from a cold-boot record. */
static bool s_time_valid;
static bool s_time_calibrated;
static uint16_t s_time_checkpoint_elapsed;
static bool s_clock_minute_refresh_pending;
static bool s_clock_day_refresh_pending;
/* Volatile backoff only: a failed panel must not spin at every BLE wake, but
 * neither failure nor a legacy Flash flag may disable future automatic work. */
static uint32_t s_automatic_display_retry_tick;

/* Backport the sampling guard from 14a9e4e without its new clock/checkpoint
 * semantics. SDK6 also calls app_on_system_powered while BLE is asleep or
 * waking; SAMPLECLK waits for hardware and is only safe when ble_is_powered's
 * predicate holds. Reuse the last sample otherwise; the next powered sample
 * includes elapsed sleep. Never force a wake or weaken extended sleep. */
__attribute__((noinline)) static uint32_t inktag_da_now_tick(void)
{
    if (GetBits16(CLK_RADIO_REG, BLE_ENABLE) != 1 ||
        GetBits32(BLE_DEEPSLCNTL_REG, DEEP_SLEEP_STAT) != 0 ||
        (rwip_prevent_sleep_get() & RW_WAKE_UP_ONGOING) != 0)
        return s_wall_clock.last_tick;
    return lld_evt_time_get();
}
static inktag_da_transfer_t s_v2_transfer
    __attribute__((section(".bss.inktag_free_area"), aligned(4)));
static uint8_t s_v2_work_opcode;
static uint8_t s_v2_work_transaction;
static uint8_t s_system_action_pending;
static uint32_t s_system_action_tick;
/* The fixed engine's generated capture-header placement reuses the previous
 * 96-byte transaction scratch section, without a second arena or BLE heap. */
uint8_t inktag_da_gui_frame_buffer[INKTAG_BOARD_GUI_FRAME_BUFFER_BYTES]
    __attribute__((aligned(4)));
_Static_assert(INKTAG_DA_TRANSFER_SCRATCH_BYTES % sizeof(uint32_t) == 0u,
               "DA transfer scratch must remain word aligned");
_Static_assert(INKTAG_BOARD_GUI_FRAME_BUFFER_BYTES >=
                   INKTAG_DA_JD79XX_A0_STREAM_BYTES,
               "DA GUI buffer cannot reserve the Huawei A0 row state");
static void epd_gpio_open_if_needed(void);
static void epd_gpio_close_if_needed(void);
static void process_packet(uint8_t *data, uint16_t length);
static void service_clock(void);
static void v2_service_display_read(void);
static void v2_service(void);
__attribute__((noinline)) static void run_epd_work(void);
__attribute__((noinline)) static bool v2_display_busy(void);
static void service_pin_config(void);

/* Same 15-minute observation bound as shared GR/OM/TLSR. Use the wrapping
 * BLE tick, never the host-set wall clock. Expiry is sticky so an entire
 * hardware-counter period cannot revive an old powered-down sample. */
static bool panel_temperature_fresh_at(uint32_t now_tick)
{
    _Static_assert(BLE_BASETIMECNT_MASK <= UINT32_MAX / 5u &&
                   BLE_BASETIMECNT_MASK > 15u * 60u * 1600u,
                   "DA temperature age requires a bounded BLE counter");
    uint32_t ticks = (now_tick - s_panel_temperature_tick) & BLE_BASETIMECNT_MASK;
    s_panel_temperature_valid = inktag_epd_temperature_sample_fresh(
        s_panel_temperature_valid, 0u, (ticks * 5u) / 8u, 15u * 60u * 1000u);
    return s_panel_temperature_valid;
}

uint32_t timestamp(void) { return s_timestamp; }
void set_timestamp(uint32_t value)
{
    s_timestamp = value;
    s_time_valid = value >= 1704067200u && value <= 4102444800u;
    s_time_calibrated = s_time_valid;
    s_time_checkpoint_elapsed = 0u;
    inktag_wall_clock_reset(&s_wall_clock, inktag_da_now_tick());
    s_clock_minute_refresh_pending = false;
    s_clock_day_refresh_pending = false;
}
void app_feed_wdt(void)
{
    /* The SDK reloads its watchdog only when the outer main loop reaches the
     * sleep decision. EPD workers can intentionally stay powered longer. */
    wdg_reload(WATCHDOG_DEFAULT_PERIOD);
}
void app_service_ble(void)
{
    app_feed_wdt();
    if (!s_in_gatt_callback) {
        /* A disconnect may clean up panel/Flash ownership from this SDK
         * call, and a nested write must preserve this same exclusion. */
        s_in_gatt_callback = true;
        rwip_schedule();
        s_in_gatt_callback = false;
    }
}
void inktag_gui_cooperative_yield(void)
{
    app_service_ble();
    v2_service_display_read();
}
static uint32_t v2_elapsed_ms(uint32_t start_tick);

static bool epd_abort_requested(void) {
    return s_cancel_requested && !(s_active_work_type == EPD_WORK_GHOST_CLEAN &&
                                   s_ghost_pass == UINT8_MAX);
}

static uint16_t read_battery_voltage(void)
{
    /* Native pages show the stable idle/entry policy sample. Samples taken
     * after EPD init describe transient sag, not the battery icon voltage. */
    return inktag_da_health_policy_voltage_mv();
}

static const char *inktag_da_board_id(void)
{
    if (s_runtime_board_kind == INKTAG_BOARD_DA_AESL29_STOCK_SELECTOR)
        return INKTAG_BOARD_DA_AESL29_ID;
    if (s_runtime_board_kind == INKTAG_BOARD_DA_AESL42_STOCK_SELECTOR)
        return INKTAG_BOARD_DA_AESL42_ID;
    return INKTAG_BOARD_ID;
}

static const char *inktag_da_board_display_name(bool compact)
{
    if (s_runtime_board_kind == INKTAG_BOARD_DA_AESL29_STOCK_SELECTOR)
        return compact ? INKTAG_BOARD_DA_AESL29_DISPLAY_NAME_SHORT :
                         INKTAG_BOARD_DA_AESL29_DISPLAY_NAME;
    if (s_runtime_board_kind == INKTAG_BOARD_DA_AESL42_STOCK_SELECTOR)
        return compact ? INKTAG_BOARD_DA_AESL42_DISPLAY_NAME_SHORT :
                         INKTAG_BOARD_DA_AESL42_DISPLAY_NAME;
    /* An unknown selector cannot inherit either exact-board name. */
    return "da14585";
}

static bool inktag_da_battery_telemetry_available(void)
{
    return INKTAG_BOARD_BATTERY_TELEMETRY_QUALIFIED != 0 ||
           (INKTAG_BOARD_DA_AESL42_BATTERY_TELEMETRY_QUALIFIED != 0 &&
            s_runtime_board_kind == INKTAG_BOARD_DA_AESL42_STOCK_SELECTOR);
}

static bool inktag_da_voltage_telemetry_available(void)
{
    return inktag_da_battery_telemetry_available() ||
           (INKTAG_BOARD_DA_AESL29_VOLTAGE_TELEMETRY_AVAILABLE != 0 &&
            s_runtime_board_kind == INKTAG_BOARD_DA_AESL29_STOCK_SELECTOR);
}

static void notify_queue_reset(void)
{
    s_notify_outstanding = 0u;
    s_epd_trace_head = 0u;
    s_epd_trace_tail = 0u;
    s_epd_trace_count = 0u;
}

#include "inktag_da_notifications.inc"

static void put_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

/* One unaligned-safe encoder for the many variable32-bit response fields.
 * Outlining prevents LTO from repeating the same shifts/stores at each site. */
__attribute__((noinline)) static void put_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void send_public_failure_log(void)
{
    static const uint8_t message[] = "epd:failed";
    /* This Free diagnostic is best-effort and may never displace a response,
     * progress event or terminal completion from the bounded notify queue. */
    if (s_notify_outstanding != 0u) return;
    send_v2_event(INKTAG_EVENT_LOG, INKTAG_STATUS_OK, 0u, 0u,
                  message, (uint16_t)(sizeof(message) - 1u));
}

void app_epd_trace(const char *tag)
{
    uint8_t code;
    uint8_t shift;
    /* Deep controller calls cannot send a 128-byte protocol event on the
     * already tight renderer stack. inktag_epd_driver.c owns this hook and passes
     * only its four stable BUSY stages. Encode them here and emit the public
     * text from the outer SDK loop after the driver unwinds. */
    if (tag == NULL) {
        app_service_ble();
        return;
    }
    if (tag[5] == 'o')
        code = tag[6] == 'n' ? 0u : 2u;
    else if (tag[5] == 'n')
        code = 1u;
    else if (tag[5] == 'h')
        code = 3u;
    else {
        app_service_ble();
        return;
    }
    if (s_notify_enabled && s_connection_idx != GAP_INVALID_CONIDX) {
        if (s_epd_trace_count == INKTAG_DA_EPD_TRACE_DEPTH) {
            s_epd_trace_tail = (uint8_t)((s_epd_trace_tail + 1u) %
                                         INKTAG_DA_EPD_TRACE_DEPTH);
            s_epd_trace_count--;
        }
        shift = (uint8_t)(s_epd_trace_head * 2u);
        s_epd_trace_queue = (uint16_t)(
            (s_epd_trace_queue & (uint16_t)~(uint16_t)(3u << shift)) |
            (uint16_t)((uint16_t)code << shift));
        s_epd_trace_head = (uint8_t)((s_epd_trace_head + 1u) %
                                     INKTAG_DA_EPD_TRACE_DEPTH);
        s_epd_trace_count++;
    }
    app_service_ble();
}

__attribute__((noinline)) static bool send_pending_epd_trace(void)
{
    static const uint8_t trace_on[] = {'b','u','s','y',':','o','n'};
    static const uint8_t trace_no_assert[] = {
        'b','u','s','y',':','n','o','-','a','s','s','e','r','t'};
    static const uint8_t trace_off[] = {'b','u','s','y',':','o','f','f'};
    static const uint8_t trace_held[] = {'b','u','s','y',':','h','e','l','d'};
    const uint8_t *tag;
    uint16_t length;
    uint8_t code;
    if (s_epd_trace_count == 0u || s_notify_outstanding != 0u ||
        !s_notify_enabled || s_connection_idx == GAP_INVALID_CONIDX)
        return false;
    code = (uint8_t)((s_epd_trace_queue >> (s_epd_trace_tail * 2u)) & 3u);
    if (code == 0u) {
        tag = trace_on;
        length = 7u;
    } else if (code == 1u) {
        tag = trace_no_assert;
        length = 14u;
    } else if (code == 2u) {
        tag = trace_off;
        length = 8u;
    } else {
        tag = trace_held;
        length = 9u;
    }
    send_v2_event(INKTAG_EVENT_LOG, INKTAG_STATUS_OK, 0u, 0u,
                  tag, length);
    s_epd_trace_tail = (uint8_t)((s_epd_trace_tail + 1u) %
                                 INKTAG_DA_EPD_TRACE_DEPTH);
    s_epd_trace_count--;
    return true;
}

static uint32_t expected_frame_payload(void)
{
    epd_screen_t screen;
    return epd_screen_resolve(&s_screen_config, &screen) == EPD_SCREEN_STATUS_OK
        ? screen.frame_bytes : 0u;
}

static bool screen_config_matches(const uint8_t encoded[EPD_SCREEN_WIRE_SIZE])
{
    uint8_t current[EPD_SCREEN_WIRE_SIZE];
    return epd_screen_config_encode(&s_screen_config, current) == EPD_SCREEN_STATUS_OK &&
           memcmp(current, encoded, sizeof(current)) == 0;
}

/* DA14585 is permanently Free and exposes every record that its qualified
 * transactional extent can hold.  Keep the protocol-facing wrappers so the
 * call sites remain shared, but do not rescan the store to impose a second
 * commercial visibility layer. */
static inktag_image_store_result_t image_store_next_visible(
    uint8_t after_id, uint8_t *id, inktag_image_store_header_t *header);

static inktag_image_store_result_t image_store_next_visible(
    uint8_t after_id, uint8_t *id, inktag_image_store_header_t *header)
{
    return inktag_da_store_next(after_id, id, header);
}

static bool image_store_next_visible_compatible(uint8_t after_id,
                                                 uint8_t *id)
{
    return inktag_image_store_next_compatible(
               after_id, &s_screen_config, id) == INKTAG_IMAGE_STORE_OK;
}

static inktag_image_store_result_t image_store_visible_status(
    const epd_screen_config_t *screen, uint32_t payload_size,
    inktag_image_store_status_t *status)
{
    return inktag_image_store_status(screen, payload_size, status);
}

bool inktag_da_active_screen_config(epd_screen_config_t *out)
{
    if (out == NULL) return false;
    *out = s_screen_config;
    return s_screen_config.schema_version == EPD_SCREEN_SCHEMA_VERSION;
}

static inktag_image_store_result_t image_store_clear_visible(
    bool incompatible_only, uint16_t *removed)
{
    return inktag_da_store_clear(incompatible_only ? &s_screen_config : NULL, removed);
}

static void slideshow_stop(void)
{
    s_slideshow_active = false;
    s_slideshow_next_at = 0u;
    s_slideshow_current_id = UINT8_MAX;
}

static void load_device_settings(epd_device_settings_t *settings)
{
    /* The Dialog reader supplies defaults on every non-null failure path. */
    (void)epd_device_settings_read(settings);
    /* Empty/missing labels mean the factory display name, not the BLE name.
     * init_device_name() owns immutable IKT_DA_XXXX for both stock selectors.
     * Derive on read: no extra Flash record/write and no custom-label migration.
     * The length guard also keeps pre-initialization fallback reads bounded. */
    if (settings->label_length == 0u) {
        const char *name = inktag_da_device_name();
        if (strlen(name) == 11u) {
            /* Reuse the common formatter and native device-page format rather
             * than adding two expanded copies to the SRAM-resident image. */
            GFX_format((char *)settings->label, sizeof(settings->label),
                       "InkTag %s", name + 7u);
            settings->label_length = 11u;
        }
    }
}

static void image_queue_reset(void)
{
    /* A cooperative Flash/EPD handler may service the disconnect callback.
     * Drop pending packets, but retain the live slot until dispatch releases
     * it. Clearing its count here would underflow the outer pop and allow a
     * new connection to overwrite bytes still being decoded. */
    if (s_image_processing) {
        s_image_head = (uint8_t)((s_image_tail + 1u) % INKTAG_DA_IMAGE_QUEUE_DEPTH);
        s_image_count = 1u;
        return;
    }
    s_image_head = 0;
    s_image_tail = 0;
    s_image_count = 0;
}

static bool image_enqueue(const uint8_t *data, uint16_t length)
{
    if (length < 3 || length > INKTAG_DA_MAX_DATA_LEN ||
        s_image_count == INKTAG_DA_IMAGE_QUEUE_DEPTH) return false;
    packet_t *packet = &s_image_queue[s_image_head];
    packet->length = length;
    memcpy(packet->data, data, length);
    /* The GATT admission below checks release-v1 before queueing. Retain
     * semantic flags only; confirmation/power policy must not see the tag. */
    packet->data[2] &= INKTAG_RELEASE_SEMANTIC_FLAGS_MASK;
    s_image_head = (uint8_t)((s_image_head + 1) % INKTAG_DA_IMAGE_QUEUE_DEPTH);
    s_image_count++;
    return true;
}

static void epd_gpio_open_if_needed(void)
{
    if (!s_epd_gpio_ready) {
        inktag_da_health_display_active(true);
        EPD_GPIO_Init();
        s_epd_gpio_ready = true;
    }
}

static void epd_gpio_close_if_needed(void)
{
    if (s_epd_gpio_ready) {
        EPD_GPIO_Uninit();
        s_epd_gpio_ready = false;
        inktag_da_health_display_active(false);
    }
}

static bool epd_ensure_screen(void)
{
    if (s_pin_unconfirmed || s_config.mosi_pin == 0xffu) return false;
    if (!s_epd) s_epd = epd_find(&s_screen_config);
    return s_epd != NULL;
}

static inktag_status_v2_t epd_unavailable_status(void)
{
    return s_pin_unconfirmed ? INKTAG_STATUS_PERSISTENCE_ERROR :
        s_config.mosi_pin == 0xffu ? INKTAG_STATUS_UNSUPPORTED :
        INKTAG_STATUS_INCOMPATIBLE;
}

static const char *epd_gui_color_name(uint8_t color)
{
    if (color == COLOR_BW) return "BW";
    if (color == COLOR_BWR) return "BWR";
    if (color == COLOR_BWRY) return "BWRY";
    if (color == COLOR_BWRYGB) return "BWRYGB";
    return "COLOR";
}

static void epd_gui_screen_info_update(void)
{
    if (s_epd == NULL) {
        s_gui_screen_info[0] = '\0';
        return;
    }
    GFX_format(s_gui_screen_info, sizeof(s_gui_screen_info), "%ux%u / %s",
               s_epd->visible_width, s_epd->visible_height,
               epd_gui_color_name(s_epd->color));
}

static bool epd_init_with_load_check(void)
{
    s_epd->drv->init(s_epd);
    return inktag_da_health_operation_allowed(
        INKTAG_POWER_OPERATION_DISPLAY, false);
}

static void record_display_state(uint8_t mode, uint8_t argument)
{
    epd_display_state_t next;
    /* The 8-byte state has no padding; assign every field below. */
    next.mode = mode;
    next.argument = argument;
    next.reserved = s_display_state_loaded ? s_display_state.reserved : 0u;
    next.interval_seconds = mode == EPD_DISPLAY_STATE_SLIDESHOW ?
        s_slideshow_interval : 0u;
    s_config.display_mode = epd_display_state_driver_mode(mode);
    if (!s_display_state_persisted ||
        memcmp(&next, &s_display_state, sizeof(next)) != 0) {
        if (epd_dialog_display_settings_write(&next, &s_display_settings)) {
            s_display_state = next;
            s_display_state_loaded = true;
            s_display_state_persisted = true;
        }
    }
}

static bool automatic_display_refresh_allowed(void)
{
    if (s_automatic_display_retry_tick == 0u) return true;
    if (v2_elapsed_ms(s_automatic_display_retry_tick - 1u) < 60000u) return false;
    s_automatic_display_retry_tick = 0u;
    return true;
}

static void defer_automatic_display_work(void)
{
    /* The 27-bit counter plus one tags a zero sample without another bool.
     * Decode before v2_elapsed_ms; SET_TIME cannot extend this cooldown. */
    s_automatic_display_retry_tick = inktag_da_now_tick() + 1u;
    /* Clock mode already receives every next minute boundary. Calendar needs
     * to retain this day's due work instead of silently waiting another day.
     * A selected slideshow must never be replaced by a native-page retry. */
    if (!s_slideshow_active && s_config.display_mode == MODE_CALENDAR)
        s_clock_day_refresh_pending = true;
}

static void da_gui_write_ram_plane(void *user_data, uint8_t *black,
                                   uint8_t *color, uint16_t x, uint16_t y,
                                   uint16_t w, uint16_t h)
{
    (void)color;
    (void)x;

    bool begin = y == 0u;
    uint16_t length = (uint16_t)(((w + 7u) / 8u) * h);
    while (length > 0u) {
        uint8_t chunk = length > UINT8_MAX ? UINT8_MAX : (uint8_t)length;
        (void)EPD_DriverWriteRam(s_epd, begin, user_data == NULL,
                                 black, chunk);
        begin = false;
        black += chunk;
        length -= chunk;
    }
}

static bool panel_temperature_acquire(void)
{
    if (s_epd->driver_id == EPD_SCREEN_DRIVER_UC8176 ||
        s_epd->driver_id == EPD_SCREEN_DRIVER_UC8276) {
        uint8_t raw[2];
        int16_t value;
        /* Same synchronous R40/internal-sensor decode as the shared GR
         * consumer. No second PON/DRF, fixed wait or guessed BUSY cycle. */
        s_panel_temperature_valid = EPD_ReadCommand(UC8xxx_TSC, raw, sizeof(raw)) &&
            inktag_uc_internal_r40_temperature_sixteenths(true, raw, &value);
        if (s_panel_temperature_valid)
            s_panel_temperature = inktag_epd_temperature_floor_celsius(value);
    } else {
        s_panel_temperature_valid =
            JD79xxx_ReadTemperature(s_epd, &s_panel_temperature) ||
            SSD1xxx_ReadTemperature(s_epd, &s_panel_temperature);
    }
    if (s_panel_temperature_valid) s_panel_temperature_tick = inktag_da_now_tick();
    return s_panel_temperature_valid;
}


/* Keep settings-read scratch out of the live paged-render stack. */
__attribute__((noinline)) static void epd_gui_display_name(gui_data_t *data)
{
    epd_device_settings_t settings;
    load_device_settings(&settings);
    if (settings.label_length > 0u && settings.label_length <= EPD_DEVICE_LABEL_MAX_BYTES) {
        memcpy(data->ssid, settings.label, settings.label_length);
        data->ssid[settings.label_length] = '\0';
    }
}

__attribute__((noinline)) static bool epd_gui_update(display_mode_t mode, uint32_t draw_timestamp)
{
    bool result;
    uint32_t gui_buffer_size = INKTAG_BOARD_GUI_FRAME_BUFFER_BYTES;
    gui_countdown_t countdown = {
        .name = s_display_settings.name,
        .target_day = s_display_settings.target_day,
    };
    gui_data_t data = {
        .mode = mode,
        .color = s_epd->color,
        .width = s_epd->width,
        .height = s_epd->height,
#if defined(INKTAG_GUI_RAW_VIEWPORT)
        .visible_x = s_epd->visible_x,
        .visible_y = s_epd->visible_y,
#endif
        .visible_width = s_epd->visible_width,
        .visible_height = s_epd->visible_height,
        .force_rotation = true,
        .rotation = (GFX_Rotate)(s_epd->transform & EPD_SCREEN_TRANSFORM_ROTATION_MASK),
        .mirror_x = (s_epd->transform & EPD_SCREEN_TRANSFORM_MIRROR_X) != 0u,
        .mirror_y = (s_epd->transform & EPD_SCREEN_TRANSFORM_MIRROR_Y) != 0u,
        .native_ui_scale = s_screen_config.native_ui_scale,
        .timestamp = draw_timestamp,
        .week_start = s_config.week_start,
        .countdown = &countdown,
        .temperature = GUI_TEMPERATURE_UNAVAILABLE,
        .voltage = read_battery_voltage(),
        .activated = false,
        .brand_hidden = false,
        .board_name = inktag_da_board_display_name(false),
        .board_name_short = inktag_da_board_display_name(true),
        .screen_info = s_gui_screen_info,
        .bluetooth_name = inktag_da_device_name(),
        .mcu_name = "DA14585",
    };
    gui_options_t options = {
        .color_plane = GFX_COLOR_PLANE_ALL,
        .prefer_largest_page = true,
        .render_buffer = inktag_da_gui_frame_buffer,
        .render_buffer_size = gui_buffer_size,
    };
    if (panel_temperature_acquire()) data.temperature = s_panel_temperature;
    strncpy(data.ssid, inktag_da_device_name(), sizeof(data.ssid) - 1);
    data.ssid[sizeof(data.ssid) - 1] = '\0';
    if (mode == MODE_DEVICE_INFO) epd_gui_display_name(&data);
    /* Huawei A0's controller-row reorder state occupies the tail only while
     * that exact recipe is streaming. Other panels retain the complete board
     * GUI buffer; A0 keeps the remaining bytes without borrowing the transfer transaction scratch. */
    if (s_epd->driver_id == EPD_SCREEN_DRIVER_JD79665AA &&
        s_epd->panel_recipe == INKTAG_RECIPE_JD79665AA_HUAWEI_398_A0) {
        options.render_buffer_size =
            gui_buffer_size - INKTAG_DA_JD79XX_A0_STREAM_BYTES;
    }
    /* Keep GUI paging on the fixed post-boot scratch. Rendering runs while
     * connected and must not reserve most of the BLE message heap or make
     * page geometry depend on transient stack traffic. The common renderer
     * culls off-page text/QR work and services BLE/watchdog after every page. */
    if ((s_epd->ic == DRV_IC_UC8151 || s_epd->ic == DRV_IC_SSD1680) &&
        s_epd->color == COLOR_BWR) {
        uint8_t plane;
        options.force_buffer_color = true;
        options.buffer_color = COLOR_BW;
        for (plane = 0u; plane < 2u; ++plane) {
            options.color_plane = plane == 0u ?
                GFX_COLOR_PLANE_BLACK : GFX_COLOR_PLANE_RED;
            if (!DrawGUIEx(&data, da_gui_write_ram_plane,
                           (void *)(uintptr_t)plane, &options)) {
                result = false;
                goto done;
            }
            app_feed_wdt();
            if (s_cancel_requested) {
                result = false;
                goto done;
            }
        }
        result = true;
        goto done;
    }

    result = DrawGUIEx(&data, (buffer_callback)s_epd->drv->write_image,
                       s_epd, &options);
done:
    return result;
}

#include "inktag_da_reset_trace.inc"
#include "inktag_da_ghost.inc"

static bool schedule_epd_work(epd_work_type_t type, uint8_t arg,
                              bool background)
{
    if (v2_display_busy()) {
        return false;
    }
    if (!inktag_da_health_operation_allowed(
            INKTAG_POWER_OPERATION_DISPLAY, true)) {
        if (background) defer_automatic_display_work();
        return false;
    }
    s_work_type = type;
    s_work_arg = arg;
    s_work_background = background;
    s_work_pending = true;
    return true;
}

static void finish_epd_work(epd_work_type_t work_type, uint8_t work_arg,
                            bool work_success, bool refresh_ok)
{
    bool cancelled = epd_abort_requested() || EPD_AbortRequested();
    bool refresh_expected = work_success && (work_type == EPD_WORK_REFRESH ||
                            work_type == EPD_WORK_IMAGE_SHOW ||
                            work_type == EPD_WORK_GUI ||
                            work_type == EPD_WORK_GHOST_CLEAN ||
                            (work_type == EPD_WORK_CLEAR && work_arg != 0));
    bool spi_failed = !EPD_DialogSpiReady() || EPD_DialogSpiLastError() != 0;
    bool missing_busy_edge = refresh_expected && !cancelled && !s_refresh_seen_busy;
    bool refresh_failed = refresh_expected && !cancelled && !refresh_ok;
    inktag_status_v2_t status = cancelled ? INKTAG_STATUS_CANCELLED :
        (spi_failed ? INKTAG_STATUS_IO_ERROR :
         (missing_busy_edge || refresh_failed ? INKTAG_STATUS_TIMEOUT :
          (!work_success ? INKTAG_STATUS_INTERNAL_ERROR : INKTAG_STATUS_OK)));
    if (work_type == EPD_WORK_GHOST_CLEAN) {
        if (ghost_clean_continue(status)) {
            s_refresh_waiting = false;
            return;
        }
        if (s_ghost_pass == UINT8_MAX) status = (inktag_status_v2_t)s_ghost_result;
    }

    if (status != INKTAG_STATUS_OK) {
        send_public_failure_log();
        if (s_active_work_background && !cancelled)
            defer_automatic_display_work();
        EPD_Reset(true, 10);
    } else {
        if (work_type == EPD_WORK_CLEAR || work_type == EPD_WORK_GHOST_CLEAN) {
            record_display_state(EPD_DISPLAY_STATE_CLEAR,
                                 work_type == EPD_WORK_CLEAR ? work_arg : 1u);
        } else if (work_type == EPD_WORK_REFRESH ||
                   work_type == EPD_WORK_IMAGE_SHOW) {
            record_display_state(work_type == EPD_WORK_IMAGE_SHOW && s_slideshow_active ?
                                     EPD_DISPLAY_STATE_SLIDESHOW :
                                     EPD_DISPLAY_STATE_PICTURE,
                                 work_type == EPD_WORK_IMAGE_SHOW ? work_arg :
                                 s_pending_picture_store_id);
        } else if (work_type == EPD_WORK_GUI) {
            record_display_state(work_arg == MODE_DEVICE_INFO ? EPD_DISPLAY_STATE_DEVICE_INFO : work_arg,
                                 EPD_DISPLAY_STATE_NO_SOURCE);
        }
        if (s_epd) s_epd->drv->sleep(s_epd);
        delay(100);
    }

    epd_gpio_close_if_needed();
    if (work_type == EPD_WORK_REFRESH || work_type == EPD_WORK_IMAGE_SHOW) {
        s_picture_session_active = false;
        /* The shared RX queue also holds HELLO/STATUS received while a driver
         * cooperatively services BLE. Dispatch those after the worker unwinds;
         * completing a frame must not discard the next host's handshake. */
        if (work_type == EPD_WORK_REFRESH) s_pending_picture_store_id = UINT8_MAX;
    }
    if (work_type == EPD_WORK_IMAGE_SHOW && !cancelled && work_success && refresh_ok &&
        !spi_failed && !missing_busy_edge && !refresh_failed && s_slideshow_active) {
        s_slideshow_current_id = work_arg;
        s_slideshow_next_at = s_timestamp + s_slideshow_interval;
    }
    if (s_v2_work_transaction != 0u) {
        uint8_t payload[2] = {work_arg, 0u};
        uint8_t payload_length = 1u;
        if (work_type == EPD_WORK_GHOST_CLEAN) {
            /* Canonical DISPLAY_ACTION completion echoes action + cycles.
             * A one-byte worker argument is not a valid ghost-clean reply. */
            payload[0] = INKTAG_DISPLAY_ACTION_GHOST_CLEAN;
            payload[1] = work_arg;
            payload_length = 2u;
        }
        send_v2_event(INKTAG_EVENT_COMPLETE, status,
                      s_v2_work_opcode, s_v2_work_transaction,
                      payload, payload_length);
        s_v2_work_opcode = 0u;
        s_v2_work_transaction = 0u;
    }
    EPD_SetAbortCallback(NULL);
    s_refresh_waiting = false;
    s_refresh_seen_busy = false;
    s_refresh_start_tick = 0;
    s_refresh_assert_ms = 0;
    s_refresh_busy_ms = 0;
    s_active_work_type = EPD_WORK_NONE;
    s_active_work_arg = 0;
    s_active_work_timestamp = 0;
    s_active_work_background = false;
    s_work_active = false;
    s_cancel_requested = false;
    s_ghost_pass = 0u;
}

static uint32_t refresh_elapsed_ms(void)
{
    return v2_elapsed_ms(s_refresh_start_tick);
}

static bool epd_refresh_is_deferred(const epd_model_t *epd)
{
    if (epd == NULL) return false;
    /* The common driver and worker share the exact deferred-refresh selector. */
    if (SSD1xxx_RefreshDeferred(epd)) return true;
    if ((uint8_t)(epd->driver_id - EPD_SCREEN_DRIVER_UC8XXX) <=
        (EPD_SCREEN_DRIVER_UC8179 - EPD_SCREEN_DRIVER_UC8XXX)) return true;
    if (epd->driver_id == EPD_SCREEN_DRIVER_JD79665AA &&
        (uint8_t)(epd->panel_recipe -
                  INKTAG_RECIPE_JD79665AA_HUAWEI_398_A0) <= 1u) return true;
    return epd->driver_id == EPD_SCREEN_DRIVER_JD79668AA &&
           epd->panel_recipe ==
               INKTAG_RECIPE_JD79668AA_HANSHOW_NEBULAR_420R_N;
}

__attribute__((noinline)) static bool process_refresh_wait(void)
{
    if (!s_refresh_waiting) return false;
    app_feed_wdt();
    if (epd_abort_requested() || EPD_AbortRequested()) {
        finish_epd_work(s_active_work_type, s_active_work_arg, true, false);
        return false;
    }

    uint32_t elapsed_ms = refresh_elapsed_ms();
    /* BUSY polarity is controller-specific: SSD1xxx is active-high while
     * UC8xxx/JD79xxx are active-low.  The selected registered driver owns the
     * normalization. */
    bool busy = s_epd->drv->read_busy(s_epd);
    if (s_refresh_seen_busy) s_refresh_busy_ms = elapsed_ms - s_refresh_assert_ms;
    /* Equal levels mean still awaiting the next edge: assertion when both
     * are false, release when both are true. Share one deadline/failure path
     * instead of keeping two limits live across the driver's indirect call.
     * A33 retains1s assertion and a no-looser45s activation-to-release bound. */
    if (busy == s_refresh_seen_busy) {
        uint32_t limit = s_refresh_seen_busy ?
            INKTAG_DA_REFRESH_RELEASE_TIMEOUT_MS : INKTAG_DA_REFRESH_ASSERT_TIMEOUT_MS;
        if (SSD1xxx_RefreshDeferred(s_epd))
            limit = !s_refresh_seen_busy ? 1000u :
                (s_epd->driver_id == EPD_SCREEN_DRIVER_SSD1683 ? UINT16_MAX : 45000u);
        if (elapsed_ms >= limit) {
            finish_epd_work(s_active_work_type, s_active_work_arg, true, false);
            return false;
        }
    }
    if (!s_refresh_seen_busy) {
        if (busy) {
            s_refresh_seen_busy = true;
            s_refresh_assert_ms = elapsed_ms;
            s_refresh_busy_ms = 0;
        }
    } else if (!busy) {
        SSD1xxx_FinishDeferredRefresh(s_epd);
        finish_epd_work(s_active_work_type, s_active_work_arg, true, true);
        return false;
    }
    /* SDK events wake the next sample without spinning or releasing EPD pins. */
    return false;
}

__attribute__((noinline)) static bool image_store_show(uint8_t id)
{
    inktag_image_store_header_t header;
    uint8_t buffer[INKTAG_DA_PROTOCOL_PACKET_BYTES];
    uint32_t offset = 0u;
    uint8_t segment;
    if (inktag_image_store_get(id, &header) != INKTAG_IMAGE_STORE_OK ||
        header.content_type != INKTAG_IMAGE_CONTENT_IMAGE ||
        !screen_config_matches(header.screen_config)) return false;
    if (!epd_init_with_load_check()) return false;
    for (segment = 0u; segment < header.segment_count; ++segment) {
        uint32_t remaining = header.segment_size[segment];
        bool begin = true;
        bool black = (header.black_mask & (1u << segment)) != 0u;
        while (remaining > 0u) {
            uint16_t chunk = remaining > sizeof(buffer) ?
                sizeof(buffer) : (uint16_t)remaining;
            if (s_cancel_requested || EPD_AbortRequested() ||
                inktag_image_store_read(id, offset, buffer, chunk) !=
                    INKTAG_IMAGE_STORE_OK) return false;
            if (!inktag_brand_overlay_apply_oriented(
                    &s_screen_config, header.format, segment,
                    header.segment_size[segment] - remaining,
                    buffer, chunk, INKTAG_BRAND_EDITION_FREE,
                    inktag_image_reading_rotation(header.black_mask))) return false;
            if (!EPD_DriverWriteRam(
                    s_epd, begin, black, buffer, (uint8_t)chunk))
                return false;
            begin = false;
            offset += chunk;
            remaining -= chunk;
            app_service_ble();
        }
    }
    return true;
}


static void reject_epd_work(inktag_status_v2_t status)
{
    if (s_active_work_type == EPD_WORK_GHOST_CLEAN && s_ghost_pass == UINT8_MAX)
        status = (inktag_status_v2_t)s_ghost_result;
    if (s_v2_work_transaction != 0u) {
        send_v2_event(INKTAG_EVENT_COMPLETE, status,
                      s_v2_work_opcode, s_v2_work_transaction, NULL, 0u);
        s_v2_work_opcode = s_v2_work_transaction = 0u;
    }
    if (s_active_work_background && status != INKTAG_STATUS_CANCELLED)
        defer_automatic_display_work();
    epd_gpio_close_if_needed();
    EPD_SetAbortCallback(NULL);
    s_active_work_background = s_work_active = s_refresh_waiting = false;
    s_active_work_type = EPD_WORK_NONE;
    s_ghost_pass = 0u;
}

__attribute__((noinline)) static void run_epd_work(void)
{
    /* Snapshot pending work in the existing active-work state. These values
     * survive the asynchronous BUSY phase already, and keeping them out of
     * this deep renderer frame preserves the interrupt/indirect-call stack
     * reserve on the 96 KiB DA14585 SRAM target. */
    if (!s_work_active) {
        s_active_work_type = s_work_type;
        s_active_work_arg = s_work_arg;
        s_active_work_timestamp = s_work_timestamp;
        s_active_work_background = s_work_background;
        s_work_type = EPD_WORK_NONE;
        s_work_arg = 0;
        s_work_timestamp = 0;
        s_work_background = false;
        s_work_pending = false;
        s_work_active = true;
        s_cancel_requested = false;
        s_ghost_pass = 0u;
    }
    s_refresh_assert_ms = 0;
    s_refresh_busy_ms = 0;
    EPD_BusyDiagReset();
    s_refresh_seen_busy = false;

    /* This second, execution-point check rejects unavailable voltage for
     * queued background restores and timed pages too. Valid samples use the
     * common display threshold, deliberately zero: valid low voltage alone
     * does not block a refresh. */
    if (!inktag_da_health_operation_allowed(
            INKTAG_POWER_OPERATION_DISPLAY, true)) {
        reject_epd_work(INKTAG_STATUS_LOW_POWER);
        return;
    }

    app_service_ble();

    if (!epd_ensure_screen()) {
        reject_epd_work(epd_unavailable_status());
        return;
    }

    epd_gpio_open_if_needed();
    EPD_DialogDiagReset();
    EPD_SetAbortCallback(epd_abort_requested);
    bool work_success = true;


    switch (s_active_work_type) {
        case EPD_WORK_GHOST_CLEAN:
            work_success = epd_init_with_load_check() &&
                ghost_clean_write_solid(s_ghost_pass == UINT8_MAX || s_ghost_pass % 3u != 1u);
            if (work_success) s_epd->drv->refresh(s_epd);
            break;
        case EPD_WORK_CLEAR:
            if (!epd_init_with_load_check()) {
                work_success = false;
                break;
            }
            app_service_ble();
            s_epd->drv->clear(s_epd, false);
            app_service_ble();
            if (s_active_work_arg != 0) s_epd->drv->refresh(s_epd);
            break;
        case EPD_WORK_REFRESH:
            if ((!s_picture_session_active && !epd_init_with_load_check()) ||
                (s_picture_session_active &&
                 !inktag_da_health_operation_allowed(
                     INKTAG_POWER_OPERATION_DISPLAY, false))) {
                work_success = false;
                break;
            }
            app_service_ble();
            s_epd->drv->refresh(s_epd);
            break;
        case EPD_WORK_GUI:
            work_success = epd_init_with_load_check();
            if (work_success)
                work_success = epd_gui_update((display_mode_t)s_active_work_arg,
                                              s_active_work_timestamp);
            if (work_success) {
                /* Shared clients distinguish layout rendering from the
                 * controller waveform by this existing common stage tag. */
                static const uint8_t phase[] = "gui:refresh";
                if (s_v2_work_transaction != 0u)
                    send_v2_event(INKTAG_EVENT_LOG, INKTAG_STATUS_OK,
                        s_v2_work_opcode, s_v2_work_transaction,
                        phase, sizeof(phase) - 1u);
                app_service_ble();
                s_epd->drv->refresh(s_epd);
            }
            break;
        case EPD_WORK_IMAGE_SHOW:
            work_success = image_store_show(s_active_work_arg);
            if (work_success) {
                app_service_ble();
                s_epd->drv->refresh(s_epd);
            }
            break;
        case EPD_WORK_NONE:
        default:
            work_success = false;
            break;
    }

    bool refresh_expected = work_success &&
        (s_active_work_type == EPD_WORK_REFRESH ||
         s_active_work_type == EPD_WORK_IMAGE_SHOW ||
         s_active_work_type == EPD_WORK_GUI ||
         s_active_work_type == EPD_WORK_GHOST_CLEAN ||
         (s_active_work_type == EPD_WORK_CLEAR && s_active_work_arg != 0));
    if (refresh_expected && work_success && !epd_abort_requested()) {
        if (epd_refresh_is_deferred(s_epd)) {
            s_refresh_waiting = true;
            s_refresh_seen_busy = false;
            s_refresh_start_tick = inktag_da_now_tick();
            s_refresh_assert_ms = 0;
            s_refresh_busy_ms = 0;
            return;
        }
        /* SSD1xxx and the remaining JD79xxx recipes already waited for the
         * complete refresh inside their common driver. Do not wait for a
         * second BUSY edge and turn a visible success into status=TIMEOUT. */
        s_refresh_busy_ms = EPD_BusyDiagActiveMs();
        s_refresh_seen_busy = s_refresh_busy_ms != 0u;
        finish_epd_work(s_active_work_type, s_active_work_arg, true,
                        !s_epd->drv->read_busy(s_epd));
        return;
    }
    finish_epd_work(s_active_work_type, s_active_work_arg, work_success, true);
}

static void service_clock(void)
{
    uint32_t now_tick = inktag_da_now_tick();
    (void)panel_temperature_fresh_at(now_tick);
    inktag_wall_clock_sample_t sample = inktag_da_clock_sample(
        &s_wall_clock, now_tick, &s_timestamp);
    if (!s_time_valid) {
        s_timestamp = 0u;
        return;
    }
    uint32_t elapsed = s_time_checkpoint_elapsed + sample.elapsed_seconds;
    s_time_checkpoint_elapsed = elapsed < INKTAG_DA_TIME_CHECKPOINT_SECONDS ?
        (uint16_t)elapsed : INKTAG_DA_TIME_CHECKPOINT_SECONDS;
    if (sample.minute_crossed) s_clock_minute_refresh_pending = true;
    if (sample.day_crossed) s_clock_day_refresh_pending = true;
}

static void service_timed_display(void)
{
    if (!s_time_valid) return;
    uint8_t mode = s_config.display_mode;
    bool due;

    if (mode == MODE_CLOCK) {
        due = s_clock_minute_refresh_pending;
        s_clock_day_refresh_pending = false;
    } else if (mode == MODE_CALENDAR) {
        due = s_clock_day_refresh_pending;
        s_clock_minute_refresh_pending = false;
    } else {
        s_clock_minute_refresh_pending = false;
        s_clock_day_refresh_pending = false;
        return;
    }
    if (!due || !automatic_display_refresh_allowed() ||
        v2_display_busy() ||
        s_picture_session_active || s_v2_transfer.active || s_image_count != 0u) return;

    s_work_timestamp = s_timestamp;
    if (!schedule_epd_work(EPD_WORK_GUI, mode, true)) return;
    if (mode == MODE_CLOCK) s_clock_minute_refresh_pending = false;
    else s_clock_day_refresh_pending = false;
}

__attribute__((noinline)) static bool service_idle_display_work(void)
{
    if (!s_v2_transfer.active && automatic_display_refresh_allowed() &&
        s_slideshow_active && s_slideshow_next_at != 0u &&
        (int32_t)(s_timestamp - s_slideshow_next_at) >= 0) {
        uint8_t next = UINT8_MAX;
        if (image_store_next_visible_compatible(
                s_slideshow_current_id, &next)) {
            return schedule_epd_work(EPD_WORK_IMAGE_SHOW, next, true);
        }
        /* An unreadable/incompatible slot is not an explicit user STOP.
         * Keep the selected mode and retry a bounded lookup later. */
        defer_automatic_display_work();
    }
    /* Emit one deferred trace only after the render/Flash stack has fully
     * unwound. */
    return send_pending_epd_trace();
}

__attribute__((noinline)) static void service_time_checkpoint(void)
{
    if (!s_time_valid ||
        s_time_checkpoint_elapsed < INKTAG_DA_TIME_CHECKPOINT_SECONDS ||
        s_epd_gpio_ready || s_work_active || s_v2_transfer.active) return;
    /* Idle-only append to the existing wear-levelled two-page Settings
     * journal. Never write per second, during EPD/transfer work, or in an IRQ.
     * Flash I/O keeps its physical voltage gate and bounded watchdog wait.
     * A rejected/failed append retries after another minute of elapsed time,
     * not on every 1-second advertising wake. */
    s_time_checkpoint_elapsed = epd_time_state_write(
        s_timestamp, s_timezone_minutes) ? 0u :
        INKTAG_DA_TIME_CHECKPOINT_SECONDS - 60u;
}

__attribute__((noinline)) bool inktag_da_process(void)
{
    /* A successful settings reset must not be repopulated by a queued write
     * or automatic display persistence while its reply drains before reboot. */
    if (s_system_action_pending != 0u) {
        v2_service();
        return true;
    }
    service_clock();
    service_pin_config();
    service_timed_display();
    v2_service();
    /* A received CANCEL wakes the SDK through BLE. Drain that command before
     * sampling BUSY so cancel latency remains bounded by the common 100 ms
     * policy instead of the panel's full refresh duration. */
    (void)INKTAG_EPD_WAIT_CANCEL_MAX_MS;
    if (s_image_count != 0u) {
        packet_t *packet = &s_image_queue[s_image_tail];
        /* Keep the slot occupied while processing. Cooperative BLE service
         * may run from Flash/display code; a nested write must not overwrite
         * the packet currently being decoded. */
        s_image_processing = true;
        process_packet(packet->data, packet->length);
        s_image_processing = false;
        s_image_tail = (uint8_t)((s_image_tail + 1u) %
                                 INKTAG_DA_IMAGE_QUEUE_DEPTH);
        s_image_count--;
        return s_image_count != 0 || s_work_pending || s_refresh_waiting;
    }
    if (s_refresh_waiting) return process_refresh_wait();
    if (s_work_pending || (s_work_active && s_active_work_type == EPD_WORK_GHOST_CLEAN)) {
        run_epd_work();
        return s_image_count != 0 || s_work_pending || s_refresh_waiting || s_work_active;
    }
    /* Emit one deferred trace only after the render/Flash stack has fully
     * unwound. Keeping this as the tail path avoids adding the trace sender's
     * frame to the renderer's static maximum-stack chain. */
    service_time_checkpoint();
    return service_idle_display_work();
}

bool inktag_da_allows_extended_sleep(void)
{
    /* mode_idle still reaches the SDK's interrupt-safe WFI path while keeping
     * BLE scheduling and direct SWD attach alive. */
    return false;
}

__attribute__((noinline)) static void load_default_config_if_needed(void)
{
    epd_screen_config_t default_screen = INKTAG_BOARD_DEFAULT_SCREEN_CONFIG;

    /* Load actual defaults directly. The legacy common RAM-cache API remains
     * in source; the DA application has no consumer for a second mirror. */
    s_pin_unconfirmed = !epd_dialog_pin_config_load(
        &s_pin_defaults, &s_config, &s_pin_override);
    s_pin_writes_allowed = inktag_da_pin_epoch_confirmed();
    if (s_pin_override && !s_pin_writes_allowed) s_pin_unconfirmed = true;
    if (s_pin_unconfirmed) memset(&s_config, 0xff, sizeof(s_config));
    s_display_state_persisted = epd_display_state_read(&s_display_state);
    if (!s_display_state_persisted) {
        inktag_display_state_default(&s_display_state);
    }
    s_display_state_loaded = true;
    memset(&s_display_settings, 0, sizeof(s_display_settings));
    if (!epd_dialog_display_settings_read(&s_display_settings)) {
        s_display_settings.week_start = INKTAG_DISPLAY_SETTINGS_DEFAULT_WEEK_START;
    }
    /* DA14585 is permanently Free-only. A stale/corrupt Pro visibility bit
     * can never become effective merely because it reached persistent Flash. */
    s_display_settings.flags &=
        (uint8_t)~INKTAG_DISPLAY_SETTINGS_FLAG_BRAND_HIDDEN;
    s_display_state.reserved &=
        (uint16_t)~EPD_DISPLAY_STATE_FLAG_BRAND_HIDDEN;
    s_config.week_start = s_display_settings.week_start <= 1u ?
        s_display_settings.week_start : INKTAG_DISPLAY_SETTINGS_DEFAULT_WEEK_START;
    epd_config_write(&s_config);
    s_config.display_mode = epd_display_state_driver_mode(s_display_state.mode);
    if (!epd_screen_config_read(&s_screen_config) || epd_find(&s_screen_config) == NULL) {
        s_screen_config = default_screen;
        (void)epd_screen_config_write(&s_screen_config);
    }
    s_epd = epd_find(&s_screen_config);
    epd_gui_screen_info_update();
    EPD_GPIO_Load(&s_config);
}


#include "inktag_da_protocol_v2.inc"

__attribute__((noinline)) void inktag_da_init(void)
{
    uint8_t factory_selector = 0xFFu;
    /* Capture policy before Recovery/settings Flash access or any EPD GPIO
     * and high-voltage activity. */
    inktag_da_health_init();
    inktag_recovery_result_t recovery =
        inktag_da_recovery_boot(&s_ota_install_boot);
    if (recovery == INKTAG_RECOVERY_ENTER ||
        recovery == INKTAG_RECOVERY_IO_ERROR ||
        recovery == INKTAG_RECOVERY_VERIFY_ERROR) {
        /* A second unconfirmed boot or unreadable control state fails closed
         * into the fixed BLE Recovery application. */
        if (inktag_da_enter_fixed_recovery())
            platform_reset(RESET_AFTER_SUOTA_UPDATE);
        return;
    }
    s_first_display_requested =
        inktag_da_first_display_requested(&factory_selector);
    s_runtime_board_kind = factory_selector;
    load_default_config_if_needed();
    /* Read headers only; old image formats are invisible, never auto-erased. */
    (void)inktag_image_store_init();
    notify_queue_reset();
    image_queue_reset();
    (void)inktag_wall_clock_init(&s_wall_clock, inktag_da_now_tick(),
                                 INKTAG_DA_CLOCK_TICKS_PER_SECOND,
                                 BLE_BASETIMECNT_MASK);
    s_time_valid = epd_time_state_read(&s_timestamp, &s_timezone_minutes);
    if (!s_time_valid) s_timestamp = 0u;
    /* The BLE counter is reinitialized by SDK boot. Neither a Flash record
     * nor a RAM cookie proves continuity across cold power loss or this
     * secondary-loader chain. Restore the last checkpoint and timezone, but
     * keep it uncalibrated until ordinary explicit SET_TIME succeeds. The
     * external trust-state wire field must use integration's common contract. */
    s_time_calibrated = false;
    reset_trace_arm();
}

bool inktag_da_connection_reached(void)
{
    inktag_recovery_result_t recovery = inktag_da_recovery_confirm_epoch(
        INKTAG_DA_OTA_COMPATIBILITY_EPOCH);
    if (recovery != INKTAG_RECOVERY_OK && recovery != INKTAG_RECOVERY_NOT_PENDING) {
        if (inktag_da_enter_fixed_recovery())
            platform_reset(RESET_AFTER_SUOTA_UPDATE);
        return false;
    }
    /* confirm_epoch alone does not upgrade an already-confirmed old floor.
     * The exact journal proof includes CONFIRMED and both epoch comparisons;
     * never infer the durable floor from the App's compiled policy. */
    s_pin_writes_allowed = inktag_da_pin_epoch_confirmed();
    return true;
}

void inktag_da_startup_ready(void)
{
    inktag_boot_display_plan_t plan;
    if (s_boot_restore_handled || !s_display_state_loaded) return;
    s_boot_restore_handled = true;
    plan = inktag_boot_display_plan(
        s_first_display_requested, s_ota_install_boot,
        s_display_state_persisted ? &s_display_state : NULL);
    if (plan.consume_first_display_request) {
        if (!inktag_da_first_display_consume()) return;
        s_first_display_requested = false;
    }
    if (!plan.defer_until_startup_complete) return;
    /* Installation information does not require a synchronized clock, even
     * when the default/saved replay mode is calendar or clock. */
    if (plan.target == INKTAG_BOOT_DISPLAY_DEVICE_INFO) {
        s_work_timestamp = s_timestamp;
        (void)schedule_epd_work(EPD_WORK_GUI, MODE_DEVICE_INFO, true);
        return;
    }
    if (plan.target != INKTAG_BOOT_DISPLAY_REPLAY) return;
    if (!s_time_valid &&
        (s_display_state.mode == EPD_DISPLAY_STATE_CALENDAR ||
         s_display_state.mode == EPD_DISPLAY_STATE_CLOCK)) return;
    if (s_display_state.mode == EPD_DISPLAY_STATE_CALENDAR ||
        s_display_state.mode == EPD_DISPLAY_STATE_CLOCK) {
        s_work_timestamp = s_timestamp;
        schedule_epd_work(EPD_WORK_GUI, s_display_state.mode, true);
    } else if (s_display_state.mode == EPD_DISPLAY_STATE_DEVICE_INFO) {
        s_work_timestamp = s_timestamp;
        schedule_epd_work(EPD_WORK_GUI, MODE_DEVICE_INFO, true);
    } else if (s_display_state.mode == EPD_DISPLAY_STATE_CLEAR) {
        uint8_t clear_white = s_display_state.argument <= 1u ? s_display_state.argument : 1u;
        schedule_epd_work(EPD_WORK_CLEAR, clear_white, true);
    } else if (s_display_state.mode == EPD_DISPLAY_STATE_SLIDESHOW) {
        uint8_t id = s_display_state.argument;
        if (s_display_state.interval_seconds >= 30u &&
            s_display_state.interval_seconds <= 86400u &&
            v2_resource_replayable(id)) {
            s_slideshow_active = true;
            s_slideshow_interval = s_display_state.interval_seconds;
            s_slideshow_current_id = id;
            s_slideshow_next_at = s_timestamp + s_slideshow_interval;
            schedule_epd_work(EPD_WORK_IMAGE_SHOW, id, true);
        }
    } else if (s_display_state.mode == EPD_DISPLAY_STATE_PICTURE &&
               s_display_state.argument != EPD_DISPLAY_STATE_NO_SOURCE) {
        if (v2_resource_replayable(s_display_state.argument)) {
            schedule_epd_work(EPD_WORK_IMAGE_SHOW, s_display_state.argument,
                              true);
        }
    }
}

void inktag_da_connected(uint8_t connection_idx)
{
    s_connection_idx = connection_idx;
    s_pin_reconcile_pending = s_pin_unconfirmed;
    notify_queue_reset();
}

void inktag_da_disconnected(void)
{
    const bool previous_callback = s_in_gatt_callback;
    s_in_gatt_callback = true;
    /* A diagnostic peer may disconnect while cooperative BLE service runs
     * inside an autonomous slideshow/calendar worker. Drop the peer's
     * transfer and replies, not that device-owned display operation. Never
     * clear an explicit CANCEL that the worker has already observed. */
    bool background_work = (s_work_pending && s_work_background) ||
        ((s_work_active || s_refresh_waiting) && s_active_work_background);
    if (inktag_image_store_capture_active()) inktag_da_store_abort();
    if (!background_work) s_cancel_requested = true;
    if (s_work_pending && !s_work_background) {
        s_work_pending = false;
        s_work_type = EPD_WORK_NONE;
    }
    s_v2_work_opcode = 0u;
    s_v2_work_transaction = 0u;
    image_queue_reset();
    s_picture_session_active = false;
    v2_transfer_reset(true);
    if (!s_work_active && !s_refresh_waiting && !s_work_pending)
        epd_gpio_close_if_needed();
    s_connection_idx = GAP_INVALID_CONIDX;
    s_notify_enabled = false;
    notify_queue_reset();
    s_in_gatt_callback = previous_callback;
}

void inktag_da_write_value(uint8_t connection_idx, uint16_t handle,
                           const uint8_t *value, uint16_t length)
{
    /* Borrowed GATT bytes are copied only into the bounded App queue. Do not
     * run protocol/Flash/EPD work here or retain the SDK message pointer.
     * SDK attribute validation and its ATT confirmation remain in charge. */
    if (connection_idx != s_connection_idx ||
        connection_idx == GAP_INVALID_CONIDX) return;
    if (handle == INKTAG_IDX_EPD_NTF_CFG) {
        /* Screen state is returned only by V2 GET_SCREEN_PROFILE. */
        s_notify_enabled = length >= 2 && (value[0] & 0x01);
        return;
    }
    if (handle == INKTAG_IDX_EPD_VAL) {
        const bool previous_callback = s_in_gatt_callback;
        uint8_t semantic_flags;
        inktag_status_v2_t status;
        s_in_gatt_callback = true;
        if (length < INKTAG_PROTOCOL_V2_HEADER_SIZE ||
            length > INKTAG_DA_PROTOCOL_PACKET_BYTES ||
            value[1] == 0u ||
            !inktag_release_request_flags(value[2], &semantic_flags)) {
            status = INKTAG_STATUS_INVALID_REQUEST;
        } else if (v2_free_only_request_unsupported(value[0], semantic_flags,
                    value + INKTAG_PROTOCOL_V2_HEADER_SIZE,
                    (uint16_t)(length - INKTAG_PROTOCOL_V2_HEADER_SIZE))) {
            /* Refuse omitted facilities before the bounded queue can be
             * consumed or report BUSY. DA has no password/owner AUTH backend. */
            status = INKTAG_STATUS_UNSUPPORTED;
        } else if (image_enqueue(value, length)) {
            s_in_gatt_callback = previous_callback;
            return;
        } else {
            status = INKTAG_STATUS_BUSY;
        }
        if (length >= 2u) send_v2_status(value[0], value[1], status);
        s_in_gatt_callback = previous_callback;
    }
}

void inktag_da_write_handler(ke_msg_id_t const msgid,
                             struct custs1_val_write_ind const *param,
                             ke_task_id_t const dest_id,
                             ke_task_id_t const src_id)
{
    (void)msgid;
    (void)dest_id;
    (void)src_id;
    inktag_da_write_value(param->conidx, param->handle, param->value, param->length);
}

void inktag_da_notify_cfm_handler(struct custs1_val_ntf_cfm const *param)
{
    if (param->handle != INKTAG_IDX_EPD_VAL) return;
    if (s_notify_outstanding != 0u) s_notify_outstanding--;
}

void inktag_da_att_info_req_handler(ke_msg_id_t const msgid,
                                    struct custs1_att_info_req const *param,
                                    ke_task_id_t const dest_id,
                                    ke_task_id_t const src_id)
{
    (void)msgid;
    struct custs1_att_info_rsp *rsp = KE_MSG_ALLOC(CUSTS1_ATT_INFO_RSP,
                                                   src_id,
                                                   dest_id,
                                                   custs1_att_info_rsp);
    rsp->conidx = app_env[param->conidx].conidx;
    rsp->att_idx = param->att_idx;
    rsp->length = 0;
    rsp->status = (param->att_idx == INKTAG_IDX_EPD_VAL || param->att_idx == INKTAG_IDX_EPD_NTF_CFG)
                    ? ATT_ERR_NO_ERROR
                    : ATT_ERR_WRITE_NOT_PERMITTED;
    KE_MSG_SEND(rsp);
}
