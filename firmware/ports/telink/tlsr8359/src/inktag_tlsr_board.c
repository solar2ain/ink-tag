#include "main.h"

#include <string.h>

#include "tlsr8359_hsel.h"
#include "inktag_timebase.h"
#include "inktag_clock_rate.h"

#if INKTAG_NOWA_UTC_RATE_DEFAULT_PPM < -INKTAG_CLOCK_RATE_MAX_PPM || \
    INKTAG_NOWA_UTC_RATE_DEFAULT_PPM > INKTAG_CLOCK_RATE_MAX_PPM || \
    INKTAG_NOWA213_UTC_RATE_DEFAULT_PPM < -INKTAG_CLOCK_RATE_MAX_PPM || \
    INKTAG_NOWA213_UTC_RATE_DEFAULT_PPM > INKTAG_CLOCK_RATE_MAX_PPM || \
    INKTAG_NEBULAR_UTC_RATE_DEFAULT_PPM < -INKTAG_CLOCK_RATE_MAX_PPM || \
    INKTAG_NEBULAR_UTC_RATE_DEFAULT_PPM > INKTAG_CLOCK_RATE_MAX_PPM || \
    INKTAG_NEBULAR213_UTC_RATE_DEFAULT_PPM < -INKTAG_CLOCK_RATE_MAX_PPM || \
    INKTAG_NEBULAR213_UTC_RATE_DEFAULT_PPM > INKTAG_CLOCK_RATE_MAX_PPM
#error "Board UTC default must be within the common ppm bound"
#endif

/* Code defaults follow the existing selector; no new Flash record/profile
 * field or per-board App. Zeroes fold to a single return in this batch. */
int16_t inktag_tlsr_board_clock_rate_default(inktag_board_id_t board) {
    if (board == INKTAG_BOARD_NOWA_154R_N) return INKTAG_NOWA_UTC_RATE_DEFAULT_PPM;
    if (board == INKTAG_BOARD_NOWA_213R_N) return INKTAG_NOWA213_UTC_RATE_DEFAULT_PPM;
    if (board == INKTAG_BOARD_NEBULAR_213R_N) return INKTAG_NEBULAR213_UTC_RATE_DEFAULT_PPM;
    if (board == INKTAG_BOARD_NEBULAR_420R_N) return INKTAG_NEBULAR_UTC_RATE_DEFAULT_PPM;
    return 0;
}

#if !INKTAG_NOWA_TRANSFER_PACKBITS_RLE_QUALIFIED || \
    !INKTAG_NEBULAR_TRANSFER_PACKBITS_RLE_QUALIFIED
#error "The shared TLSR PackBits image must remain qualified on both exact boards"
#endif

/* Immutable-selector access state belongs to the port, not the hardware
 * descriptors. Cache only validated reads; retain it across Deep Retention. */
static RAM inktag_board_id_t s_detected_board;
static RAM bool s_high_image_extent_allowed;

#define INKTAG_TLSR8359_HSEL_DEFINE_APP
#include "tlsr8359_hsel.h"
#undef INKTAG_TLSR8359_HSEL_DEFINE_APP

static RAM uint8_t s_selector_flags = 0xFFu;

bool inktag_tlsr_board_device_info_qr_qualified(inktag_board_id_t board) {
    const inktag_tlsr_board_profile_t *profile = inktag_tlsr_board_profile(board);
    /* QR/password use the shared Settings backend, not a fitted PCB device.
     * Preserve unknown/undersized backend refusal and the live storage gate
     * in v2_device_info_qr_available; no per-marking product switch. */
    return profile && profile->physical_flash_bytes >= 524288u;
}

uint32_t inktag_fnv1a(const uint8_t *data, uint16_t length) {
    uint32_t value = 2166136261u;
    while (length--) value = (value ^ *data++) * 16777619u;
    return value;
}

uint8_t inktag_tlsr_board_selector_flags(void) {
    inktag_board_selector_t selector;
    uint32_t checksum;
    if (s_selector_flags != 0xFFu) return s_selector_flags;
    flash_read_page(INKTAG_BOARD_SELECTOR_ADDRESS, sizeof(selector),
                    (uint8_t *)&selector);
    if (memcmp(selector.magic, "ITB1", 4u) != 0 || selector.version != 1u)
        return 0xFFu;
    checksum = inktag_fnv1a((const uint8_t *)&selector, 8u);
    if (checksum != selector.checksum ||
        selector.board_id > INKTAG_BOARD_HSEL5_154M02 ||
        (selector.board_id == INKTAG_BOARD_UNKNOWN &&
         selector.reserved != INKTAG_TLSR_SELECTOR_TYPED_ADDRESS_REQUIRED) ||
        (selector.reserved & ~(INKTAG_TLSR_SELECTOR_BASE_IMAGE_STORE_ONLY |
                               INKTAG_TLSR_SELECTOR_PIN_RECORD_REQUIRED |
                               INKTAG_TLSR_SELECTOR_TYPED_ADDRESS_REQUIRED)) != 0u ||
        ((selector.reserved & INKTAG_TLSR_SELECTOR_BASE_IMAGE_STORE_ONLY) &&
          selector.board_id != INKTAG_BOARD_NEBULAR_420R_N))
        return 0xFFu;
    s_selector_flags = (uint8_t)selector.reserved;
    s_high_image_extent_allowed =
        selector.board_id == INKTAG_BOARD_NEBULAR_420R_N &&
        (selector.reserved & INKTAG_TLSR_SELECTOR_BASE_IMAGE_STORE_ONLY) == 0u;
    s_detected_board = (inktag_board_id_t)selector.board_id;
    return s_selector_flags;
}

inktag_board_id_t inktag_board_detect(void) {
    (void)inktag_tlsr_board_selector_flags();
    return s_detected_board;
}

bool inktag_tlsr_board_high_image_extent_allowed(void) {
    return inktag_board_detect() == INKTAG_BOARD_NEBULAR_420R_N &&
           s_high_image_extent_allowed;
}

bool inktag_tlsr_board_pin_record_required(void) {
    uint8_t flags = inktag_tlsr_board_selector_flags();
    return flags != 0xFFu && (flags & INKTAG_TLSR_SELECTOR_PIN_RECORD_REQUIRED) != 0u;
}

const char *inktag_board_name(void) {
    /* Legacy GET_DEVICE_INFO board field: identify the existing signed image
     * family, never a product model. Numeric ITBD remains the sole backend
     * selector; an invalid selector still has no peripheral profile. */
    return "tlsr8359_hanshow";
}

bool inktag_tlsr_board_first_display_requested(void) {
    uint8_t marker = 0u;
    flash_read_page(INKTAG_FIRST_DISPLAY_MARKER, 1u, &marker);
    return marker == INKTAG_FIRST_DISPLAY_REQUEST;
}

bool inktag_tlsr_board_consume_first_display_request(void) {
    uint8_t consumed = 0u;
    uint8_t verify = 0xFFu;
    if (!inktag_tlsr_board_first_display_requested()) return true;
    /* The marker sits immediately after the checksummed selector. Full images
     * program 0xA5 and the application consumes it with a one-way 1->0 page
     * write, so no sector erase can endanger board identity. */
    if (!inktag_tlsr_flash_write_page(INKTAG_FIRST_DISPLAY_MARKER, 1u,
            &consumed, INKTAG_POWER_OPERATION_FLASH)) return false;
    flash_read_page(INKTAG_FIRST_DISPLAY_MARKER, 1u, &verify);
    return verify == consumed;
}

const inktag_tlsr_board_profile_t *inktag_tlsr_board_profile(
    inktag_board_id_t board) {
    if (board == INKTAG_BOARD_NOWA_154R_N) return &s_nowa;
    if (board == INKTAG_BOARD_NOWA_213R_N) return &s_nowa213;
    if (board == INKTAG_BOARD_NEBULAR_213R_N) return &s_nebular213;
    if (board == INKTAG_BOARD_NEBULAR_420R_N) return &s_nebular;
    if (board == INKTAG_BOARD_NEBULAR_PRO_154Q_N) return &s_nebularpro154q;
    if (board == INKTAG_BOARD_HSEL3_266Q) return &s_hsel3266q;
    if (board == INKTAG_BOARD_HSEL5101_200BWR) return &s_hsel5101200;
    if (board == INKTAG_BOARD_HSEL5_154M02) return &s_hsel5154m02;
    if (board == INKTAG_BOARD_STELLAR_154R_N) return &s_stellar154;
    if (board == INKTAG_BOARD_STELLAR_PRO_154Q_N) return &s_stellarpro154q;
    return NULL;
}

/* Same board-owned facts, reduced Recovery-only view. No screen/clock/NFC
 * driver, qualification inheritance or mutable boot cache is added. */
#define INKTAG_TLSR8359_HSEL_DEFINE_RECOVERY
#include "tlsr8359_hsel.h"
#undef INKTAG_TLSR8359_HSEL_DEFINE_RECOVERY

const inktag_tlsr_recovery_profile_t *inktag_tlsr_board_recovery_profile(
    inktag_board_id_t board) {
    if (board == INKTAG_BOARD_NOWA_154R_N) return &r_nowa;
    if (board == INKTAG_BOARD_NOWA_213R_N) return &r_nowa213;
    if (board == INKTAG_BOARD_NEBULAR_420R_N) return &r_nebular;
    if (board == INKTAG_BOARD_NEBULAR_213R_N) return &r_nebular213;
    if (board == INKTAG_BOARD_STELLAR_PRO_154Q_N ||
        board == INKTAG_BOARD_NEBULAR_PRO_154Q_N ||
        board == INKTAG_BOARD_HSEL5101_200BWR ||
        board == INKTAG_BOARD_HSEL5_154M02 ||
        board == INKTAG_BOARD_HSEL3_266Q ||
        board == INKTAG_BOARD_STELLAR_154R_N) return &r_pb0;
    return NULL;
}
