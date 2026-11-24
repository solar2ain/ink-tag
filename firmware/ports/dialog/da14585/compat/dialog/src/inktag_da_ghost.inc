/* Canonical DISPLAY_ACTION/GHOST_CLEAN: white-black-white, 1..10 cycles.
 * Each pass reinitializes the panel (some recipes power off after refresh).
 * The normal asynchronous worker owns BUSY, timeout, cancellation and GPIOs.
 * No framebuffer, image-slot write or controller-specific raw polarity here. */
__attribute__((noinline)) static bool ghost_clean_fill_plane(
    bool black_plane, uint8_t value, uint32_t length)
{
    uint8_t buffer[64];
    bool begin = true;
    memset(buffer, value, sizeof(buffer));
    while (length != 0u) {
        uint8_t chunk = length > sizeof(buffer) ? sizeof(buffer) : (uint8_t)length;
        if (EPD_AbortRequested() ||
            !EPD_DriverWriteRam(s_epd, begin, black_plane, buffer, chunk)) return false;
        begin = false;
        length -= chunk;
        app_service_ble();
    }
    return EPD_DialogSpiReady() && EPD_DialogSpiLastError() == 0;
}

__attribute__((noinline)) static bool ghost_clean_write_solid(bool white)
{
    epd_screen_t resolved;
    static const uint8_t white_value[] = {0xffu, 0xffu, 0x55u, 0xffu, 0x11u, 0x55u};
    uint8_t value;
    uint8_t color_value;
    bool two_planes;
    uint32_t length;
    if (epd_screen_resolve(&s_screen_config, &resolved) != EPD_SCREEN_STATUS_OK)
        return false;
    two_planes = resolved.transfer_format == EPD_SCREEN_TRANSFER_DUAL_1BPP ||
                 resolved.transfer_format == EPD_SCREEN_TRANSFER_PLANAR_2X1BPP ||
                 resolved.transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2X2BPP;
    /* Successful common resolution has exactly the six registered formats. */
    value = white ? white_value[resolved.transfer_format - 1u] : 0u;
    color_value = resolved.transfer_format == EPD_SCREEN_TRANSFER_DUAL_1BPP ? 0xffu : value;
    length = two_planes ? resolved.frame_bytes / 2u : resolved.frame_bytes;
    return ghost_clean_fill_plane(true, value, length) &&
           (!two_planes || ghost_clean_fill_plane(false, color_value, length));
}

/* UINT8_MAX is the one best-effort white cleanup pass, never another cycle.
 * Keep the original error even if cleanup succeeds. Do not bypass the physical
 * display admission gate. The outer loop drains CANCEL before each next pass. */
static bool ghost_clean_continue(inktag_status_v2_t status)
{
    if (s_ghost_pass == UINT8_MAX) return false;
    if (status == INKTAG_STATUS_OK) {
        uint8_t total = (uint8_t)(s_active_work_arg * 3u);
        uint8_t progress[2] = {INKTAG_BUSY_PHASE_DISPLAY,
            (uint8_t)((++s_ghost_pass * 100u) / total)};
        send_v2_event(INKTAG_EVENT_PROGRESS, INKTAG_STATUS_OK,
                      s_v2_work_opcode, s_v2_work_transaction,
                      progress, sizeof(progress));
        return s_ghost_pass < total;
    }
    if (!inktag_da_health_operation_allowed(INKTAG_POWER_OPERATION_DISPLAY, true))
        return false;
    s_ghost_result = (uint8_t)status;
    s_ghost_pass = UINT8_MAX;
    EPD_Reset(true, 10u);
    return true;
}
