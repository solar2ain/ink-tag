#include "inktag_clock_rate.h"
#include "../epd/inktag_protocol_v2.h"
#include <stddef.h>

#define RATE_SCALE 1000000u

bool inktag_clock_rate_decode(const uint8_t *payload, size_t length, int16_t *ppm)
{
    int32_t value;
    if (!payload || !ppm || length != INKTAG_CLOCK_RATE_WIRE_BYTES ||
        payload[0] != INKTAG_SETTINGS_SECTION_CLOCK_RATE || payload[1] != 1u)
        return false;
    value = (int32_t)payload[2] | ((int32_t)payload[3] << 8);
    if (value >= 32768) value -= 65536;
    if (value < -INKTAG_CLOCK_RATE_MAX_PPM || value > INKTAG_CLOCK_RATE_MAX_PPM)
        return false;
    *ppm = (int16_t)value;
    return true;
}

void inktag_clock_rate_encode(int16_t ppm, int16_t default_ppm,
                              uint8_t payload[INKTAG_CLOCK_RATE_REPORT_BYTES])
{
    uint16_t wire = (uint16_t)ppm;
    payload[0] = INKTAG_SETTINGS_SECTION_CLOCK_RATE;
    payload[1] = 1u;
    payload[2] = (uint8_t)wire;
    payload[3] = (uint8_t)(wire >> 8);
    wire = (uint16_t)default_ppm;
    payload[4] = (uint8_t)wire;
    payload[5] = (uint8_t)(wire >> 8);
}

bool inktag_clock_rate_set(inktag_clock_rate_t *rate, int16_t ppm)
{
    if (!rate || ppm < -INKTAG_CLOCK_RATE_MAX_PPM ||
        ppm > INKTAG_CLOCK_RATE_MAX_PPM) return false;
    rate->ppm = ppm;
    rate->fraction = 0u;
    return true;
}

bool inktag_clock_rate_apply(inktag_clock_rate_t *rate,
                             uint32_t elapsed, uint32_t *adjusted)
{
    uint32_t magnitude, product, correction;
    if (!rate || !adjusted || rate->fraction >= RATE_SCALE ||
        rate->ppm < -INKTAG_CLOCK_RATE_MAX_PPM ||
        rate->ppm > INKTAG_CLOCK_RATE_MAX_PPM) return false;
    if (!rate->ppm) {
        *adjusted = elapsed;
        return true;
    }
    magnitude = (uint32_t)(rate->ppm < 0 ? -rate->ppm : rate->ppm);
    /* Split before multiplying: product <= 3,999,996,000 + 999,999.
     * Even UINT32_MAX elapsed needs only 32-bit arithmetic. */
    product = (elapsed % RATE_SCALE) * magnitude + rate->fraction;
    correction = (elapsed / RATE_SCALE) * magnitude + product / RATE_SCALE;
    if (rate->ppm > 0 && correction > UINT32_MAX - elapsed) return false;
    *adjusted = rate->ppm < 0 ? elapsed - correction : elapsed + correction;
    rate->fraction = product % RATE_SCALE;
    return true;
}
