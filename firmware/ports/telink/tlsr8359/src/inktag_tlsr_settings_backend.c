#include "main.h"

#include <string.h>

#include "inktag_tlsr_settings_backend.h"
#include "inktag_settings_records.h"

static bool range_valid(const inktag_tlsr_settings_backend_t *backend,
                        uint32_t address, uint16_t length) {
    uint32_t end = address + length;
    return backend && length != 0u && end >= address &&
        address >= backend->io.start_address &&
        end <= backend->io.start_address + INKTAG_SETTINGS_JOURNAL_SIZE;
}

static bool backend_read(void *context, uint32_t address, void *data,
                         uint16_t length) {
    inktag_tlsr_settings_backend_t *backend =
        (inktag_tlsr_settings_backend_t *)context;
    if (!data || !range_valid(backend, address, length)) return false;
    flash_read_page(address, length, (uint8_t *)data);
    return true;
}
static bool backend_program(void *context, uint32_t address,
                            const void *data, uint16_t length) {
    inktag_tlsr_settings_backend_t *backend =
        (inktag_tlsr_settings_backend_t *)context;
    const uint8_t *source = (const uint8_t *)data;
    uint8_t verify[32];
    if (!source || !range_valid(backend, address, length)) return false;
    while (length != 0u) {
        uint16_t room = (uint16_t)(256u - (address & 0xFFu));
        uint16_t count = length < room ? length : room;
        uint16_t verified = 0u;
        if (!inktag_tlsr_flash_write_page(address, count, source,
                INKTAG_POWER_OPERATION_FLASH)) return false;
        while (verified < count) {
            uint16_t chunk = (uint16_t)(count - verified);
            if (chunk > sizeof(verify)) chunk = sizeof(verify);
            flash_read_page(address + verified, chunk, verify);
            if (memcmp(verify, source + verified, chunk) != 0) return false;
            verified = (uint16_t)(verified + chunk);
        }
        address += count;
        source += count;
        length = (uint16_t)(length - count);
    }
    return true;
}

static bool backend_erase(void *context, uint32_t address) {
    inktag_tlsr_settings_backend_t *backend =
        (inktag_tlsr_settings_backend_t *)context;
    uint8_t verify[32];
    uint16_t offset = 0u;
    if (!backend || (address != backend->io.start_address &&
         address != backend->io.start_address +
                    INKTAG_SETTINGS_JOURNAL_PAGE_SIZE) ||
        !inktag_tlsr_flash_erase_sector(address,
            INKTAG_POWER_OPERATION_FLASH)) return false;
    while (offset < INKTAG_SETTINGS_JOURNAL_PAGE_SIZE) {
        uint8_t index;
        uint16_t count = (uint16_t)(
            INKTAG_SETTINGS_JOURNAL_PAGE_SIZE - offset);
        if (count > sizeof(verify)) count = sizeof(verify);
        flash_read_page(address + offset, count, verify);
        for (index = 0u; index < count; ++index) {
            if (verify[index] != 0xFFu) return false;
        }
        offset = (uint16_t)(offset + count);
    }
    return true;
}

bool inktag_tlsr_settings_backend_configure(
    inktag_tlsr_settings_backend_t *backend,
    uint32_t start_address, uint32_t allowed_records) {
    if (!backend || allowed_records == 0u ||
        (start_address & (INKTAG_SETTINGS_JOURNAL_PAGE_SIZE - 1u)) != 0u)
        return false;
    memset(backend, 0, sizeof(*backend));
    backend->io.context = backend;
    backend->io.start_address = start_address;
    backend->io.allowed_records = allowed_records;
    backend->io.read = backend_read;
    backend->io.write = backend_program;
    backend->io.erase = backend_erase;
    return true;
}

bool inktag_tlsr_settings_backend_pair_erased(
    inktag_tlsr_settings_backend_t *backend) {
    uint8_t bytes[32];
    uint32_t offset = 0u;
    if (!backend) return false;
    while (offset < INKTAG_SETTINGS_JOURNAL_SIZE) {
        uint8_t index;
        uint16_t count = (uint16_t)(INKTAG_SETTINGS_JOURNAL_SIZE - offset);
        if (count > sizeof(bytes)) count = sizeof(bytes);
        if (!backend_read(backend, backend->io.start_address + offset,
                          bytes, count)) return false;
        for (index = 0u; index < count; ++index) {
            if (bytes[index] != 0xFFu) return false;
        }
        offset += count;
    }
    return true;
}
