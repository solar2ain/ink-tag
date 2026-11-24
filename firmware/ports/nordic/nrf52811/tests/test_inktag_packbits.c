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
    calls = 0u;
    assert(inktag_packbits_validate_block(encoded, sizeof(encoded), 6u));
    assert(!inktag_packbits_stream_validated(
        encoded, sizeof(encoded), reject_after_validation, &calls));
    assert(calls == 1u);
}

static void test_prevalidated_stream_matches_checked_api(void) {
    for (unsigned control = 0u; control < 256u; ++control) {
        uint8_t encoded[129];
        uint8_t source_length;
        uint16_t decoded_length;
        collector_t checked = {{0}, 0u, 0u}, admitted = {{0}, 0u, 0u};
        encoded[0] = (uint8_t)control;
        if (control & 128u) {
            encoded[1] = 0xa5u;
            source_length = 2u;
            decoded_length = (uint16_t)((control & 127u) + 3u);
        } else {
            decoded_length = (uint16_t)(control + 1u);
            source_length = (uint8_t)(decoded_length + 1u);
            for (unsigned i = 0u; i < decoded_length; ++i)
                encoded[i + 1u] = (uint8_t)(i * 19u);
        }
        assert(inktag_packbits_validate_block(encoded, source_length, decoded_length));
        assert(inktag_packbits_stream_block(encoded, source_length,
            decoded_length, collect, &checked));
        assert(inktag_packbits_stream_validated(encoded, source_length, collect, &admitted));
        assert(checked.length == admitted.length && checked.calls == admitted.calls);
        assert(!memcmp(checked.output, admitted.output, checked.length));
        assert(!inktag_packbits_stream_validated(encoded, source_length, NULL, &admitted));
    }
}

int main(void) {
    test_literal_repeat_and_large_decoded_block();
    test_invalid_block_has_no_sink_side_effect();
    test_sink_failure_stops_stream();
    test_prevalidated_stream_matches_checked_api();
    return 0;
}
