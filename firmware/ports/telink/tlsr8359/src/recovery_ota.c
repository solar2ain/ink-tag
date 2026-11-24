#include "recovery.h"

#include <string.h>

#include "inktag_recovery.h"
#include "inktag_code_guard.h"
#include "inktag_status.h"
#include "inktag_tlsr_recovery.h"
#include "inktag_tlsr_ota_policy.h"
#include "inktag_tlsr_power.h"
#include "inktag_tlsr_gpio_func.h"
#include "inktag_tlsr_recovery_pins.h"
#include "stack/ble/ble.h"
#include "../trusted-entry/image_verify.h"

#define OTA_BLOCK_SIZE 16u
/* Existing signed Pro transport adds this tail; CRC-only InkTag sends none. */
#define OTA_LEGACY_SIGNATURE_BYTES 96u
#define OTA_FLASH_SECTOR_SIZE 0x1000u
#define OTA_MIN_DATA_PACKET_SIZE 20u
#define OTA_DATA_PAYLOAD_SIZE \
    (INKTAG_RECOVERY_OTA_FAST_MAX_BLOCKS * OTA_BLOCK_SIZE)
#define OTA_DATA_PACKET_SIZE (4u + OTA_DATA_PAYLOAD_SIZE)
#define OTA_END_PACKET_SIZE 6u
/* Two entries match the host's bounded transfer window. Keep an explicit
 * count instead of sacrificing one ring slot so the retained queue contains
 * no third, unreachable packet buffer. Flash work remains outside GATT
 * callbacks and advances one 16-byte block per loop. */
#define OTA_QUEUE_SLOTS 2u
#define RECOVERY_LED_TICK_MS 120u
#define RECOVERY_LED_TRANSFER_HALF_TICKS 2u
#define RECOVERY_LED_STARTUP_COLOR_TICKS \
    (INKTAG_STATUS_STARTUP_COLOR_MS / RECOVERY_LED_TICK_MS)
#define RECOVERY_LED_READY_HALF_TICKS \
    (INKTAG_STATUS_READY_HALF_MS / RECOVERY_LED_TICK_MS)
#define RECOVERY_CONTROL_START 0x01u
#define RECOVERY_CONTROL_FINISH 0x02u
#define RECOVERY_CONTROL_SAFE_RESUME_APP 0x03u
#define RECOVERY_RETURN_WAIT_ACK 1u
#define RECOVERY_RETURN_VERIFY 2u
#define RECOVERY_RETURN_RESTORE 3u
#define RECOVERY_RETURN_VERIFY_RESTORED 4u
#define RECOVERY_RETURN_BOOT_CHECK 5u
#define RECOVERY_RETURN_BOOT_VERIFIED 6u
#if defined(INKTAG_TLSR_TRUSTED_ENTRY)
extern bool inktag_tlsr_trusted_epoch_allowed(uint32_t epoch);
#endif

typedef enum {
    RECOVERY_LED_OFF = 0,
    RECOVERY_LED_STARTUP,
    RECOVERY_LED_TRANSFER,
    RECOVERY_LED_INSTALL,
    RECOVERY_LED_ERROR,
} recovery_led_mode_t;

typedef struct {
    uint16_t length;
    uint16_t byte_cursor;
    uint8_t data[OTA_DATA_PACKET_SIZE];
} ota_packet_t;

/* Byte 0 remains the original status value. Bytes 1..2 acknowledge the next
 * expected block so a write-without-response host can provide real flow
 * control; bytes 3..7 retain bounded failure diagnostics. */
static RAM uint8_t s_status[INKTAG_RECOVERY_OTA_STATUS_SIZE];
static RAM uint8_t s_active;
static RAM uint16_t s_expected_index;
static RAM uint32_t s_declared_size;
static RAM uint8_t s_return_state;
static RAM uint8_t s_return_trial;
static RAM uint8_t s_session_mutated;
static RAM uint8_t s_validation_active;
static RAM uint32_t s_verified_boot_size;
static RAM uint8_t s_validation_safe_resume;
static RAM uint32_t s_validation_offset;
static RAM uint32_t s_validation_crc;
static RAM ota_packet_t s_queue[OTA_QUEUE_SLOTS];
static RAM volatile uint8_t s_queue_head;
static RAM volatile uint8_t s_queue_tail;
static RAM volatile uint8_t s_queue_count;
static RAM uint16_t s_recovery_led_red;
static RAM uint16_t s_recovery_led_green;
static RAM uint16_t s_recovery_led_blue;
static RAM uint8_t s_recovery_led_active_low;
static RAM uint8_t s_recovery_led_colors;
static RAM uint32_t s_recovery_led_next;
static RAM recovery_led_mode_t s_recovery_led_mode;
static RAM uint32_t s_recovery_led_phase;
static RAM uint8_t s_restore_active;
static RAM uint16_t s_restore_offset;
/* Ordinary .bss: safe resume needs one complete first-sector snapshot, but it
 * must not enter the retained-data load image and consume Recovery Flash. */
static uint8_t s_first_sector[OTA_FLASH_SECTOR_SIZE];

static void advertise_transport(void);

static void recovery_led_output_init(uint32_t pin,uint8_t color) {
    bool low=(s_recovery_led_active_low&color)!=0u;
    gpio_setup_up_down_resistor(pin, low?PM_PIN_PULLUP_1M:PM_PIN_PULLDOWN_100K);
    gpio_write(pin, low);
    inktag_tlsr_gpio_set_func(pin, AS_GPIO);
    gpio_set_output_en(pin, 1);
    gpio_set_input_en(pin, 0);
}

static void recovery_blue_apply(bool on) {
    uint16_t pin=s_recovery_led_blue;
    if (!pin || (pin==GPIO_PA7 && !on)) {
        /* A disabled/malformed LED configuration never activates a pad.
         * The immutable SWS pad remains available, not an LED fallback. */
        gpio_write(GPIO_PA7,1);
        gpio_set_output_en(GPIO_PA7,0);
        gpio_set_input_en(GPIO_PA7,1);
        inktag_tlsr_gpio_set_func(GPIO_PA7,AS_SWIRE);
        gpio_setup_up_down_resistor(GPIO_PA7,PM_PIN_PULLUP_1M);
        return;
    }
    recovery_led_output_init(pin,INKTAG_STATUS_COLOR_BLUE);
    gpio_write(pin,on!=((s_recovery_led_active_low&INKTAG_STATUS_COLOR_BLUE)!=0u));
}

static void recovery_led_output(bool red, bool green, bool blue) {
    if (s_recovery_led_colors == 0u) {
        recovery_blue_apply(false);
        return;
    }
    /* Error indication uses configured red, with configured blue as the
     * fallback when the red channel is unavailable. */
    bool red_on = red && s_recovery_led_red != 0u;
    if (s_recovery_led_red != 0u)
        gpio_write(s_recovery_led_red, red_on!=((s_recovery_led_active_low&INKTAG_STATUS_COLOR_RED)!=0u));
    if (s_recovery_led_green != 0u)
        gpio_write(s_recovery_led_green, green!=((s_recovery_led_active_low&INKTAG_STATUS_COLOR_GREEN)!=0u));
    /* PA7 is also SWS. Recovery owns it only during the blue on phase and
     * restores AS_SWIRE immediately during every off phase and idle state. */
    recovery_blue_apply(blue || (red && !red_on));
}

static _attribute_no_inline_ void recovery_led_set(recovery_led_mode_t mode) {
    s_recovery_led_mode = mode;
    s_recovery_led_phase = 0u;
    s_recovery_led_next = clock_time();
}

static void recovery_led_tick(void) {
    uint8_t leds = 0u;
    bool on = false;
    uint32_t now = clock_time();
    /* clock_time() is the fixed 16 MHz system timer, not the CPU clock.
     * CPU-derived ticks stretched this cadence 1.5x at 24 MHz and 3x at 48 MHz. */
    uint32_t tick = RECOVERY_LED_TICK_MS * CLOCK_16M_SYS_TIMER_CLK_1MS;
    uint32_t due;
    if ((int32_t)(now - s_recovery_led_next) < 0) return;
    /* Keep the scene phase tied to the absolute monotonic deadline. Flash
     * sector erase/program can return after a nominal 120 ms boundary. Using
     * `now + tick` here accumulated every such delay and made the 240 ms OTA
     * download indication visibly irregular. Skip any phase that elapsed
     * while the CPU was inside Flash, then preserve the original deadline. */
    due = (now - s_recovery_led_next) / tick + 1u;
    s_recovery_led_next += due * tick;
    s_recovery_led_phase += due - 1u;
    if (s_recovery_led_mode == RECOVERY_LED_STARTUP) {
        uint8_t colors = s_recovery_led_colors & INKTAG_STATUS_COLOR_WHITE;
        uint8_t count = (uint8_t)(((colors & INKTAG_STATUS_COLOR_RED) != 0u) +
                                  ((colors & INKTAG_STATUS_COLOR_GREEN) != 0u) +
                                  ((colors & INKTAG_STATUS_COLOR_BLUE) != 0u));
        uint32_t startup_ticks = (uint32_t)count *
            RECOVERY_LED_STARTUP_COLOR_TICKS;
        uint32_t ready_start = startup_ticks +
            RECOVERY_LED_READY_HALF_TICKS;
        if (s_recovery_led_phase < startup_ticks) {
            uint8_t selected = (uint8_t)(s_recovery_led_phase /
                RECOVERY_LED_STARTUP_COLOR_TICKS);
            uint8_t bit;
            for (bit = INKTAG_STATUS_COLOR_RED;
                 bit <= INKTAG_STATUS_COLOR_BLUE; bit <<= 1u) {
                if ((colors & bit) == 0u) continue;
                if (selected-- == 0u) {
                    leds = bit;
                    break;
                }
            }
        } else if (s_recovery_led_phase >= ready_start) {
            uint32_t ready_phase = s_recovery_led_phase - ready_start;
            if (ready_phase < 6u * RECOVERY_LED_READY_HALF_TICKS &&
                ((ready_phase / RECOVERY_LED_READY_HALF_TICKS) & 1u) == 0u)
                leds = INKTAG_STATUS_COLOR_BLUE;
            if (ready_phase >= 6u * RECOVERY_LED_READY_HALF_TICKS)
                s_recovery_led_mode = RECOVERY_LED_OFF;
        }
    } else if (s_recovery_led_mode == RECOVERY_LED_TRANSFER) {
        /* Host-to-Recovery firmware bytes use the product-wide 240 ms
         * download cadence. Keep the 120 ms tick so the post-validation
         * install phase below remains the required 120 ms on/off cadence. */
        on = (s_recovery_led_phase %
              (2u * RECOVERY_LED_TRANSFER_HALF_TICKS)) <
             RECOVERY_LED_TRANSFER_HALF_TICKS;
    } else if (s_recovery_led_mode == RECOVERY_LED_INSTALL) {
        on = (s_recovery_led_phase & 1u) == 0u;
    } else if (s_recovery_led_mode == RECOVERY_LED_ERROR) {
        on = s_recovery_led_phase < 6u &&
             (s_recovery_led_phase & 1u) == 0u;
        if (s_recovery_led_phase >= 6u)
            s_recovery_led_mode = RECOVERY_LED_OFF;
    }
    if (s_recovery_led_mode == RECOVERY_LED_ERROR && on)
        leds = s_recovery_led_red != 0u ? INKTAG_STATUS_COLOR_RED :
                                         INKTAG_STATUS_COLOR_BLUE;
    else if ((s_recovery_led_mode == RECOVERY_LED_TRANSFER ||
              s_recovery_led_mode == RECOVERY_LED_INSTALL) && on)
        leds = INKTAG_STATUS_COLOR_BLUE;
    recovery_led_output((leds & INKTAG_STATUS_COLOR_RED) != 0u,
                        (leds & INKTAG_STATUS_COLOR_GREEN) != 0u,
                        (leds & INKTAG_STATUS_COLOR_BLUE) != 0u);
    ++s_recovery_led_phase;
}

void inktag_recovery_ota_init(void) {
    inktag_tlsr_recovery_led_pins_t pins;
    memset(s_status, 0, sizeof(s_status));
    s_active = 0u;
    s_expected_index = 0u;
    s_declared_size = 0u;
    s_return_state = s_return_trial = s_session_mutated = 0u;
    s_validation_active = 0u;
    s_validation_safe_resume = 0u;
    s_validation_offset = 0u;
    s_validation_crc = 0xFFFFFFFFu;
    s_queue_head = 0u;
    s_queue_tail = 0u;
    s_queue_count = 0u;
    s_restore_active = 0u;
    s_restore_offset = 0u;
    advertise_transport();
    s_recovery_led_red=s_recovery_led_green=s_recovery_led_blue=0u;
    s_recovery_led_colors=s_recovery_led_active_low=0u;
    if (inktag_tlsr_recovery_led_pins(&pins)) {
        s_recovery_led_red=pins.pins[0];
        s_recovery_led_green=pins.pins[1];
        s_recovery_led_blue=pins.pins[2];
        s_recovery_led_active_low=pins.active_low;
        s_recovery_led_colors=(s_recovery_led_red?1u:0u)|
            (s_recovery_led_green?2u:0u)|(s_recovery_led_blue?4u:0u);
    }
    if (s_recovery_led_red != 0u)
        recovery_led_output_init(s_recovery_led_red,INKTAG_STATUS_COLOR_RED);
    if (s_recovery_led_green != 0u)
        recovery_led_output_init(s_recovery_led_green,INKTAG_STATUS_COLOR_GREEN);
    recovery_led_output(false, false, false);
    recovery_led_set(RECOVERY_LED_OFF);
}

void inktag_recovery_ota_role_ready(void) {
    recovery_led_set(RECOVERY_LED_STARTUP);
}

static uint16_t read_u16_le(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t crc16_telink(const uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFFu;
    while (length-- != 0u) {
        uint8_t current = *data++;
        uint8_t bit;
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (uint16_t)((crc >> 1) ^
                (((crc ^ current) & 1u) != 0u ? 0xA001u : 0u));
            current >>= 1;
        }
    }
    return crc;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data,
                             uint32_t length) {
    while (length-- != 0u) {
        uint8_t bit;
        crc ^= *data++;
        for (bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static _attribute_no_inline_ void fail(uint8_t status) {
    s_return_state = 0u;
    s_active = 0u;
    s_validation_active = 0u;
    s_validation_safe_resume = 0u;
    s_restore_active = 0u;
    s_queue_tail = s_queue_head;
    s_queue_count = 0u;
    /* Keep the first causal error. Extra write-without-response packets that
     * were already on-air must not replace it with a generic BAD_PACKET. */
    if (s_status[0] < INKTAG_RECOVERY_OTA_BAD_PACKET ||
        s_status[0] == INKTAG_RECOVERY_OTA_SAFE_RESUME_COMPLETE)
        s_status[0] = status;
    inktag_recovery_att_notify_status();
    recovery_led_set(RECOVERY_LED_ERROR);
}

static _attribute_no_inline_ void acknowledge_index(void) {
    s_status[1] = (uint8_t)s_expected_index;
    s_status[2] = (uint8_t)(s_expected_index >> 8);
}

static void advertise_transport(void) {
    s_status[8] = 'I';
    s_status[9] = 'T';
    s_status[10] = INKTAG_RECOVERY_OTA_FAST_VERSION;
    s_status[11] = INKTAG_RECOVERY_OTA_FAST_MAX_BLOCKS;
}

static bool flash_write_chunk(uint32_t address, const uint8_t *data) {
    return inktag_tlsr_flash_write_page(address, OTA_BLOCK_SIZE, data,
                                        INKTAG_POWER_OPERATION_FLASH);
}

INKTAG_GUARD_A
static _attribute_no_inline_ bool application_context_valid(
    uint32_t application_size) {
    inktag_board_id_t board = inktag_board_detect();
    /* The current native mapping/epoch includes upper-stock exclusion. Do not
     * recover an older unbound App or lower a durable journal floor. This is
     * format/physical context, not a publisher-signature authority. */
    return board != INKTAG_BOARD_UNKNOWN &&
        application_size >= 36u && application_size <= INKTAG_APPLICATION_SIZE &&
        !(board == INKTAG_BOARD_NEBULAR_420R_N &&
          !inktag_tlsr_board_high_image_extent_allowed() &&
          INKTAG_TLSR_OTA_COMPATIBILITY_EPOCH < INKTAG_TLSR_OTA_UPPER_STOCK_MIN_EPOCH);
}

INKTAG_GUARD_A
static bool begin_image_validation(uint8_t marker) {
    s_verified_boot_size=0u;
    uint8_t header[32];
    uint32_t application_size;
    flash_read_page(INKTAG_APPLICATION_START, sizeof(header), header);
    s_declared_size = read_u32_le(&header[24]);
    application_size = s_declared_size + 4u;
    if (header[8] != marker || header[9] != 'N' || header[10] != 'L' ||
        header[11] != 'T' || s_declared_size < sizeof(header) ||
        s_declared_size > INKTAG_APPLICATION_SIZE - 4u) {
        return false;
    }
#if defined(INKTAG_TLSR_BOOT_BINDING)
    uint32_t prefix;
    header[8] = 0x4Bu;
    if (!inktag_tlsr_boot_ram_prefix(header, INKTAG_APPLICATION_SIZE, &prefix))
        return false;
    if (s_declared_size < INKTAG_TLSR_BOOT_BINDING_OFFSET + 20u) return false;
    /* Actual native bytes must bind this physical mapping. A host manifest,
     * old zero-linked image or stale cached verdict cannot substitute it. */
    flash_read_page(INKTAG_APPLICATION_START + INKTAG_TLSR_BOOT_BINDING_OFFSET,20u,header);
    if (!inktag_tlsr_boot_binding_matches(header)) return false;
#endif
    if (!application_context_valid(application_size)) return false;
    s_validation_active = 1u;
    s_validation_offset = 0u;
    s_validation_crc = 0xFFFFFFFFu;
    return true;
}

static _attribute_no_inline_ void begin_safe_resume(void) {
    if (s_active || s_validation_active || s_restore_active || s_return_state ||
        s_queue_count || s_session_mutated ||
        !inktag_tlsr_recovery_controlled_request_intact() ||
        !inktag_tlsr_recovery_return_ready(false)) {
        fail(INKTAG_RECOVERY_OTA_UNSAFE_RESUME);
        return;
    }
    if (!inktag_tlsr_power_operation_allowed(INKTAG_POWER_OPERATION_FLASH)) {
        fail(INKTAG_RECOVERY_OTA_LOW_POWER);
        return;
    }
    if (!begin_image_validation(0u)) {
        fail(INKTAG_RECOVERY_OTA_UNSAFE_RESUME);
        return;
    }
    s_validation_safe_resume = 1u;
    recovery_led_set(RECOVERY_LED_INSTALL);
    s_status[0] = INKTAG_RECOVERY_OTA_RECEIVING;
    advertise_transport();
    inktag_recovery_att_notify_status();
}

bool inktag_recovery_ota_return_waiting(void) {
    return s_return_state == RECOVERY_RETURN_WAIT_ACK;
}

void inktag_recovery_ota_return_cancel(void) {
    if (inktag_recovery_ota_return_waiting()) fail(INKTAG_RECOVERY_OTA_UNSAFE_RESUME);
}

void inktag_recovery_ota_return_transmitted(void) {
    if (!inktag_recovery_ota_return_waiting()) return;
    if (s_active || s_validation_active || s_restore_active || s_queue_count ||
        !inktag_tlsr_recovery_return_ready(s_return_trial != 0u) ||
        !inktag_tlsr_power_operation_allowed(INKTAG_POWER_OPERATION_FLASH) ||
        !begin_image_validation(s_return_trial ? 0x4Bu : 0u)) {
        fail(INKTAG_RECOVERY_OTA_UNSAFE_RESUME);
        return;
    }
    /* Rehash/reverify the current App after actual response TX. This is a
     * fresh main-loop operation, not a cached verdict or disconnect timer. */
    s_return_state = RECOVERY_RETURN_VERIFY;
    s_validation_safe_resume = 0u;
}

static void finish_return(void) {
    if (s_active || s_validation_active || s_restore_active || s_queue_count ||
        !inktag_tlsr_application_marker_valid() ||
        !inktag_tlsr_recovery_return_ready(s_return_trial != 0u) ||
        !inktag_tlsr_power_operation_allowed(INKTAG_POWER_OPERATION_FLASH)) {
        fail(INKTAG_RECOVERY_OTA_UNSAFE_RESUME);
        return;
    }
#if defined(INKTAG_TLSR_TRUSTED_ENTRY)
    inktag_tlsr_trusted_handoff();
    fail(INKTAG_RECOVERY_OTA_UNSAFE_RESUME);
#else
    start_reboot();
#endif
}

INKTAG_GUARD_B
static void complete_validated_install(void) {
    uint8_t expected_bytes[4];
    uint32_t expected;
    bool safe_resume = s_validation_safe_resume != 0u;
    s_validation_active = 0u;
    s_validation_safe_resume = 0u;
    s_validation_crc = ~s_validation_crc;
    flash_read_page(INKTAG_APPLICATION_START + s_declared_size, sizeof(expected_bytes), expected_bytes);
    expected = ((uint32_t)expected_bytes[0] << 24) |
               ((uint32_t)expected_bytes[1] << 16) |
               ((uint32_t)expected_bytes[2] << 8) | expected_bytes[3];
    uint32_t admitted=(s_declared_size+4u)&(0u-(uint32_t)(s_validation_crc==expected));
    if (!admitted) {
        fail(safe_resume ? INKTAG_RECOVERY_OTA_UNSAFE_RESUME :
                           INKTAG_RECOVERY_OTA_BAD_IMAGE);
        return;
    }
#if defined(INKTAG_TLSR_TRUSTED_ENTRY)
    if (s_return_state == RECOVERY_RETURN_BOOT_CHECK) {
        if (inktag_tlsr_trusted_epoch_allowed(INKTAG_TLSR_OTA_COMPATIBILITY_EPOCH)) {
            s_verified_boot_size=admitted;
            s_return_state = RECOVERY_RETURN_BOOT_VERIFIED;
        }
        else fail(INKTAG_RECOVERY_OTA_UNSAFE_RESUME);
        return;
    }
#endif
    recovery_led_set(RECOVERY_LED_INSTALL);
    if ((s_return_state == RECOVERY_RETURN_VERIFY && s_return_trial) ||
        s_return_state == RECOVERY_RETURN_VERIFY_RESTORED) {
        finish_return();
        return;
    }
    if (safe_resume) {
        /* Guard accepted, but do not rewrite even the controlled marker until
         * the client has actually received the successful transport response. */
        s_return_trial = 0u;
        s_return_state = RECOVERY_RETURN_WAIT_ACK;
        s_status[0] = INKTAG_RECOVERY_OTA_SAFE_RESUME_COMPLETE;
        inktag_recovery_att_notify_status();
        return;
    }
    if (s_return_state == RECOVERY_RETURN_VERIFY) {
        if (!inktag_tlsr_recovery_controlled_request_intact()) {
            fail(INKTAG_RECOVERY_OTA_UNSAFE_RESUME);
            return;
        }
        if (!inktag_tlsr_power_operation_allowed(
                INKTAG_POWER_OPERATION_FLASH)) {
            fail(INKTAG_RECOVERY_OTA_LOW_POWER);
            return;
        }
        flash_read_page(INKTAG_APPLICATION_START, sizeof(s_first_sector), s_first_sector);
        /* Keep the MCU unbootable until every other byte is durable. */
        s_first_sector[INKTAG_APPLICATION_MARKER - INKTAG_APPLICATION_START] = 0xFFu;
        s_session_mutated = 1u;
        if (!inktag_tlsr_flash_erase_sector(
                INKTAG_APPLICATION_START, INKTAG_POWER_OPERATION_FLASH)) {
            fail(INKTAG_RECOVERY_OTA_LOW_POWER);
            return;
        }
        s_restore_offset = 0u;
        s_restore_active = 1u;
        s_return_state = RECOVERY_RETURN_RESTORE;
        return;
    }
    if (!inktag_tlsr_recovery_arm_compatibility_epoch(
            INKTAG_TLSR_OTA_COMPATIBILITY_EPOCH) ||
        !inktag_tlsr_application_marker_commit()) {
        fail(INKTAG_RECOVERY_OTA_FLASH_ERROR);
        return;
    }
    s_status[0] = INKTAG_RECOVERY_OTA_COMPLETE;
    inktag_recovery_att_notify_status();
    /* The trial application alone reports success after it has registered
     * GATT, advertised and committed the Recovery Journal confirmation. */
    s_return_trial = 1u;
    s_return_state = RECOVERY_RETURN_WAIT_ACK;
}

static void restore_first_sector_step(void) {
    uint8_t verify[OTA_BLOCK_SIZE];
    if (!s_restore_active) return;
    if (s_restore_offset < sizeof(s_first_sector)) {
        if (!inktag_tlsr_flash_write_page(
                INKTAG_APPLICATION_START + s_restore_offset, OTA_BLOCK_SIZE,
                &s_first_sector[s_restore_offset],
                INKTAG_POWER_OPERATION_FLASH)) {
            fail(INKTAG_RECOVERY_OTA_LOW_POWER);
            return;
        }
        flash_read_page(INKTAG_APPLICATION_START + s_restore_offset, sizeof(verify), verify);
        if (memcmp(verify, &s_first_sector[s_restore_offset],
                   sizeof(verify)) != 0) {
            fail(INKTAG_RECOVERY_OTA_FLASH_ERROR);
            return;
        }
        s_restore_offset = (uint16_t)(s_restore_offset + OTA_BLOCK_SIZE);
        return;
    }
    /* The marker is the persistent mutation guard and the boot gate. Restore
     * it last; this path never confirms a trial or advances the epoch. */
    if (!inktag_tlsr_application_marker_commit()) {
        fail(INKTAG_RECOVERY_OTA_LOW_POWER);
        return;
    }
    s_restore_active = 0u;
    /* Rewriting the first sector invalidates the earlier read-only verdict.
     * Verify the actual durable App once more after final marker commit. */
    if (!begin_image_validation(0x4Bu)) {
        fail(INKTAG_RECOVERY_OTA_UNSAFE_RESUME);
        return;
    }
    s_return_state = RECOVERY_RETURN_VERIFY_RESTORED;
}

static void validate_image_step(void) {
    uint8_t buffer[64];
    uint32_t body_size = s_declared_size;
    uint32_t count;
    uint32_t crc_count = 0u;
    if (!s_validation_active) return;
    if (s_validation_offset >= body_size) {
        complete_validated_install();
        return;
    }
    count = body_size - s_validation_offset;
    if (count > sizeof(buffer)) count = sizeof(buffer);
    flash_read_page(INKTAG_APPLICATION_START + s_validation_offset, count, buffer);
    if (s_validation_offset <= 8u && s_validation_offset + count > 8u) {
        buffer[8u - s_validation_offset] = 0x4Bu;
    }
    if (s_validation_offset < s_declared_size) {
        crc_count = s_declared_size - s_validation_offset;
        if (crc_count > count) crc_count = count;
        s_validation_crc = crc32_update(s_validation_crc, buffer, crc_count);
    }
    s_validation_offset += count;
}

#if defined(INKTAG_TLSR_TRUSTED_ENTRY)
bool inktag_recovery_ota_boot_image_valid(void) {
    uint8_t previous=s_return_state;
    bool valid;
    if (s_active || s_validation_active || s_restore_active || s_queue_count ||
        (previous != 0u && previous != RECOVERY_RETURN_VERIFY &&
         previous != RECOVERY_RETURN_VERIFY_RESTORED) ||
        !inktag_tlsr_trusted_epoch_allowed(INKTAG_TLSR_OTA_COMPATIBILITY_EPOCH) ||
        !begin_image_validation(0x4Bu)) return false;
    s_return_state=RECOVERY_RETURN_BOOT_CHECK;
    /* The same native context/CRC validation serves OTA and guarded return.
     * No publisher verifier or cached successful verdict is involved. */
    while (s_validation_active) {
        validate_image_step();
        wd_clear();
    }
    valid=s_return_state==RECOVERY_RETURN_BOOT_VERIFIED;
    if (valid) s_return_state=previous;
    return valid;
}
#endif

static void start_transfer(void) {
    if (s_validation_active || s_restore_active || s_return_state != 0u) {
        fail(INKTAG_RECOVERY_OTA_BAD_SEQUENCE);
        return;
    }
    s_active = 1u;
    s_expected_index = 0u;
    s_declared_size = 0u;
    s_validation_active = 0u;
    s_validation_safe_resume = 0u;
    memset(s_status, 0, sizeof(s_status));
    s_status[0] = INKTAG_RECOVERY_OTA_RECEIVING;
    acknowledge_index();
    advertise_transport();
    inktag_recovery_ble_request_fast_connection();
    inktag_recovery_att_notify_status();
    /* Discard data queued behind a repeated start. A new transfer is a hard
     * session boundary, never a continuation of stale packets. */
    s_queue_tail = s_queue_head;
    s_queue_count = 0u;
    recovery_led_set(RECOVERY_LED_TRANSFER);
}

typedef enum {
    OTA_PACKET_PENDING = 0,
    OTA_PACKET_COMPLETE,
    OTA_PACKET_FAILED,
} ota_packet_result_t;

static ota_packet_result_t receive_data_step(ota_packet_t *packet) {
    const uint8_t *data = packet->data;
    uint16_t length = packet->length;
    uint16_t payload_length;
    uint8_t block_count;
    uint16_t index;
    uint32_t address;
    uint8_t payload[OTA_BLOCK_SIZE];
    if (!s_active || length < OTA_MIN_DATA_PACKET_SIZE ||
        length > OTA_DATA_PACKET_SIZE || ((length - 4u) % OTA_BLOCK_SIZE) != 0u ||
        crc16_telink(data, (uint16_t)(length - 2u)) !=
            read_u16_le(&data[length - 2u])) {
        fail(INKTAG_RECOVERY_OTA_BAD_PACKET);
        return OTA_PACKET_FAILED;
    }
    block_count = (uint8_t)((length - 4u) / OTA_BLOCK_SIZE);
    payload_length = (uint16_t)block_count * OTA_BLOCK_SIZE;
    index = read_u16_le(data);
    if (index != s_expected_index || packet->byte_cursor >= payload_length) {
        fail(INKTAG_RECOVERY_OTA_BAD_SEQUENCE);
        return OTA_PACKET_FAILED;
    }
    address = (uint32_t)index * OTA_BLOCK_SIZE;
    if ((uint16_t)(index % (OTA_FLASH_SECTOR_SIZE / OTA_BLOCK_SIZE)) +
            block_count > (OTA_FLASH_SECTOR_SIZE / OTA_BLOCK_SIZE) ||
        address > INKTAG_APPLICATION_SIZE - payload_length) {
        fail(INKTAG_RECOVERY_OTA_BAD_IMAGE);
        return OTA_PACKET_FAILED;
    }
    address += packet->byte_cursor;
    if (address == 0u) {
        const uint8_t *header = &data[2u];
        uint8_t size_bytes[4];
        if (block_count < 2u) {
            fail(INKTAG_RECOVERY_OTA_BAD_IMAGE);
            return OTA_PACKET_FAILED;
        }
        memcpy(size_bytes, &header[24], sizeof(size_bytes));
        s_declared_size = read_u32_le(size_bytes);
        s_status[3] = 1u;
        memcpy(&s_status[4], size_bytes, sizeof(size_bytes));
        if (header[8] != 0x4Bu || header[9] != 'N' ||
            header[10] != 'L' || header[11] != 'T' ||
            s_declared_size < 32u ||
            s_declared_size > INKTAG_APPLICATION_SIZE - 4u) {
            s_status[3] = (header[8] != 0x4Bu || header[9] != 'N' ||
                header[10] != 'L' || header[11] != 'T') ? 4u :
                (s_declared_size < 32u ? 2u : 3u);
            fail(INKTAG_RECOVERY_OTA_BAD_IMAGE);
            return OTA_PACKET_FAILED;
        }
    }
    memcpy(payload, &data[2u + packet->byte_cursor], sizeof(payload));
    if (address == 0u) {
        payload[8] = 0xFFu;
    }
    if (packet->byte_cursor == 0u &&
        (address & (OTA_FLASH_SECTOR_SIZE - 1u)) == 0u) {
        /* Repeat the 2.60 V OTA gate at the first destructive primitive.
         * A controlled, untouched App may be restored at the 2.40 V Flash
         * floor after the client has first observed LOW_POWER. */
        if (address == 0u && !inktag_tlsr_power_operation_allowed(INKTAG_POWER_OPERATION_OTA)) {
            fail(INKTAG_RECOVERY_OTA_LOW_POWER);
            return OTA_PACKET_FAILED;
        }
        s_session_mutated = 1u;
        if (!inktag_tlsr_flash_erase_sector(INKTAG_APPLICATION_START + address,
                INKTAG_POWER_OPERATION_FLASH)) {
            fail(INKTAG_RECOVERY_OTA_LOW_POWER);
            return OTA_PACKET_FAILED;
        }
    }
    /* Keep each IRQ-off Page Program at the qualified 16-byte granularity and
     * service BLE between blocks. The final image CRC verifies every byte, so
     * a second per-block Flash read only doubled radio-blocking operations. */
    if (!flash_write_chunk(INKTAG_APPLICATION_START + address, payload)) {
        fail(INKTAG_RECOVERY_OTA_LOW_POWER);
        return OTA_PACKET_FAILED;
    }
    packet->byte_cursor = (uint16_t)(packet->byte_cursor + sizeof(payload));
    if (packet->byte_cursor < payload_length) return OTA_PACKET_PENDING;
    s_expected_index = (uint16_t)(index + block_count);
    acknowledge_index();
    inktag_recovery_att_notify_status();
    return OTA_PACKET_COMPLETE;
}

static void finish_transfer(const uint8_t *data, uint16_t length) {
    uint16_t maximum;
    uint32_t application_size;
    uint32_t total_size;
    uint32_t required_blocks;
    if (!s_active || length != OTA_END_PACKET_SIZE ||
        data[0] != RECOVERY_CONTROL_FINISH || data[1] != 0xFFu) {
        fail(INKTAG_RECOVERY_OTA_BAD_PACKET);
        return;
    }
    maximum = read_u16_le(&data[2]);
    if ((uint16_t)(maximum ^ read_u16_le(&data[4])) != 0xFFFFu ||
        s_expected_index != (uint16_t)(maximum + 1u) ||
        s_declared_size == 0u) {
        fail(INKTAG_RECOVERY_OTA_BAD_IMAGE);
        return;
    }
    application_size = s_declared_size + 4u;
    total_size = application_size + OTA_LEGACY_SIGNATURE_BYTES;
    required_blocks = (application_size + OTA_BLOCK_SIZE - 1u) / OTA_BLOCK_SIZE;
    if (required_blocks != s_expected_index &&
        (total_size > INKTAG_APPLICATION_SIZE ||
         (total_size + OTA_BLOCK_SIZE - 1u) / OTA_BLOCK_SIZE != s_expected_index)) {
        fail(INKTAG_RECOVERY_OTA_BAD_IMAGE);
        return;
    }
    if (!begin_image_validation(0xFFu)) {
        fail(INKTAG_RECOVERY_OTA_BAD_IMAGE);
        return;
    }
    s_active = 0u;
}

uint8_t *inktag_recovery_ota_status_value(void) {
    return s_status;
}

int inktag_recovery_ota_write(void *packet) {
    rf_packet_att_write_t *request = (rf_packet_att_write_t *)packet;
    uint16_t length = request->l2capLen >= 3u ? request->l2capLen - 3u : 0u;
    if (s_return_state != 0u) {
        fail(INKTAG_RECOVERY_OTA_UNSAFE_RESUME);
        return 0;
    }
    if (length == 0u || length > OTA_DATA_PACKET_SIZE) {
        fail(INKTAG_RECOVERY_OTA_BAD_PACKET);
        return 0;
    }
    if (s_queue_count >= OTA_QUEUE_SLOTS) {
        fail(INKTAG_RECOVERY_OTA_QUEUE_FULL);
        return 0;
    }
    /* GATT context only copies one bounded packet. Flash erase/program and
     * full-image CRC stay in the Recovery application loop so a slow sector
     * operation cannot resume inside the BLE stack callback. */
    s_queue[s_queue_head].length = length;
    s_queue[s_queue_head].byte_cursor = 0u;
    memcpy(s_queue[s_queue_head].data, &request->value, length);
    s_queue_head = (uint8_t)((s_queue_head + 1u) % OTA_QUEUE_SLOTS);
    ++s_queue_count;
    return 0;
}

void inktag_recovery_ota_process(void) {
    recovery_led_tick();
    if (s_queue_count != 0u) {
        ota_packet_t *packet = &s_queue[s_queue_tail];
        const uint8_t *data = packet->data;
        if (packet->length == 2u &&
            data[0] == RECOVERY_CONTROL_START && data[1] == 0xFFu) {
            start_transfer();
            /* start_transfer discards every queued packet, including this one. */
        } else if (packet->length == 2u &&
                   data[0] == RECOVERY_CONTROL_SAFE_RESUME_APP &&
                   data[1] == 0xFFu) {
            s_queue_tail = (uint8_t)((s_queue_tail + 1u) % OTA_QUEUE_SLOTS);
            --s_queue_count;
            begin_safe_resume();
        } else if (packet->length >= 2u &&
                   data[0] == RECOVERY_CONTROL_FINISH &&
                   data[1] == 0xFFu) {
            finish_transfer(data, packet->length);
            if (s_queue_count != 0u) {
                s_queue_tail = (uint8_t)((s_queue_tail + 1u) % OTA_QUEUE_SLOTS);
                --s_queue_count;
            }
        } else {
            ota_packet_result_t result = receive_data_step(packet);
            if (result == OTA_PACKET_COMPLETE) {
                s_queue_tail = (uint8_t)((s_queue_tail + 1u) % OTA_QUEUE_SLOTS);
                --s_queue_count;
            }
        }
    }
    validate_image_step();
    restore_first_sector_step();
}

/* Consumed once by the actual trusted handoff, never an OTA ACK or cached
 * outer-validator boolean. Fresh validation resets this before any read. */
uint32_t inktag_tlsr_take_verified_boot_size(void) {
    uint32_t size=s_verified_boot_size;s_verified_boot_size=0u;return size;
}
