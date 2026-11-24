#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "inktag_packbits.h"

typedef struct {
    uint8_t output[512];
    uint16_t length;
    uint16_t calls;
} collector_t;

static bool collect(void *opaque, uint8_t *decoded, uint16_t length) {
    collector_t *collector = (collector_t *)opaque;
    assert((uint32_t)collector->length + length <= sizeof(collector->output));
    memcpy(&collector->output[collector->length], decoded, length);
    collector->length = (uint16_t)(collector->length + length);
    ++collector->calls;
    return true;
}

static void test_literal_repeat_and_large_decoded_block(void) {
    uint8_t encoded[] = {
        0x01u, 0x10u, 0x11u,
        0xffu, 0x22u,
        0xfdu, 0x22u,
        0x00u, 0x33u,
    };
    collector_t collector = {{0}, 0u, 0u};
    assert(inktag_packbits_validate_block(encoded, sizeof(encoded), 261u));
    assert(inktag_packbits_stream_block(
        encoded, sizeof(encoded), 261u, collect, &collector));
    assert(collector.length == 261u);
    assert(collector.calls == 3u);
    assert(collector.output[0] == 0x10u && collector.output[1] == 0x11u);
    for (uint16_t index = 2u; index < 260u; ++index)
        assert(collector.output[index] == 0x22u);
    assert(collector.output[260] == 0x33u);
}

static void test_invalid_block_has_no_sink_side_effect(void) {
    uint8_t truncated[] = {0x02u, 0x01u, 0x02u};
    uint8_t trailing[] = {0x80u, 0x01u, 0x00u, 0x02u};
    collector_t collector = {{0}, 0u, 0u};
    assert(!inktag_packbits_validate_block(truncated, sizeof(truncated), 3u));
    assert(!inktag_packbits_validate_block(trailing, sizeof(trailing), 3u));
    assert(!inktag_packbits_validate_block((uint8_t[]){0xffu, 0x00u}, 2u, 129u));
    assert(!inktag_packbits_stream_block(
        truncated, sizeof(truncated), 3u, collect, &collector));
    assert(collector.calls == 0u && collector.length == 0u);
}

static bool reject_after_validation(void *opaque, uint8_t *decoded,
                                    uint16_t length) {
    uint16_t *calls = (uint16_t *)opaque;
    (void)decoded;
    (void)length;
    ++*calls;
    return false;
}

static void test_sink_failure_stops_stream(void) {
    uint8_t encoded[] = {0x80u, 0x55u, 0x80u, 0xaau};
    uint16_t calls = 0u;
    assert(!inktag_packbits_stream_block(
        encoded, sizeof(encoded), 6u, reject_after_validation, &calls));
    assert(calls == 1u);
}

int main(void) {
    test_literal_repeat_and_large_decoded_block();
    test_invalid_block_has_no_sink_side_effect();
    test_sink_failure_stops_stream();
    return 0;
}
