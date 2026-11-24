/* Optional bounded-RAM journal experiment. The legacy implementation remains
 * in config_store.c for source comparison; persistent wire encoding is shared. */
static uint32_t stream_checksum_prefix(uint16_t type, uint16_t length) {
    uint32_t checksum = checksum_update(2166136261u, &type, sizeof(type));
    return checksum_update(checksum, &length, sizeof(length));
}

static bool stream_checksum(uint32_t address, uint16_t type, uint16_t length,
                            uint32_t *checksum) {
    *checksum = stream_checksum_prefix(type, length);
    while (length) {
        uint16_t chunk = length > sizeof(s_io_buffer) ? sizeof(s_io_buffer) : length;
        if (!flash_read(address, s_io_buffer, chunk)) return false;
        *checksum = checksum_update(*checksum, s_io_buffer, chunk);
        address += chunk;
        length -= chunk;
    }
    return true;
}

static bool stream_append(uint32_t page, uint16_t *offset, uint16_t type,
                          const void *data, uint16_t length, uint32_t source) {
    config_record_header_t header;
    uint32_t address = page + *offset;
    uint32_t checksum = stream_checksum_prefix(type, length);
    uint32_t commit = CONFIG_RECORD_COMMIT;
    uint16_t copied = 0u, body = (uint16_t)((length + 3u) & ~3u);
    if (length > CONFIG_MAX_PAYLOAD ||
        (uint32_t)*offset + record_size(length) > CONFIG_STORE_PAGE_SIZE) return false;
    if (source) {
        if (!flash_read(source - sizeof(header), &header, sizeof(header)) ||
            header.magic != CONFIG_RECORD_MAGIC || header.type != type ||
            header.length != length) return false;
    } else {
        header.magic = CONFIG_RECORD_MAGIC;
        header.type = type;
        header.length = length;
        header.checksum = record_checksum(type, data, length);
    }
    if (!flash_write(address, &header, sizeof(header))) goto failed;
    address += sizeof(header);
    while (copied < body) {
        uint16_t chunk = body - copied > sizeof(s_io_buffer) ?
                         sizeof(s_io_buffer) : (uint16_t)(body - copied);
        uint16_t bytes = copied < length ? (uint16_t)(length - copied) : 0u;
        if (bytes > chunk) bytes = chunk;
        memset(s_io_buffer, 0xff, chunk);
        if (source) {
            if (bytes && !flash_read(source + copied, s_io_buffer, bytes)) goto failed;
        } else if (bytes) memcpy(s_io_buffer, (const uint8_t *)data + copied, bytes);
        checksum = checksum_update(checksum, s_io_buffer, bytes);
        if (!flash_write(address + copied, s_io_buffer, chunk)) goto failed;
        copied += chunk;
    }
    /* A damaged source is never republished with a fresh checksum. */
    if (checksum != header.checksum ||
        !flash_write(address + body, &commit, sizeof(commit))) goto failed;
    *offset = (uint16_t)(*offset + record_size(length));
    return true;
failed:
    s_tail_clean = false;
    return false;
}
