/* SDK6 owns queued notification memory. Build directly in that final request
 * instead of retaining a second 220-byte automatic packet on every response
 * and event path. Included by the port and exercised with the same code by
 * the offline SDK-ownership/byte-contract harness. */
static struct custs1_val_ntf_ind_req *notify_allocate(uint16_t length)
{
    struct custs1_val_ntf_ind_req *req = KE_MSG_ALLOC_DYN(CUSTS1_VAL_NTF_REQ,
        prf_get_task_from_id(TASK_ID_CUSTS1), TASK_APP,
        custs1_val_ntf_ind_req, length);
    req->handle = INKTAG_IDX_EPD_VAL;
    req->length = length;
    req->notification = true;
    return req;
}

static void notify_commit(struct custs1_val_ntf_ind_req *req)
{
    if (s_notify_outstanding != UINT8_MAX) s_notify_outstanding++;
    KE_MSG_SEND(req);
}

static bool notify_available(void)
{
    return s_notify_enabled && s_connection_idx != GAP_INVALID_CONIDX;
}

static void send_bytes(const uint8_t *data, uint16_t length)
{
    struct custs1_val_ntf_ind_req *req;
    if (!notify_available() || length == 0u) return;
    if (length > INKTAG_DA_PROTOCOL_PACKET_BYTES)
        length = INKTAG_DA_PROTOCOL_PACKET_BYTES;
    req = notify_allocate(length);
    memcpy(req->value, data, length);
    notify_commit(req);
}

static void send_v2_response(uint8_t opcode, uint8_t transaction_id,
                             inktag_status_v2_t status,
                             const uint8_t *payload, uint16_t payload_length)
{
    struct custs1_val_ntf_ind_req *req;
    if (!notify_available()) return;
    if (payload_length > INKTAG_DA_PROTOCOL_PACKET_BYTES - INKTAG_PROTOCOL_V2_HEADER_SIZE) {
        status = INKTAG_STATUS_INTERNAL_ERROR;
        payload_length = 0u;
    }
    req = notify_allocate(INKTAG_PROTOCOL_V2_HEADER_SIZE + payload_length);
    req->value[0] = opcode;
    req->value[1] = transaction_id;
    req->value[2] = (uint8_t)status;
    if (payload != NULL && payload_length > 0u)
        memcpy(&req->value[INKTAG_PROTOCOL_V2_HEADER_SIZE], payload, payload_length);
    notify_commit(req);
}

/* A status-only reply has three arguments, all in Cortex-M0 registers. Keep
 * its zero payload/length setup in one place instead of every command branch.
 * Notification ownership and the wire encoder remain the same. */
__attribute__((noinline)) static void send_v2_status(
    uint8_t opcode, uint8_t transaction_id, inktag_status_v2_t status)
{
    send_v2_response(opcode, transaction_id, status, NULL, 0u);
}

static void send_v2_event(uint8_t event, inktag_status_v2_t status,
                          uint8_t origin_opcode, uint8_t origin_transaction,
                          const uint8_t *payload, uint16_t payload_length)
{
    struct custs1_val_ntf_ind_req *req;
    const uint16_t prefix = INKTAG_PROTOCOL_V2_HEADER_SIZE + 2u;
    if (!notify_available()) return;
    if (payload_length > INKTAG_DA_PROTOCOL_PACKET_BYTES - prefix)
        payload_length = INKTAG_DA_PROTOCOL_PACKET_BYTES - prefix;
    req = notify_allocate(prefix + payload_length);
    req->value[0] = event;
    req->value[1] = 0u;
    req->value[2] = (uint8_t)status;
    req->value[3] = origin_opcode;
    req->value[4] = origin_transaction;
    if (payload != NULL && payload_length > 0u)
        memcpy(&req->value[prefix], payload, payload_length);
    notify_commit(req);
}
