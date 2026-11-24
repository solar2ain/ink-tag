#include <stdint.h>

/* This portable helper needs no GPIO/SDK header. Transfers are synchronous;
 * stage RO tuples in RAM because some vendor DMA engines cannot read Flash. */
void EPD_WriteCmd(uint8_t command);
void EPD_WriteData(uint8_t *data, uint8_t length);

#if defined(__GNUC__)
__attribute__((noinline))
#endif
void EPD_WriteInitCommands(const uint8_t *commands, uint16_t length) {
    uint8_t bytes[16];
    while (length >= 2u) {
        uint8_t command = *commands++;
        uint8_t count = *commands++;
        length -= 2u;
        if (count > sizeof(bytes) || count > length) return;
        for (uint8_t i = 0u; i < count; ++i) bytes[i] = commands[i];
        EPD_WriteCmd(command);
        EPD_WriteData(bytes, count);
        commands += count;
        length -= count;
    }
}

#if defined(INKTAG_DIALOG)
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void EPD_WriteCommandData(uint8_t command, uint8_t *data, uint8_t length) {
    EPD_WriteCmd(command);
    EPD_WriteData(data, length);
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
void EPD_WriteRegister1(uint8_t command, uint8_t first) {
    EPD_WriteCommandData(command, &first, 1u);
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
void EPD_WriteRegister2(uint8_t command, uint8_t first, uint8_t second) {
    uint8_t bytes[2] = {first, second};
    EPD_WriteCommandData(command, bytes, sizeof(bytes));
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
void EPD_WriteRegister3(uint8_t command, uint8_t first, uint8_t second, uint8_t third) {
    uint8_t bytes[3] = {first, second, third};
    EPD_WriteCommandData(command, bytes, sizeof(bytes));
}
#endif
