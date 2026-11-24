/* base: plain device-info QR writes and fail-closed existing-record reads. */
#include "inktag_device_password_store.h"
#include "inktag_protocol_core.h"
#include "inktag_sha256.h"
#include <string.h>

static bool matrix_valid(uint8_t modules, const uint8_t *matrix, uint16_t n) {
    inktag_transfer_descriptor_t d;
    memset(&d, 0, sizeof(d));
    d.object_type = INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR;
    d.format = modules; d.segment_count = 1u;
    d.total_size = d.segment_size[0] = n;
    if (!matrix || !inktag_device_info_qr_transfer_valid(&d)) return false;
    uint8_t padding = (uint8_t)(n * 8u - modules * modules);
    return !padding || !(matrix[n - 1u] & ((1u << padding) - 1u));
}

bool inktag_device_password_store_record_decode(const uint8_t *wire, uint16_t n,
    inktag_device_password_config_t *c, uint8_t *modules, uint16_t *offset) {
    if (!c) return false;
    memset(c, 0, sizeof(*c));
    if (!wire || !modules || !offset || n > 420u) return false;
    *modules = 0u; *offset = 0u;
    if (n >= 2u && wire[0] == INKTAG_DEVICE_PASSWORD_BUNDLE_VERSION && wire[1] == 1u) {
        if (!inktag_device_password_record_decode(wire, n, c)) return false;
        *modules = wire[2]; *offset = INKTAG_DEVICE_PASSWORD_BUNDLE_HEADER_BYTES;
        return true;
    }
    if (n < INKTAG_DEVICE_PASSWORD_PLAIN_HEADER_BYTES ||
        wire[0] != INKTAG_DEVICE_PASSWORD_BUNDLE_VERSION || wire[1] != 0u ||
        inktag_read_u16_le(wire + 3u) != n - INKTAG_DEVICE_PASSWORD_PLAIN_HEADER_BYTES ||
        !matrix_valid(wire[2], wire + INKTAG_DEVICE_PASSWORD_PLAIN_HEADER_BYTES,
                      (uint16_t)(n - INKTAG_DEVICE_PASSWORD_PLAIN_HEADER_BYTES)) ||
        (inktag_crc32_update(0xffffffffu, wire + INKTAG_DEVICE_PASSWORD_PLAIN_HEADER_BYTES,
            (uint16_t)(n - INKTAG_DEVICE_PASSWORD_PLAIN_HEADER_BYTES)) ^
         0xffffffffu) != inktag_read_u32_le(wire + 5u)) return false;
    c->valid = true; *modules = wire[2]; *offset = INKTAG_DEVICE_PASSWORD_PLAIN_HEADER_BYTES;
    return true;
}

inktag_device_password_store_result_t inktag_device_password_store_load_io(
    const inktag_device_password_store_io_t *io, uint8_t scratch[420],
    inktag_device_password_config_t *c, uint8_t *modules,
    uint16_t *offset, uint16_t *length) {
    if (!c) return INKTAG_PASSWORD_STORE_ERROR;
    memset(c, 0, sizeof(*c));
    if (!io || !io->read || !scratch || !modules || !offset || !length)
        return INKTAG_PASSWORD_STORE_ERROR;
    *length = *offset = 0u; *modules = 0u;
    inktag_device_password_store_result_t r = io->read(io->context, scratch, 420u, length);
    if (r == INKTAG_PASSWORD_STORE_MISSING && !*length) {
        c->valid = true; return INKTAG_PASSWORD_STORE_MISSING;
    }
    if (r == INKTAG_PASSWORD_STORE_CORRUPT) return INKTAG_PASSWORD_STORE_CORRUPT;
    if (r != INKTAG_PASSWORD_STORE_OK) return INKTAG_PASSWORD_STORE_ERROR;
    return inktag_device_password_store_record_decode(scratch, *length, c, modules, offset) ?
        INKTAG_PASSWORD_STORE_OK : INKTAG_PASSWORD_STORE_CORRUPT;
}

bool inktag_device_password_store_qr(const inktag_device_password_config_t *c,
    uint8_t modules, const uint8_t *matrix, uint16_t n,
    uint8_t output[420], uint16_t *length) {
    uint16_t header;
    if (!c || !c->valid || c->enabled || !output || !length || !matrix_valid(modules, matrix, n))
        return false;
    header = INKTAG_DEVICE_PASSWORD_PLAIN_HEADER_BYTES;
    if (header + n > 420u) return false;
    memmove(output + header, matrix, n);
    memset(output, 0, header);
    output[0] = INKTAG_DEVICE_PASSWORD_BUNDLE_VERSION; output[2] = modules;
    inktag_write_u16_le(output + 3u, n);
    inktag_write_u32_le(output + 5u,
        inktag_crc32_update(0xffffffffu, output + header, n) ^ 0xffffffffu);
    *length = header + n;
    return true;
}

inktag_status_v2_t inktag_device_password_store_prepare_bundle(
    inktag_device_password_t *session, const inktag_device_password_config_t *c,
    uint32_t now_ms, uint8_t scratch[480], uint16_t *length) {
    (void)session; (void)c; (void)now_ms; (void)scratch; (void)length;
    return INKTAG_STATUS_UNSUPPORTED;
}

bool inktag_device_password_store_commit_io(const inktag_device_password_store_io_t *io,
    uint8_t scratch[420], uint16_t n, inktag_device_password_config_t *actual) {
    uint8_t expected[32], observed[32], modules;
    uint16_t offset, length;
    inktag_device_password_config_t parsed;
    if (!io || !io->read || !io->put_atomic || !scratch || !actual || n > 420u ||
        !inktag_device_password_store_record_decode(scratch, n, &parsed, &modules, &offset) ||
        parsed.enabled) return false;
    memset(&parsed, 0, sizeof(parsed));
    inktag_sha256(scratch, n, expected);
    bool saved = io->put_atomic(io->context, scratch, n);
    inktag_device_password_store_result_t result = inktag_device_password_store_load_io(
        io, scratch, actual, &modules, &offset, &length);
    if (result != INKTAG_PASSWORD_STORE_OK) return false;
    inktag_sha256(scratch, length, observed);
    uint8_t diff = 0u;
    for (uint8_t i = 0u; i < 32u; ++i) diff |= expected[i] ^ observed[i];
    return saved && length == n && !diff;
}

static inktag_device_password_store_result_t journal_read(
    void *context, uint8_t *output, uint16_t capacity, uint16_t *length) {
    inktag_settings_journal_read_result_t result = inktag_settings_journal_read_result(
        context, INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR, output, capacity, length);
    if (result == INKTAG_SETTINGS_JOURNAL_READ_FOUND) return INKTAG_PASSWORD_STORE_OK;
    if (result == INKTAG_SETTINGS_JOURNAL_READ_MISSING) return INKTAG_PASSWORD_STORE_MISSING;
    return INKTAG_PASSWORD_STORE_ERROR;
}

static bool journal_put(void *context, const uint8_t *wire, uint16_t length) {
    return inktag_settings_journal_put_record_atomic(
        context, INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR, wire, length);
}

inktag_device_password_store_result_t inktag_device_password_store_load(
    inktag_settings_journal_t *journal, uint8_t scratch[420],
    inktag_device_password_config_t *config, uint8_t *modules,
    uint16_t *offset, uint16_t *length) {
    const inktag_device_password_store_io_t io = { journal, journal_read, journal_put };
    return inktag_device_password_store_load_io(journal ? &io : NULL,
                                               scratch, config, modules, offset, length);
}

bool inktag_device_password_store_commit(inktag_settings_journal_t *journal,
    uint8_t scratch[420], uint16_t length, inktag_device_password_config_t *actual) {
    const inktag_device_password_store_io_t io = { journal, journal_read, journal_put };
    return inktag_device_password_store_commit_io(journal ? &io : NULL, scratch, length, actual);
}
