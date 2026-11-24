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
        case NRF_DFU_OP_PROTOCOL_VERSION:
            response.protocol.version = INKTAG_NRF_DFU_RESUME_PROTOCOL;
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
        # An explicit write rejection must reach the controller immediately,
        # including between PRNs or with PRNs disabled. Successful packets keep
        # the SDK's unchanged receipt cadence and payload encoding.
        text = replace_once(text,
            "if ((m_pkt_notif_target == 0) || (m_pkt_notif_target_cnt && m_pkt_notif_target > 0))",
            "if (p_res->result == NRF_DFU_RES_CODE_SUCCESS &&\n"
            "            ((m_pkt_notif_target == 0) || (m_pkt_notif_target_cnt && m_pkt_notif_target > 0)))")
        text = replace_once(text, "    addr.addr[0] += 1;", """    /* Keep the ordinary factory InkTag address; Find rotation is unrelated. */
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
    if ((p_ble_write_evt->data[0] == NRF_DFU_OP_PROTOCOL_VERSION ||
         p_ble_write_evt->data[0] == NRF_DFU_OP_ABORT) &&
        p_ble_write_evt->len != 1u)
    {
        /* These local replies consume only opcode and result. */
        nrf_dfu_response_t response;
        response.request = (nrf_dfu_op_t)p_ble_write_evt->data[0];
        response.result = NRF_DFU_RES_CODE_INVALID_PARAMETER;
        ble_dfu_req_handler_callback(&response, p_dfu);
        return NRF_SUCCESS;
    }
    //lint -save -e415 -e416 : Out-of-bounds access on p_ble_write_evt->data""")
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
    if path.name == "nrf_dfu_ble.c":
        # Standard FW_VERSION image zero is read-only. The SDK settings field
        # holds a pending App version here, so never use it as Recovery identity.
        text = replace_once(text, '#include "nrf_bootloader_info.h"',
            '#include "nrf_bootloader_info.h"\n#include "inktag_recovery_version.h"')
        text = replace_once(text,
            "        case NRF_DFU_OP_PROTOCOL_VERSION:\n            buffer[len++] = p_res->protocol.version;",
            """        case NRF_DFU_OP_FIRMWARE_VERSION:
        {
            /* Native little-endian response; the address remains a linker
             * relocation and length comes from this profile's existing limit. */
            static uint32_t const identity[] = {
                NRF_DFU_OP_RESPONSE | (NRF_DFU_OP_FIRMWARE_VERSION << 8) |
                    (NRF_DFU_RES_CODE_SUCCESS << 16) |
                    (NRF_DFU_FIRMWARE_TYPE_BOOTLOADER << 24),
                (INKTAG_RECOVERY_VERSION_MAJOR << 16) |
                    (INKTAG_RECOVERY_VERSION_MINOR << 8) | INKTAG_RECOVERY_VERSION_PATCH,
                BOOTLOADER_START_ADDR, INKTAG_NRF_RECOVERY_FLASH_SIZE,
            };
            memcpy(buffer, identity, sizeof identity);
            len = sizeof identity;
        } break;
        case NRF_DFU_OP_PROTOCOL_VERSION:
            buffer[len++] = p_res->protocol.version;""")
        text = replace_once(text,
            "    if ((p_ble_write_evt->data[0] == NRF_DFU_OP_PROTOCOL_VERSION ||",
            "    if (((p_ble_write_evt->data[0] == NRF_DFU_OP_PROTOCOL_VERSION ||")
        text = replace_once(text,
            "        p_ble_write_evt->len != 1u)",
            """        p_ble_write_evt->len != 1u) ||
        p_ble_write_evt->data[0] == NRF_DFU_OP_FIRMWARE_VERSION)""")
        text = replace_once(text,
            "        response.result = NRF_DFU_RES_CODE_INVALID_PARAMETER;",
            """        response.result =
            p_ble_write_evt->data[0] == NRF_DFU_OP_FIRMWARE_VERSION &&
            p_ble_write_evt->len == 2u && p_ble_write_evt->data[1] == 0u ?
            NRF_DFU_RES_CODE_SUCCESS : NRF_DFU_RES_CODE_INVALID_PARAMETER;""")
        # Every byte inside the transmitted length is filled by response_prepare
        # and the selected payload encoder. Do not clear the unused tail.
        text = replace_once(text, "uint8_t buffer[MAX_RESPONSE_LEN] = {0};",
                            "uint8_t buffer[MAX_RESPONSE_LEN];")
        # The two native characteristic builders differ only in these four
        # fields. Keep every permission/length/handle, share their initialization.
        import runpy
        replace_function = runpy.run_path(Path(__file__).with_name(
            "materialize-nordic-dfu-validation.py"))["replace_function"]
        text = replace_function(text, "static uint32_t dfu_pkt_char_add(", '''
static __attribute__((noinline, noclone)) uint32_t
dfu_characteristic_add(ble_dfu_t * const p_dfu, bool control)
{
    ble_gatts_char_md_t char_md = {
        .char_props.write = control,
        .char_props.notify = control,
        .char_props.write_wo_resp = !control,
    };
    ble_uuid_t char_uuid = {
        .type = p_dfu->uuid_type,
        .uuid = control ? BLE_DFU_CTRL_PT_UUID : BLE_DFU_PKT_CHAR_UUID,
    };
    ble_gatts_attr_md_t attr_md = {
        .vloc = BLE_GATTS_VLOC_STACK,
        .wr_auth = control,
        .vlen = 1,
        .write_perm = {
            .sm = 1,
#if NRF_DFU_BLE_REQUIRES_BONDS
            .lv = 2,
#else
            .lv = 1,
#endif
        },
    };
    ble_gatts_attr_t attr_char_value = {
        .p_uuid = &char_uuid,
        .p_attr_md = &attr_md,
        .max_len = control ? BLE_GATT_ATT_MTU_DEFAULT : MAX_DFU_PKT_LEN,
    };
    return sd_ble_gatts_characteristic_add(p_dfu->service_handle, &char_md,
        &attr_char_value, control ? &p_dfu->dfu_ctrl_pt_handles : &p_dfu->dfu_pkt_handles);
}
static uint32_t dfu_pkt_char_add(ble_dfu_t * const p_dfu)
{
    return dfu_characteristic_add(p_dfu, false);
}
''')
        text = replace_function(text, "static uint32_t dfu_ctrl_pt_add(", '''
static uint32_t dfu_ctrl_pt_add(ble_dfu_t * const p_dfu)
{
    return dfu_characteristic_add(p_dfu, true);
}
''')
        text = replace_once(text,
            """        uint32_t const sent = response_send(buffer, len);
        inktag_nrf_dfu_resume_response_sent(p_res->request, p_res->result, sent);
        return;""", "        goto send_response;")
        text = replace_once(text,
            "    uint32_t const sent = response_send(buffer, len);",
            "send_response:;\n    uint32_t const sent = response_send(buffer, len);")
    if path.name == "nrf_dfu_req_handler.c":
        text = text.replace('#include "nrf_crypto.h"', '')
        # Keep this platform's durable partial-object acknowledgement hook.
        # Both projections apply to the same hash-locked immutable SDK input.
        import runpy
        progress = runpy.run_path(Path(__file__).with_name("materialize-nordic-dfu-request.py"))
        text = progress["reject_failed_writes"](text)
        text = replace_once(text, progress["OLD"], progress["NEW"])
        text = replace_once(text, "#include <stdint.h>",
                            '#include "nrf_soc.h"\n#include <stdint.h>')
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
