/* SDK6 transport binding of the public safe-resume semantic. Native status
 * bytes and the existing public IK rejection tail are unchanged. ACK is only
 * acceptance: its correlated transport completion schedules validation and
 * the existing packaged pP handoff in the main loop, never in a BLE callback. */
static bool s_inktag_safe_resume_pending;
static bool s_inktag_safe_resume_ack_confirmed;
static bool s_inktag_safe_resume_running;
static uint16_t s_inktag_safe_resume_sequence;
static uint16_t s_inktag_safe_resume_ack_sequence;

bool inktag_da_safe_resume_busy(void)
{
    return s_inktag_safe_resume_pending;
}

/* Captured into the internal SDK status message when it is queued, never
 * inferred later from a stale status attribute or an unrelated COMPLETE. */
uint16_t inktag_da_safe_resume_status_sequence(uint8_t status)
{
    return s_inktag_safe_resume_pending && status == SUOTAR_CMP_OK ?
        s_inktag_safe_resume_ack_sequence : 0u;
}

void inktag_da_safe_resume_tx_complete(uint8_t connection_idx,
                                     uint16_t sequence, bool success)
{
    if (!s_inktag_safe_resume_pending || sequence == 0u ||
        sequence != s_inktag_safe_resume_ack_sequence ||
        connection_idx == GAP_INVALID_CONIDX ||
        connection_idx != app_env[0].conidx) return;
    s_inktag_safe_resume_ack_sequence = 0u; /* Consume before any SDK call. */
    if (!success) {
        s_inktag_safe_resume_pending = false;
        return;
    }
    /* The ACK has completed at the transport. Do not allocate a disconnect
     * command or run Flash/reset work here. The main-loop callback owns it. */
    s_inktag_safe_resume_ack_confirmed = true;
}

void inktag_da_safe_resume_request(uint32_t command, bool finalize_pending)
{
    if (command != 0xFC000000u) {
        suotar_send_status_update_req((uint8_t)SUOTAR_INVAL_MEM_TYPE);
        return;
    }
    if (s_inktag_safe_resume_pending || finalize_pending ||
        suota_state.mem_dev < SUOTAR_MEM_INVAL_DEV ||
        suota_state.suota_secure_lock || suota_state.reboot_requested) {
        suotar_send_status_update_req((uint8_t)SUOTAR_LOCKED_ERR);
        return;
    }
    inktag_da_ota_reject_clear();
    if (!inktag_da_recovery_resume_allowed()) {
        suotar_send_status_update_req((uint8_t)SUOTAR_EXT_MEM_WRITE_ERR);
        return;
    }
    s_inktag_safe_resume_pending = true;
    s_inktag_safe_resume_ack_confirmed = false;
    if (++s_inktag_safe_resume_sequence == 0u) ++s_inktag_safe_resume_sequence;
    s_inktag_safe_resume_ack_sequence = s_inktag_safe_resume_sequence;
    suotar_send_status_update_req((uint8_t)SUOTAR_CMP_OK);
    /* Pending ownership blocks other SUOTA commands/data until the main-loop
     * continuation completes; neither enqueue nor peer disconnect is an ACK. */
}

static void inktag_da_safe_resume_on_disconnect(void)
{
    /* As in Nordic, a disconnect before confirmed ACK delivery cancels the
     * continuation. A confirmed request survives client-initiated release. */
    if (s_inktag_safe_resume_ack_confirmed || s_inktag_safe_resume_running) return;
    s_inktag_safe_resume_pending = false;
    s_inktag_safe_resume_ack_sequence = 0u;
}

/* LTO must not merge the runtime CRC/journal frame into SDK main: that frame
 * would also remain allocated during the unrelated TRNG startup peak. */
__attribute__((noinline))
arch_main_loop_callback_ret_t user_app_on_system_powered(void)
{
    if (s_inktag_safe_resume_pending &&
        s_inktag_safe_resume_ack_confirmed && !s_inktag_safe_resume_running) {
        bool handed_off = false;
        s_inktag_safe_resume_running = true;
        s_inktag_safe_resume_ack_confirmed = false;
        /* This repeats the full CRC, current voltage and durable-state
         * checks before clearing the request marker. Failure stays Recovery.
         * Use the same bounded pP reload as App restart and OTA completion,
         * rather than introducing a separate BootROM/reset startup path.
         * Keep ownership while Flash waits service SDK/controller events. */
        if (inktag_da_recovery_resume_application()) {
            handed_off = inktag_da_restart_via_secondary_bootloader();
            if (!handed_off)
                inktag_da_ota_reject_set_default(INKTAG_OTA_REJECT_REASON_INTERNAL,
                    INKTAG_OTA_REJECT_STAGE_MODE_SWITCH,
                    INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS, 0u);
        }
        /* A successful production handoff never returns. A preflight refusal
         * returns here without a reset; expose it once and never replay. */
        s_inktag_safe_resume_running = false;
        s_inktag_safe_resume_pending = false;
        if (!handed_off)
            suotar_send_status_update_req((uint8_t)SUOTAR_EXT_MEM_WRITE_ERR);
    }
    return GOTO_SLEEP;
}
