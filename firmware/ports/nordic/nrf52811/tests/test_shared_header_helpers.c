#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "inktag_ble_identity.h"
#include "inktag_elapsed_clock.h"

static uint32_t random_state = 0x43a92601u;
static uint32_t next_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void test_clocks(void) {
    const uint32_t rates[] = {0u, 1u, 1000u, 32768u, 16000000u, UINT32_MAX};
    const uint32_t masks[] = {UINT32_MAX, 0x00ffffffu, 0x0000ffffu};
    for (unsigned rate = 0u; rate < sizeof(rates) / sizeof(rates[0]); ++rate) {
        for (unsigned mask = 0u; mask < sizeof(masks) / sizeof(masks[0]); ++mask) {
            inktag_elapsed_clock_t inline_clock, shared_clock, seconds_clock;
            uint32_t tick = next_random();
            inktag_elapsed_clock_reset(&inline_clock, tick, masks[mask], rates[rate]);
            shared_clock = seconds_clock = inline_clock;
            for (unsigned step = 0u; step < 2048u; ++step) {
                tick += next_random();
                inktag_elapsed_time_t left = inktag_elapsed_clock_sample_time(&inline_clock, tick);
                inktag_elapsed_time_t right = inktag_elapsed_clock_sample_time_shared(&shared_clock, tick);
                assert(left.ticks == right.ticks);
                assert(left.seconds == right.seconds);
                assert(left.milliseconds == right.milliseconds);
                assert(inktag_elapsed_clock_sample_shared(&seconds_clock, tick) == left.seconds);
                assert(memcmp(&inline_clock, &shared_clock, sizeof(inline_clock)) == 0);
                assert(memcmp(&inline_clock, &seconds_clock, sizeof(inline_clock)) == 0);
            }
        }
    }
}

static void test_names(void) {
    uint8_t name[256] = {0};
    const char *prefixes[] = {"IKT_", "InkTag_", "OTHER_"};
    for (unsigned p = 0u; p < sizeof(prefixes) / sizeof(prefixes[0]); ++p) {
        size_t prefix_length = strlen(prefixes[p]);
        memset(name, 'A', sizeof(name));
        memcpy(name, prefixes[p], prefix_length);
        for (unsigned length = 0u; length < 256u; ++length) {
            for (unsigned byte = 0u; byte < 256u; ++byte) {
                name[prefix_length] = (uint8_t)byte;
                assert(inktag_ble_device_name_valid(name, (uint8_t)length) ==
                       inktag_ble_device_name_valid_shared(name, (uint8_t)length));
            }
        }
    }
    for (unsigned length = 0u; length < 256u; ++length) {
        assert(!inktag_ble_device_name_valid_shared(NULL, (uint8_t)length));
    }
}

int main(void) {
    test_clocks();
    test_names();
    return 0;
}
