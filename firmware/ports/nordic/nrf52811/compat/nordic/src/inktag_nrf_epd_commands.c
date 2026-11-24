#include <stdint.h>
void EPD_WriteCmd(uint8_t command);
void EPD_WriteData(uint8_t* data, uint8_t length);

#if defined(__GNUC__)
__attribute__((noinline))
#endif
void EPD_WriteInitCommands(const uint8_t* commands, uint16_t length) {
    /* Flash tables cannot be passed directly to Nordic EasyDMA. Keep the
     * original command/data transactions and stage each small tuple in RAM. */
    uint8_t bytes[16];
    while (length >= 2u) {
        uint8_t command = *commands++, count = *commands++;
        length -= 2u;
        if (count > sizeof(bytes) || count > length) return;
        for (uint8_t i = 0u; i < count; ++i) bytes[i] = commands[i];
        EPD_WriteCmd(command);
        EPD_WriteData(bytes, count);
        commands += count;
        length -= count;
    }
}
