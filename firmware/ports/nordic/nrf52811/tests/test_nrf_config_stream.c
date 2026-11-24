#include <assert.h>
#include <stdio.h>
#include <string.h>
#define INKTAG_NRF_STREAMING_CONFIG_STORE 1
#define INKTAG_BOARD_EXTERNAL_FLASH_SETTINGS_START 0u
#define INKTAG_BOARD_SETTINGS_SIZE 8192u
#define INKTAG_BOARD_EXTERNAL_FLASH_CAPACITY 0x80000u
#define INKTAG_BOARD_EXTERNAL_FLASH_ASSET_OFFSET 0x2000u
#define INKTAG_BOARD_EXTERNAL_FLASH_ASSET_SIZE 0x8000u
#define INKTAG_BOARD_DFU_SETTINGS_START 0x2f000u
/* Compile the real adapter in this TU to emulate a cold boot of RAM state. */
#include "../firmware/ports/nordic/common/src/inktag_nrf_config_store.c"

static uint8_t flash_bytes[8192], snapshot[8192];
static uint8_t old_data[4064], new_data[4064], output[4064];
static uint32_t io_count, erase_count, program_count, error;
static int cut = -1, cut_mode;
static bool read_failure;
static unsigned sessions;
bool inktag_nrf_external_flash_begin(void) { ++sessions; return true; }
void inktag_nrf_external_flash_end(void) { assert(sessions); --sessions; }
uint32_t inktag_nrf_external_flash_last_error(void) { return error; }
bool inktag_nrf_external_flash_read(uint32_t address, void *data, uint32_t length) {
    assert(sessions && address + length <= sizeof(flash_bytes));
    if (read_failure) { error = NRF_ERROR_INTERNAL; return false; }
    memcpy(data, flash_bytes + address, length);
    return true;
}
bool inktag_nrf_external_flash_program(uint32_t address, const void *data, uint16_t length) {
    assert(sessions && address + length <= sizeof(flash_bytes));
    assert(length && (address & 255u) + length <= 256u);
    const uint8_t *bytes = data;
    bool fail = (int)io_count++ == cut;
    unsigned written = !fail || cut_mode == 2 ? length : cut_mode == 1 ? length / 2u : 0u;
    for (unsigned n = 0; n < written; ++n) {
        assert((flash_bytes[address+n] & bytes[n]) == bytes[n]);
        flash_bytes[address+n] &= bytes[n];
    }
    ++program_count;
    error = fail ? NRF_ERROR_FORBIDDEN : NRF_SUCCESS;
    return !fail;
}
bool inktag_nrf_external_flash_erase_sector(uint32_t address) {
    assert(sessions && !(address & 4095u) && address + 4096u <= sizeof(flash_bytes));
    bool fail = (int)io_count++ == cut;
    if (!fail || cut_mode == 2) memset(flash_bytes + address, 0xff, 4096);
    ++erase_count;
    error = fail ? NRF_ERROR_FORBIDDEN : NRF_SUCCESS;
    return !fail;
}
static void reboot(void) {
    assert(!sessions);
    s_initialized = false;
    cut = -1;
    assert(inktag_nrf_config_store_init());
}
static void expect_record(inktag_nrf_config_record_type_t type, const uint8_t *expected,
                          uint16_t bytes) {
    uint16_t length = 0;
    memset(output, 0, sizeof(output));
    assert(inktag_nrf_config_store_read(type, output, sizeof(output), &length));
    assert(length == bytes && !memcmp(output, expected, bytes));
}
int main(void) {
    memset(flash_bytes, 0xff, sizeof(flash_bytes));
    reboot();
    assert(erase_count == 0u && program_count == 0u);
    uint16_t absent_length = 99u;
    assert(inktag_nrf_config_store_read_result(INKTAG_NRF_CONFIG_BOARD,
        output, sizeof(output), &absent_length) == INKTAG_SETTINGS_JOURNAL_READ_MISSING);
    assert(absent_length == 0u);
    /* A retired header, nonempty invalid body and IO failure cannot become a
     * factory-empty store or trigger even one boot write/erase. */
    const config_page_header_t retired = {0x31475043u, 1u, ~1u, UINT32_MAX};
    memcpy(flash_bytes, &retired, sizeof(retired));
    s_initialized = false;
    assert(!inktag_nrf_config_store_init());
    assert(erase_count == 0u && program_count == 0u);
    memset(flash_bytes, 255, sizeof(flash_bytes));
    flash_bytes[sizeof(flash_bytes) - 1u] = 0u;
    assert(!inktag_nrf_config_store_init());
    assert(erase_count == 0u && program_count == 0u);
    memset(flash_bytes, 255, sizeof(flash_bytes));
    read_failure = true;
    assert(!inktag_nrf_config_store_init());
    assert(erase_count == 0u && program_count == 0u);
    read_failure = false;
    for (unsigned n = 0; n < sizeof(old_data); ++n) {
        old_data[n] = (uint8_t)(n * 7u + 13u);
        new_data[n] = (uint8_t)(n * 19u + 23u);
    }
    reboot();
    assert(sizeof(s_io_buffer) == 128u && CONFIG_MAX_PAYLOAD == 4064u);
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_BOARD, old_data, 3001));
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_TIME, old_data, 31));
    reboot();
    expect_record(INKTAG_NRF_CONFIG_BOARD, old_data, 3001);
    expect_record(INKTAG_NRF_CONFIG_TIME, old_data, 31);
    memcpy(snapshot, flash_bytes, sizeof(snapshot));
    io_count = 0;
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_BOARD, new_data, 3001));
    unsigned operations = io_count;
    reboot();
    expect_record(INKTAG_NRF_CONFIG_BOARD, new_data, 3001);
    /* Every erase/program boundary: rejected, partial, or physically completed
     * before reporting failure. Reboot sees a whole old or whole new value. */
    for (cut_mode = 0; cut_mode < 3; ++cut_mode) {
        for (unsigned at = 0; at < operations; ++at) {
            memcpy(flash_bytes, snapshot, sizeof(snapshot));
            reboot();
            io_count = 0;
            cut = (int)at;
            (void)inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_BOARD, new_data, 3001);
            reboot();
            uint16_t length = 0;
            assert(inktag_nrf_config_store_read(INKTAG_NRF_CONFIG_BOARD, output, sizeof(output), &length));
            assert(length == 3001 && (!memcmp(output, old_data, length) || !memcmp(output, new_data, length)));
            expect_record(INKTAG_NRF_CONFIG_TIME, old_data, 31);
        }
    }
    unsigned erased = erase_count, programmed = program_count;
    assert(!inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_BOARD, new_data, 4064));
    assert(inktag_nrf_config_store_last_error() == NRF_ERROR_DATA_SIZE);
    assert(erase_count == erased && program_count == programmed);
    assert(inktag_nrf_config_store_delete(INKTAG_NRF_CONFIG_TIME));
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_BOARD, new_data, 4064));
    reboot();
    expect_record(INKTAG_NRF_CONFIG_BOARD, new_data, 4064);
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_BOARD, old_data, 4064));
    reboot();
    expect_record(INKTAG_NRF_CONFIG_BOARD, old_data, 4064);
    uint16_t length = 0;
    assert(!inktag_nrf_config_store_read(INKTAG_NRF_CONFIG_BOARD, output, 420, &length));
    assert(inktag_nrf_config_store_delete(INKTAG_NRF_CONFIG_BOARD));
    reboot();
    assert(!inktag_nrf_config_store_read(INKTAG_NRF_CONFIG_BOARD, output, sizeof(output), &length));
    /* Lifecycle mask clearing is one generation: after any interrupted write,
     * every selected record is either old or removed, never a partial set.
     * Protected factory/activation/health/boot/anti-replay records survive. */
    memset(flash_bytes, 0xff, sizeof(flash_bytes)); reboot();
    for (unsigned type=1;type<=12;type++) if (type!=8 && type!=9)
        assert(inktag_nrf_config_store_write((inktag_nrf_config_record_type_t)type,old_data,37));
    assert(inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_CONTENT_CLEAR_PENDING,old_data,8));
    /* Seed opaque historical records directly, bypassing no production API.
     * Public reads/writes/deletes deny them; compaction must retain bytes. */
    const unsigned opaque_types[] = {8u, 14u, 29u};
    for (unsigned n=0; n<sizeof(opaque_types)/sizeof(opaque_types[0]); ++n)
        assert(append_to_page(s_active_page, &s_write_offset,
                              opaque_types[n], old_data, 37));
    reboot();
    unsigned no_erase=erase_count, no_write=program_count;
    for (unsigned type=8u; type<=31u; ++type) {
        if (type>=10u && type<=12u) continue;
        uint16_t untouched=123u;
        assert(!inktag_nrf_config_store_write((inktag_nrf_config_record_type_t)type, old_data, 37));
        assert(!inktag_nrf_config_store_delete((inktag_nrf_config_record_type_t)type));
        assert(!inktag_nrf_config_store_read((inktag_nrf_config_record_type_t)type,
                                            output, sizeof(output), &untouched));
        assert(untouched==123u && inktag_nrf_config_store_last_error()==NRF_ERROR_INVALID_PARAM);
    }
    assert(erase_count==no_erase && program_count==no_write);
    memcpy(snapshot,flash_bytes,sizeof(snapshot)); io_count=0;
    assert(inktag_nrf_config_store_clear_records(INKTAG_SETTINGS_RESET_SETTINGS_MASK));
    unsigned clear_ops=io_count;
    for (cut_mode=0;cut_mode<3;cut_mode++) for (unsigned at=0;at<clear_ops;at++) {
        memcpy(flash_bytes,snapshot,sizeof(snapshot));reboot();io_count=0;cut=at;
        (void)inktag_nrf_config_store_clear_records(INKTAG_SETTINGS_RESET_SETTINGS_MASK);reboot();
        int selected=-1;
        for(unsigned type=1;type<=12;type++) if(type!=8 && type!=9) {
            bool found=inktag_nrf_config_store_read((inktag_nrf_config_record_type_t)type,output,sizeof(output),&length);
            if (INKTAG_SETTINGS_RESET_SETTINGS_MASK & INKTAG_SETTINGS_RECORD_BIT(type)) {
                if(selected<0) selected=found; else assert(selected==found);
            } else assert(found);
            if(found) assert(length==37&&!memcmp(output,old_data,37));
        }
        for (unsigned n=0; n<sizeof(opaque_types)/sizeof(opaque_types[0]); ++n) {
            config_record_index_t *raw=&s_records[opaque_types[n]];
            assert(raw->present && raw->length==37u);
            assert(!memcmp(flash_bytes+raw->address,old_data,37u));
        }
        expect_record(INKTAG_NRF_CONFIG_CONTENT_CLEAR_PENDING,old_data,8);
    }
    memcpy(flash_bytes,snapshot,sizeof(snapshot));reboot();
    unsigned before_erase=erase_count, before_write=program_count;
    assert(!inktag_nrf_config_store_clear_records(INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_ACTIVATION)));
    assert(erase_count==before_erase && program_count==before_write);
    assert(inktag_nrf_config_store_clear_records(INKTAG_SETTINGS_CLEAR_USER_DATA_MASK |
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_OWNER_RESET_PENDING)));reboot();
    for(unsigned type=1;type<=12;type++) if(type!=8 && type!=9) {
        bool found=inktag_nrf_config_store_read((inktag_nrf_config_record_type_t)type,output,sizeof(output),&length);
        assert(found==!(INKTAG_SETTINGS_CLEAR_USER_DATA_MASK & INKTAG_SETTINGS_RECORD_BIT(type)));
    }
    assert(!inktag_nrf_config_store_read(INKTAG_NRF_CONFIG_CONTENT_CLEAR_PENDING,output,sizeof(output),&length));
    assert(!sessions);
    printf("stream journal: max4064, chunk128, %u write and %u reset-mask fault boundaries x3; ID32 and protected records passed\n", operations,clear_ops);
}
