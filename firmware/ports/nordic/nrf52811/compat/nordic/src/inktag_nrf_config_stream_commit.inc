/* Write the replacement directly into the uncommitted target page. Copying
 * the old value first would require old+new space and reject a large update
 * even when the final live set fits. Page magic is the final transaction commit. */
static bool stream_compact_pair(uint32_t clear_mask, uint16_t changed_type, const void *data,
                               uint16_t length, uint16_t other_type,
                               const void *other, uint16_t other_length) {
    uint32_t needed = sizeof(config_page_header_t);
    uint32_t old_page = s_active_page;
    uint32_t target = old_page == CONFIG_STORE_START ?
                      CONFIG_STORE_START + CONFIG_STORE_PAGE_SIZE : CONFIG_STORE_START;
    uint32_t generation = s_generation + 1u;
    uint16_t offset = sizeof(config_page_header_t);
    for (uint16_t type = 1u; type <= INKTAG_NRF_CONFIG_RECORD_MAX; ++type)
        if (type != changed_type && type != other_type &&
            !(clear_mask & INKTAG_SETTINGS_RECORD_BIT(type)) && s_records[type].present)
            needed += record_size(s_records[type].length);
    if (length) needed += record_size(length);
    if (other_length) needed += record_size(other_length);
    if (needed > CONFIG_STORE_PAGE_SIZE) {
        s_last_error = NRF_ERROR_DATA_SIZE;
        return false;
    }
    if (!flash_erase(target) || !write_page_header(target, generation, false)) goto failed;
    for (uint16_t type = 1u; type <= INKTAG_NRF_CONFIG_RECORD_MAX; ++type) {
        config_record_index_t record = s_records[type];
        if (type == changed_type || type == other_type ||
            (clear_mask & INKTAG_SETTINGS_RECORD_BIT(type)) || !record.present) continue;
        if (!stream_append(target, &offset, type, NULL, record.length, record.address)) goto failed;
    }
    if (length && !append_to_page(target, &offset, changed_type, data, length)) goto failed;
    if (other_length && !append_to_page(target, &offset, other_type, other, other_length)) goto failed;
    if (!write_page_header(target, generation, true)) goto failed;
    s_active_page = target;
    s_generation = generation;
    if (!scan_page(target, true, &s_write_offset)) goto failed;
    (void)flash_erase(old_page);
    s_last_error = NRF_SUCCESS;
    return true;
failed:
    /* The page/record commit may have physically succeeded before an I/O
     * error. Re-initialize from persistent generations before any new write. */
    s_initialized = false;
    return false;
}

static bool stream_compact_change(uint32_t mask, uint16_t type, const void *data,
                                  uint16_t length) {
    return stream_compact_pair(mask, type, data, length, 0u, NULL, 0u);
}

static bool stream_compact_update(uint16_t type, const void *data, uint16_t length) {
    return stream_compact_change(0u, type, data, length);
}
