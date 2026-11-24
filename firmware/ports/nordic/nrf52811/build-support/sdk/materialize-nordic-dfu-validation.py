#!/usr/bin/env python3
"""Materialize InkTag's application-only Nordic DFU validation source.

The input must be the byte-exact Nordic nRF5 SDK 17.1.0 source.  Keeping the
vendor file immutable rejects unreviewed SDK edits. The generated application-
only policy removes publisher verification while retaining image integrity,
target/version admission and the existing update transaction.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


EXPECTED_SHA256 = "50d03bf26e9778ca0031ffd34c354bb37be48bd890aefaf962db5c1de56d5f2f"


def replace_function(source: str, signature: str, replacement: str) -> str:
    start = source.index(signature)
    body_start = source.index("{", start)
    depth = 0
    body_end = -1
    for index in range(body_start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                body_end = index + 1
                break
    if body_end < 0:
        raise ValueError(f"unterminated function: {signature}")
    return source[:start] + replacement.rstrip() + source[body_end:]


def materialize(source_path: Path) -> str:
    payload = source_path.read_bytes()
    digest = hashlib.sha256(payload).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError(
            f"Nordic DFU validation source hash is {digest}; expected {EXPECTED_SHA256}. "
            "Rematerialize the exact nRF5 SDK 17.1.0_ddde560 archive."
        )

    source = payload.decode("utf-8").replace("\r\n", "\n")
    replacements = {
        "static nrf_dfu_result_t nrf_dfu_validation_signature_check(": "",
        "static bool signature_required(": "",
        "static void crypto_init(": "",
        "static nrf_dfu_result_t update_data_size_get(": """
static nrf_dfu_result_t update_data_size_get(dfu_init_command_t const * p_init,
                                             uint32_t * p_size)
{
    if ((p_init->type != DFU_FW_TYPE_APPLICATION) ||
        !p_init->has_app_size || (p_init->app_size == 0U))
    {
        return EXT_ERR(NRF_DFU_EXT_ERROR_INIT_COMMAND_INVALID);
    }
    *p_size = p_init->app_size;
    return NRF_DFU_RES_CODE_SUCCESS;
}
""",
        "static bool use_single_bank(": """
static bool use_single_bank(dfu_fw_type_t new_fw_type)
{
    UNUSED_PARAMETER(new_fw_type);
    return true;
}
""",
        "static bool update_requires_softdevice(": """
static bool update_requires_softdevice(dfu_init_command_t const * p_init)
{
    UNUSED_PARAMETER(p_init);
    return true;
}
""",
        "static bool keep_softdevice(": """
static bool keep_softdevice(dfu_init_command_t const * p_init)
{
    UNUSED_PARAMETER(p_init);
    return true;
}
""",
        "static nrf_dfu_result_t update_data_addr_get(": """
static nrf_dfu_result_t update_data_addr_get(dfu_init_command_t const * p_init,
                                             uint32_t fw_size,
                                             uint32_t * p_addr)
{
    UNUSED_PARAMETER(p_init);
    /* This is the first point reached only after the complete init
     * command and target/version policy have passed. Reject a new OTA before
     * the SDK accepts or persists its destination when VDD is below 2.60 V. */
    if (!inktag_nrf_dfu_ota_admission_allowed())
    {
        return NRF_DFU_RES_CODE_OPERATION_NOT_PERMITTED;
    }
    if (nrf_dfu_cache_prepare(fw_size, true, false, true) != NRF_SUCCESS)
    {
        return NRF_DFU_RES_CODE_INSUFFICIENT_RESOURCES;
    }
    *p_addr = nrf_dfu_bank1_start_addr();
    return NRF_DFU_RES_CODE_SUCCESS;
}
""",
        "static bool nrf_dfu_validation_hash_ok(": """
static bool nrf_dfu_validation_hash_ok(uint8_t const * p_hash,
                                       uint32_t src_addr,
                                       uint32_t data_len,
                                       bool little_endian)
{
    return little_endian &&
           inktag_nrf_dfu_sha256_matches_le(
               p_hash, (uint8_t const *)src_addr, data_len);
}
""",
        "static bool postvalidate_app(": """
static bool postvalidate_app(dfu_init_command_t const * p_init,
                             uint32_t src_addr,
                             uint32_t data_len,
                             bool is_trusted)
{
    boot_validation_t boot_validation;

    ASSERT(p_init->type == DFU_FW_TYPE_APPLICATION);
    if ((p_init->boot_validation_count != 1U) ||
        (p_init->boot_validation[0].type !=
         (dfu_validation_type_t)VALIDATE_CRC))
    {
        return false;
    }
    if (!is_trusted)
    {
        return true;
    }

    memset(&boot_validation, 0, sizeof(boot_validation));
    boot_validation.type = VALIDATE_CRC;
    uint32_t const app_crc =
        crc32_compute((uint8_t *)src_addr, data_len, NULL);
    memcpy(boot_validation.bytes, &app_crc, sizeof(app_crc));
    memcpy(&s_dfu_settings.boot_validation_app,
           &boot_validation,
           sizeof(boot_validation));
    s_dfu_settings.bank_1.bank_code = NRF_DFU_BANK_VALID_APP;
    s_dfu_settings.bank_0.bank_code = NRF_DFU_BANK_INVALID;
    /* The application confirms this candidate after service and
     * advertising readiness. Until then app_version remains the durable
     * confirmed compatibility floor used by prevalidation. */
    s_dfu_settings.bootloader_version = p_init->fw_version;
    return true;
}
""",
        "bool nrf_dfu_validation_boot_validate(": """
bool nrf_dfu_validation_boot_validate(boot_validation_t const * p_validation,
                                      uint32_t data_addr,
                                      uint32_t data_len)
{
    uint32_t expected_crc;

    /* With -fshort-enums the SDK byte array follows a one-byte enum. Copy
     * rather than dereferencing an unaligned uint32_t pointer. */
    memcpy(&expected_crc, p_validation->bytes, sizeof(expected_crc));

    return (p_validation->type == VALIDATE_CRC) &&
           (crc32_compute((uint8_t const *)data_addr, data_len, NULL) ==
            expected_crc);
}
""",
        "nrf_dfu_result_t postvalidate(": """
nrf_dfu_result_t postvalidate(uint32_t data_addr, uint32_t data_len, bool is_trusted)
{
    nrf_dfu_result_t ret_val = NRF_DFU_RES_CODE_SUCCESS;

    if ((mp_init == NULL) || (mp_init->type != DFU_FW_TYPE_APPLICATION))
    {
        ret_val = EXT_ERR(NRF_DFU_EXT_ERROR_INIT_COMMAND_INVALID);
    }
    else if (!fw_hash_ok(mp_init, data_addr, data_len))
    {
        ret_val = EXT_ERR(NRF_DFU_EXT_ERROR_VERIFICATION_FAILED);
    }
    else if (!postvalidate_app(mp_init, data_addr, data_len, is_trusted))
    {
        ret_val = NRF_DFU_RES_CODE_INVALID_OBJECT;
    }

    if (!is_trusted)
    {
        if (ret_val == NRF_DFU_RES_CODE_SUCCESS)
        {
            s_dfu_settings.bank_current = NRF_DFU_CURRENT_BANK_1;
        }
        else
        {
            nrf_dfu_settings_progress_reset();
        }
    }
    else
    {
        if (ret_val == NRF_DFU_RES_CODE_SUCCESS)
        {
            s_dfu_settings.bank_1.image_crc =
                crc32_compute((uint8_t *)data_addr, data_len, NULL);
            s_dfu_settings.bank_1.image_size = data_len;
        }
        else
        {
            nrf_dfu_bank_invalidate(&s_dfu_settings.bank_1);
        }
        nrf_dfu_settings_progress_reset();
        s_dfu_settings.progress.update_start_address = data_addr;
    }
    return ret_val;
}
""",
    }
    for signature, replacement in replacements.items():
        source = replace_function(source, signature, replacement)

    # App publisher signature verification is deliberately absent.
    # Keep the SDK's bounded protobuf decoder and every target/version,
    # image hash, capacity, CRC and transactional admission check. A signed
    # envelope is accepted as a transport format, never authenticated.
    for signature in ("static bool boot_validation_extract(",
                      "static bool postvalidate_sd_bl("):
        source = replace_function(source, signature, "")
    source = replace_function(source, "nrf_dfu_result_t nrf_dfu_validation_prevalidate(", """
nrf_dfu_result_t nrf_dfu_validation_prevalidate(void)
{
    dfu_command_t const * p_command = m_packet.has_signed_command
    ? &m_packet.signed_command.command : &m_packet.command;
    return nrf_dfu_ver_validation_check(&p_command->init);
}
""")
    for line in ('#include "nrf_crypto.h"', '#include "nrf_crypto_shared.h"',
                 '__ALIGN(4) extern const uint8_t pk[64];'):
        source = source.replace(line, "")


    unused_crypto_state = (
        "m_public_key",
        "m_signature",
        "m_sig_hash",
        "m_fw_hash",
        "m_crypto_initialized",
    )
    source = "\n".join(
        line for line in source.splitlines()
        if not any(name in line for name in unused_crypto_state)
    ) + "\n"

    marker = "#include <stdbool.h>"
    banner = (
        "/* Generated from hash-locked Nordic SDK source by "
        "build-support/sdk/materialize-nordic-dfu-validation.py. */\n"
        "#define INKTAG_NRF_DFU_APPLICATION_ONLY 1\n"
        '#include "inktag_nrf_dfu_hash.h"\n'
        '#include "inktag_nrf_dfu_power.h"\n'
    )
    return source.replace(marker, banner + marker, 1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check-only", action="store_true")
    args = parser.parse_args()

    rendered = materialize(args.source)
    if args.check_only:
        return 0
    if args.output is None:
        parser.error("--output is required unless --check-only is used")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
