#!/usr/bin/env python3
"""Three bounded hooks in byte-exact SDK17.1 SecureDFU; vendor tree stays immutable."""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

HASHES = {
    "nrf_dfu_req_handler.c": "862f32ca53e3921a41458732346214f2a534c7a4f43204875805205c02a17d7e",
    "nrf_dfu_ble.c": "84a021d35df99537bad20a7838df9d01cc203e29e6d7188a73c2c3bb1611a9bd",
    "nrf_bootloader.c": "4a90df5aef10dc3719e715e83884f7e68979335e88b2bd59715ab775da89c08d",
}


def replace_once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise ValueError(f"expected one SDK hook: {old[:80]!r}")
    return text.replace(old, new, 1)


def materialize(path: Path) -> str:
    payload = path.read_bytes()
    if hashlib.sha256(payload).hexdigest() != HASHES[path.name]:
        raise ValueError(f"Nordic resume source hash mismatch: {path.name}")
    text = payload.decode("utf-8").replace("\r\n", "\n")
    if path.name == "nrf_dfu_req_handler.c":
        text = replace_once(text, '#include "nrf_crypto.h"\n', '')
        text = '#include "inktag_recovery_version.h"\n' + text
        # This function was itself inside the SDK's full-protocol-only block.
        # Keep protocol/hardware/ping/MTU excluded, expose only FW_VERSION.
        text = replace_once(text,
            "static void on_fw_version_request(",
            "#endif // !NRF_DFU_PROTOCOL_REDUCED\n\nstatic void on_fw_version_request(")
        text = replace_once(text,
            "static void on_ping_request(",
            "#if !NRF_DFU_PROTOCOL_REDUCED\nstatic void on_ping_request(")
        # Settings.bootloader_version carries the pending App in single-slot
        # admission. It is never the running Recovery's software identity.
        text = replace_once(text,
            "p_res->firmware.version = s_dfu_settings.bootloader_version;",
            """p_res->firmware.version =
                ((uint32_t)INKTAG_RECOVERY_VERSION_MAJOR << 16) |
                ((uint32_t)INKTAG_RECOVERY_VERSION_MINOR << 8) |
                (uint32_t)INKTAG_RECOVERY_VERSION_PATCH;""")
        # Reduced BLE exposes only its resident Recovery image. Leave the
        # vendor's full-protocol SD/App enumeration unchanged when enabled.
        text = replace_once(text, "        uint8_t fw_count = 1;",
                            "#if !NRF_DFU_PROTOCOL_REDUCED\n        uint8_t fw_count = 1;")
        text = replace_once(text, "        p_res->result = NRF_DFU_RES_CODE_SUCCESS;\n\n        if (p_req->firmware.image_number == 0)",
                            "#endif\n        p_res->result = NRF_DFU_RES_CODE_SUCCESS;\n\n        if (p_req->firmware.image_number == 0)")
        text = replace_once(text, "        else if ((p_req->firmware.image_number == 1) && SD_PRESENT)",
                            "#if !NRF_DFU_PROTOCOL_REDUCED\n        else if ((p_req->firmware.image_number == 1) && SD_PRESENT)")
        text = replace_once(text, '        else\n        {\n            NRF_LOG_DEBUG("No such firmware image");',
                            '#endif\n        else\n        {\n            NRF_LOG_DEBUG("No such firmware image");')
        text = replace_once(text, "    m_observer(NRF_DFU_EVT_DFU_ABORTED);",
            "    p_res->result = inktag_nrf_dfu_resume_prepare();")
        text = replace_once(text, "    switch (p_req->request)\n    {\n#if !NRF_DFU_PROTOCOL_REDUCED", """
    if (inktag_nrf_dfu_resume_pending() &&
        (p_req->request == NRF_DFU_OP_OBJECT_CREATE ||
         p_req->request == NRF_DFU_OP_OBJECT_WRITE ||
         p_req->request == NRF_DFU_OP_OBJECT_EXECUTE))
    {
        response.result = NRF_DFU_RES_CODE_OPERATION_NOT_PERMITTED;
        if (p_req->request == NRF_DFU_OP_OBJECT_WRITE && p_req->callback.write)
            p_req->callback.write((void *)p_req->write.p_data);
        p_req->callback.response(&response, p_req->p_context);
        return;
    }
    switch (p_req->request)
    {
#if NRF_DFU_PROTOCOL_REDUCED
        case NRF_DFU_OP_FIRMWARE_VERSION:
            on_fw_version_request(p_req, &response);
            break;
        case NRF_DFU_OP_PROTOCOL_VERSION:
            response.protocol.version = g_inktag_nrf_recovery_protocol;
            break;
#endif
#if !NRF_DFU_PROTOCOL_REDUCED""")
        text = replace_once(text,
            "    ret = app_sched_event_put(p_req, sizeof(nrf_dfu_request_t), nrf_dfu_req_handler_req);",
            """    /* Mark admission before scheduler ordering or queue-full failure can
     * make a mutating transaction look idle to a following exit request. */
    if (p_req->request == NRF_DFU_OP_OBJECT_CREATE ||
        p_req->request == NRF_DFU_OP_OBJECT_WRITE ||
        p_req->request == NRF_DFU_OP_OBJECT_EXECUTE)
        inktag_nrf_dfu_resume_touch();
    ret = app_sched_event_put(p_req, sizeof(nrf_dfu_request_t), nrf_dfu_req_handler_req);""")
    elif path.name == "nrf_dfu_ble.c":
        text = '#include "inktag_nrf_ble_address.h"\n' + text
        text = replace_once(text, "    err_code = sd_ble_gap_addr_get(&addr);",
                            "    err_code = inktag_nrf_ble_address_read(&addr);")
        text = replace_once(text, "    addr.addr[0] += 1;", """    /* NRF32 same-address pilot: keep the factory InkTag address, never a
     * mutable label or Find address. Default preserves the pinned SDK policy. */
#if !defined(INKTAG_NRF_RECOVERY_SAME_MAC) || !INKTAG_NRF_RECOVERY_SAME_MAC
    addr.addr[0] += 1;
#endif""")
        text = replace_once(text,
            "    switch (p_res->request)\n    {\n        case NRF_DFU_OP_OBJECT_CREATE:",
            """    switch (p_res->request)
    {
        case NRF_DFU_OP_PROTOCOL_VERSION:
            buffer[len++] = p_res->protocol.version;
            break;
        case NRF_DFU_OP_FIRMWARE_VERSION:
            buffer[len++] = (uint8_t)p_res->firmware.type;
            len += uint32_encode(p_res->firmware.version, buffer + len);
            len += uint32_encode(p_res->firmware.addr, buffer + len);
            len += uint32_encode(p_res->firmware.len, buffer + len);
            break;
        case NRF_DFU_OP_OBJECT_CREATE:""")
        text = replace_once(text,
            "        (void) response_send(buffer, len);\n        return;",
            """        uint32_t const sent = response_send(buffer, len);
        inktag_nrf_dfu_resume_response_sent(p_res->request, p_res->result, sent);
        return;""")
        text = replace_once(text,
            "    (void) response_send(buffer, len);\n}",
            """    uint32_t const sent = response_send(buffer, len);
    inktag_nrf_dfu_resume_response_sent(p_res->request, p_res->result, sent);
}""")
        text = replace_once(text,
            "    //lint -save -e415 -e416 : Out-of-bounds access on p_ble_write_evt->data",
            """    if (p_ble_write_evt->len == 0u) return NRF_ERROR_INVALID_LENGTH;
    if (((p_ble_write_evt->data[0] == NRF_DFU_OP_PROTOCOL_VERSION ||
         p_ble_write_evt->data[0] == NRF_DFU_OP_ABORT) &&
         p_ble_write_evt->len != 1u) ||
        (p_ble_write_evt->data[0] == NRF_DFU_OP_FIRMWARE_VERSION &&
         p_ble_write_evt->len != 2u))
    {
        nrf_dfu_response_t response = {
            .request = (nrf_dfu_op_t)p_ble_write_evt->data[0],
            .result = NRF_DFU_RES_CODE_INVALID_PARAMETER,
        };
        ble_dfu_req_handler_callback(&response, p_dfu);
        return NRF_SUCCESS;
    }
    //lint -save -e415 -e416 : Out-of-bounds access on p_ble_write_evt->data""")
        text = replace_once(text,
            "    switch (request.request)\n    {\n        case NRF_DFU_OP_OBJECT_SELECT:",
            """    switch (request.request)
    {
        case NRF_DFU_OP_FIRMWARE_VERSION:
            request.firmware.image_number = p_ble_write_evt->data[1];
            break;
        case NRF_DFU_OP_OBJECT_SELECT:""")
        text = replace_once(text,
            "        case BLE_GAP_EVT_DISCONNECTED:\n        {\n            m_conn_handle = BLE_CONN_HANDLE_INVALID;",
            """        case BLE_GATTS_EVT_HVN_TX_COMPLETE:
            inktag_nrf_dfu_resume_tx_complete(
                p_ble_evt->evt.gatts_evt.params.hvn_tx_complete.count);
            break;
        case BLE_GAP_EVT_DISCONNECTED:
        {
            inktag_nrf_dfu_resume_disconnect();
            m_conn_handle = BLE_CONN_HANDLE_INVALID;""")
    else:
        text = '#include "inktag_nrf_boot_integrity.h"\n' + text
        text = replace_once(text,
            "static bool app_is_valid(bool do_crc)\n{",
            """static bool app_is_valid(bool do_crc)
{
    if (!inktag_nrf_boot_integrity_valid(s_dfu_settings.bank_0.image_size, s_dfu_settings.bank_0.image_crc))
        return false;""")
        text = replace_once(text,
            '    else if (NRF_BL_APP_SIGNATURE_CHECK_REQUIRED &&\n        (s_dfu_settings.boot_validation_app.type != VALIDATE_ECDSA_P256_SHA256))\n    {\n        NRF_LOG_WARNING("Boot validation failed. The boot validation of the app must be a signature check.");\n        return false;\n    }\n', '')
        text = replace_once(text,
            '    NRF_LOG_INFO("Inactivity timeout.");\n    bootloader_reset(true);',
            '    NRF_LOG_INFO("Inactivity timeout.");\n'
            '    if (!inktag_nrf_dfu_resume_pending() &&\n'
            '        inktag_nrf_dfu_resume_allowed()) bootloader_reset(true);')
        text = replace_once(text,
            "        case NRF_DFU_EVT_DFU_COMPLETED:\n        case NRF_DFU_EVT_DFU_ABORTED:\n            bootloader_reset(true);\n            break;",
            """        case NRF_DFU_EVT_DFU_COMPLETED:
            bootloader_reset(true);
            break;
        case NRF_DFU_EVT_DFU_ABORTED:
            if (!inktag_nrf_dfu_resume_pending() &&
                inktag_nrf_dfu_resume_allowed()) bootloader_reset(true);
            break;""")
        text = replace_once(text,
            "static void dfu_enter_flags_clear(void)",
            """bool inktag_nrf_dfu_resume_boot_valid(void)
{
    /* Reuse the complete linked boot check, never the reset-reason CRC skip. */
    return app_is_valid(true);
}

static void dfu_enter_flags_clear(void)""")
    return ('/* Generated guarded-resume hooks from hash-locked SDK17.1. */\n'
            '#include "inktag_nrf_dfu_resume.h"\n' + text)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check-only", action="store_true")
    args = parser.parse_args()
    text = materialize(args.source)
    if not args.check_only:
        if args.output is None:
            parser.error("--output is required")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
