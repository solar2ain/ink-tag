#ifndef INKTAG_EPD_TEMPERATURE_H
#define INKTAG_EPD_TEMPERATURE_H

#include <stdbool.h>
#include <stdint.h>

/* Pure controller-wire decoders. The port owns CS/BUSY timing and must pass
 * read_ok=false if its two-byte transaction did not complete. A failed read
 * never becomes a plausible temperature or a fixed fallback. */
static inline bool inktag_uc_internal_r40_temperature_sixteenths(
    bool read_ok, const uint8_t raw[2], int16_t *out) {
    int16_t value;
    if (!read_ok || raw == 0 || out == 0 ||
        (raw[0] == 0xFFu && raw[1] == 0xFFu)) return false;
    /* UC8176/UC8276 R40 with R41 TSE=0: TS[7:0] is signed whole C.
     * Byte two's D[2:0] belongs to the external LM75 mode, not a fraction
     * of the internal sensor. Never apply the JD/SSD fractional decoder. */
    value = (int16_t)((int8_t)raw[0]) * 16;
    if (value < -40 * 16 || value > 85 * 16) return false;
    *out = value;
    return true;
}

static inline bool inktag_jd_r40_temperature_sixteenths(
    bool read_ok, const uint8_t raw[2], int16_t *out) {
    int16_t value;
    if (!read_ok || raw == 0 || out == 0 ||
        (raw[0] == 0xFFu && raw[1] == 0xFFu)) return false;
    /* JD796xx R40: signed TS[7:0] whole degrees, TS[9:8] quarters in
     * response byte 2 bits 7:6. The fraction is added even when negative. */
    value = (int16_t)((int8_t)raw[0]) * 16 +
            (int16_t)((raw[1] >> 6u) & 3u) * 4;
    if (value < -40 * 16 || value > 85 * 16) return false;
    *out = value;
    return true;
}

static inline bool inktag_ssd1680_r1b_temperature_sixteenths(
    bool read_ok, const uint8_t raw[2], int16_t *out) {
    int16_t value;
    if (!read_ok || raw == 0 || out == 0 || (raw[1] & 0x0Fu) != 0u)
        return false;
    /* SSD1680 R1B: signed 12-bit two's-complement sixteenths, with A[3:0]
     * in response byte 2's high nibble. Reject the POR 0x7FF value through
     * the product's -40..85 C plausible range. */
    value = (int16_t)(((uint16_t)raw[0] << 4u) | (raw[1] >> 4u));
    if ((value & 0x0800) != 0) value -= 0x1000;
    if (value < -40 * 16 || value > 85 * 16) return false;
    *out = value;
    return true;
}

static inline int8_t inktag_epd_temperature_floor_celsius(int16_t sixteenths) {
#if defined(INKTAG_DIALOG)
    _Static_assert((-1 >> 1) == -1, "DA temperature needs arithmetic signed shift");
    return (int8_t)(sixteenths >> 4);
#else
    int16_t whole = sixteenths / 16;
    if (sixteenths < 0 && sixteenths % 16 != 0) --whole;
    return (int8_t)whole;
#endif
}

/* SSD1619/SSD1680 R1B whole-degree product reading. The signed high byte is
 * already floor(sixteenths/16); validate the same wire/range as above without
 * retaining a fractional conversion only to discard it. */
static inline bool inktag_ssd_r1b_temperature_floor_celsius(
    bool read_ok, const uint8_t raw[2], int8_t *out) {
    if (!read_ok || raw == 0 || out == 0 || (raw[1] & 0x0Fu) != 0u ||
        (uint8_t)(raw[0] + 40u) > 125u ||
        (raw[0] == 85u && raw[1] != 0u)) return false;
    *out = (int8_t)raw[0];
    return true;
}

/* A decoded controller sample is a bounded observation, not a live sensor
 * after the panel rail has been parked. Unsigned subtraction remains valid
 * across a 32-bit millisecond-counter wrap for bounds below 2^31 ms. */
static inline bool inktag_epd_temperature_sample_fresh(
    bool valid, uint32_t sampled_ms, uint32_t now_ms, uint32_t max_age_ms) {
    return valid && max_age_ms < 0x80000000u &&
           (uint32_t)(now_ms - sampled_ms) <= max_age_ms;
}

static inline bool inktag_epd_temperature_connection_probe_required(
    bool observation_only, bool fresh_complete_sample) {
    return !observation_only && !fresh_complete_sample;
}

static inline bool inktag_ssd1683_r1b_temperature_sixteenths(
    bool read_ok, const uint8_t raw[1], int16_t *out) {
    int16_t whole;
    if (!read_ok || raw == 0 || out == 0) return false;
    /* SSD1683 R1B is one signed whole-degree byte, not SSD1680's 12-bit
     * fraction. 0xFF is a valid -1 C; 0x7F is the POR value. */
    whole = raw[0] < 0x80u ? (int16_t)raw[0] : (int16_t)raw[0] - 256;
    if (whole < -40 || whole > 85) return false;
    *out = whole * 16;
    return true;
}

#endif
