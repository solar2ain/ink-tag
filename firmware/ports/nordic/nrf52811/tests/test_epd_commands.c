#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void EPD_WriteInitCommands(const uint8_t *commands, uint16_t length);
static const uint8_t *table;
static unsigned commands_seen, data_calls, used;
static uint8_t trace[64];

void EPD_WriteCmd(uint8_t command) {
    ++commands_seen;
    trace[used++] = command;
}
void EPD_WriteData(uint8_t *bytes, uint8_t length) {
    ++data_calls;
    /* The pointer is writable scratch, never the const table. */
    assert(bytes != table + 2);
    assert(length <= 16);
    trace[used++] = length;
    memcpy(trace + used, bytes, length);
    used += length;
    memset(bytes, 0xCC, length);
}
static void run(const uint8_t *bytes, unsigned length) {
    table = bytes;
    commands_seen = data_calls = used = 0;
    EPD_WriteInitCommands(bytes, (uint16_t)length);
}
int main(void) {
    static const uint8_t valid[] = {0x01, 0, 0x02, 3, 1, 2, 3,
        0x03, 16, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    static const uint8_t truncated[] = {0x04, 2, 1};
    static const uint8_t oversized[] = {0x05, 17};
    static const uint8_t tail[] = {0x06, 1, 7, 0x08};
    run(valid, sizeof(valid));
    assert(commands_seen == 3 && data_calls == 3);
    assert(used == sizeof(valid) && memcmp(trace, valid, used) == 0);
    run(valid, sizeof(valid)); /* table was not modified by the DMA adapter */
    assert(memcmp(trace, valid, used) == 0);
    run(truncated, sizeof(truncated));
    assert(commands_seen == 0 && data_calls == 0);
    run(oversized, sizeof(oversized));
    assert(commands_seen == 0 && data_calls == 0);
    run(tail, sizeof(tail));
    assert(commands_seen == 1 && data_calls == 1 && used == 3);
    run(valid, 0);
    assert(commands_seen == 0);
    puts("EPD tuple framing, RAM staging and bounds passed");
    return 0;
}
