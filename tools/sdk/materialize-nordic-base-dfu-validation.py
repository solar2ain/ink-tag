#!/usr/bin/env python3
"""Materialize InkTag's application-only Nordic DFU validation source.

The input must be the byte-exact Nordic nRF5 SDK 17.1.0 source.  Keeping the
vendor file immutable makes unreviewed local SDK edits a hard build failure, while the generated source records the small, reviewable
application-only policy needed by InkTag Recovery.
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
        'static nrf_dfu_result_t nrf_dfu_validation_signature_check(': "",
        'static bool signature_required(': "",
        'static void crypto_init(': "",
        'static nrf_dfu_result_t update_data_size_get(': r'''
static nrf_dfu_result_t update_data_size_get(dfu_init_command_t const * p_init,
                                             uint32_t * p_size)
{
    if ((p_init->type != DFU_FW_TYPE_APPLICATION) ||
        !p_init->has_app_size || (p_init->app_size < 8U) || (p_init->app_size > 163840U))
    {
        return EXT_ERR(NRF_DFU_EXT_ERROR_INIT_COMMAND_INVALID);
    }
    *p_size = p_init->app_size;
    return NRF_DFU_RES_CODE_SUCCESS;
}
''',
        'static bool use_single_bank(': r'''
static bool use_single_bank(dfu_fw_type_t new_fw_type)
{
    UNUSED_PARAMETER(new_fw_type);
    return true;
}
''',
        'static bool update_requires_softdevice(': r'''
static bool update_requires_softdevice(dfu_init_command_t const * p_init)
{
    UNUSED_PARAMETER(p_init);
    return true;
}
''',
        'static bool keep_softdevice(': r'''
static bool keep_softdevice(dfu_init_command_t const * p_init)
{
    UNUSED_PARAMETER(p_init);
    return true;
}
''',
        'static nrf_dfu_result_t update_data_addr_get(': r'''
static nrf_dfu_result_t update_data_addr_get(dfu_init_command_t const * p_init,
                                             uint32_t fw_size,
                                             uint32_t * p_addr)
{
    UNUSED_PARAMETER(p_init);
    /* This is the first point reached only after the complete native init
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
''',
        'static bool nrf_dfu_validation_hash_ok(': r'''
static bool nrf_dfu_validation_hash_ok(uint8_t const * p_hash,
                                       uint32_t src_addr,
                                       uint32_t data_len,
                                       bool little_endian)
{
    return little_endian &&
           inktag_nrf_dfu_sha256_matches_le(
               p_hash, (uint8_t const *)src_addr, data_len);
}
''',
        'static bool postvalidate_app(': r'''
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
    uint32_t const expected_crc =
        crc32_compute((uint8_t *)src_addr, data_len, NULL);
    /* SDK -fshort-enums places bytes at offset one, not word alignment. */
    memcpy(boot_validation.bytes, &expected_crc, sizeof(expected_crc));
    memcpy(&s_dfu_settings.boot_validation_app,
           &boot_validation,
           sizeof(boot_validation));
    s_dfu_settings.bank_1.bank_code = NRF_DFU_BANK_VALID_APP;
    s_dfu_settings.bank_0.bank_code = NRF_DFU_BANK_INVALID;
    /* The application confirms this native candidate after service and
     * advertising readiness. Until then app_version remains the durable
     * confirmed compatibility floor used by prevalidation. */
    s_dfu_settings.bootloader_version = p_init->fw_version;
    return true;
}
''',
        'bool nrf_dfu_validation_boot_validate(': r'''
bool nrf_dfu_validation_boot_validate(boot_validation_t const * p_validation,
                                      uint32_t data_addr,
                                      uint32_t data_len)
{
    uint32_t expected_crc;
    memcpy(&expected_crc, p_validation->bytes, sizeof(expected_crc));

    return (p_validation->type == VALIDATE_CRC) &&
           (crc32_compute((uint8_t const *)data_addr, data_len, NULL) ==
            expected_crc);
}
''',
        'nrf_dfu_result_t postvalidate(': r'''
nrf_dfu_result_t postvalidate(uint32_t data_addr, uint32_t data_len, bool is_trusted)
{
    nrf_dfu_result_t ret_val = NRF_DFU_RES_CODE_SUCCESS;

    if ((mp_init == NULL) || (mp_init->type != DFU_FW_TYPE_APPLICATION) ||
        data_len != mp_init->app_size || data_len < 8u || data_len > 163840u)
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
        if (ret_val == NRF_DFU_RES_CODE_SUCCESS)
        {
            /* Native CRC/length/boot-validation records are complete.
             * Clear transfer progress after publication; retain no signature
             * proof marker and do not depend on the old init packet at boot. */
            memset(&s_dfu_settings.progress, 0, sizeof(s_dfu_settings.progress));
            s_dfu_settings.write_offset = 0;
        }
        else
        {
            nrf_dfu_settings_progress_reset();
        }
        s_dfu_settings.progress.update_start_address = data_addr;
    }
    return ret_val;
}
''',
        'nrf_dfu_result_t nrf_dfu_validation_prevalidate(': r'''
nrf_dfu_result_t nrf_dfu_validation_prevalidate(void)
{
    dfu_command_t const * command = m_packet.has_signed_command ?
        &m_packet.signed_command.command : &m_packet.command;
    /* Signed and unsigned native containers share the same integrity path.
     * A signature field is opaque compatibility data, never an authority. */
    if (m_packet.has_signed_command == m_packet.has_command ||
        !command->has_op_code || command->op_code != DFU_OP_CODE_INIT ||
        !command->has_init || mp_init != &command->init ||
        mp_init->type != DFU_FW_TYPE_APPLICATION ||
        !mp_init->has_app_size || mp_init->app_size < 8u ||
        mp_init->app_size > 163840u || mp_init->sd_size != 0u ||
        mp_init->bl_size != 0u || !mp_init->has_hash ||
        mp_init->hash.hash_type != DFU_HASH_TYPE_SHA256 ||
        mp_init->hash.hash.size != 32u ||
        mp_init->boot_validation_count != 1u ||
        mp_init->boot_validation[0].type != (dfu_validation_type_t)VALIDATE_CRC ||
        mp_init->boot_validation[0].bytes.size != 0u)
        return EXT_ERR(NRF_DFU_EXT_ERROR_INIT_COMMAND_INVALID);
    return nrf_dfu_ver_validation_check(mp_init);
}
''',
    }
    for unsupported in ('static bool is_major_softdevice_update(',
                        'static bool softdevice_info_ok(',
                        'static bool boot_validation_extract(',
                        'static bool postvalidate_sd_bl('):
        replacements[unsupported] = ''
    for signature, replacement in replacements.items():
        source = replace_function(source, signature, replacement)

    first = source.index('/** @brief Value length structure holding the public key.')
    last = source.index('/** @brief Flag used by parser code', first)
    source = source[:first] + source[last:]
    unused_crypto_state = (
        "m_public_key",
        "m_signature",
        "m_sig_hash",
        "m_fw_hash",
        "m_crypto_initialized",
        'extern const uint8_t pk[64]',
        '#include "nrf_crypto.h"',
        '#include "nrf_crypto_shared.h"',
    )
    source = "\n".join(
        line for line in source.splitlines()
        if not any(name in line for name in unused_crypto_state)
    ) + "\n"

    for obsolete in (
        '// Function determines if init command signature is obligatory.',
        '// Function to perform signature check if required.',
    ):
        source = source.replace(obsolete, '')
    marker = "#include <stdbool.h>"
    banner = (
        "/* Generated from hash-locked Nordic SDK source by "
        "tools/sdk/materialize-nordic-base-dfu-validation.py. */\n"
        "#define INKTAG_NRF_DFU_APPLICATION_ONLY 1\n"
        "#include \"inktag_nrf_integrity_hash.h\"\n"
        "#include \"inktag_nrf_dfu_power.h\"\n"
        "#include \"inktag_nrf_boot_integrity.h\"\n"
        "#include \"inktag_nrf_dfu_epoch.h\"\n"
    )
    source = source.replace(marker, banner + marker, 1)
    return source + BOOT_INTEGRITY


BOOT_INTEGRITY = r'''
/* Unconditional full-App CRC/context gate on every cold/Recovery return.
 * Native Settings provide the stored expected length/CRC; this is not a
 * self-comparison or an App-supplied cookie. */
bool inktag_nrf_boot_integrity_valid(uint32_t image_size,
                                                  uint32_t expected_crc)
{
    uint32_t stored_crc;
    uint32_t const start = nrf_dfu_bank0_start_addr();
    memcpy(&stored_crc, s_dfu_settings.boot_validation_app.bytes, 4u);
    if (start != 0x26000u || image_size < 8u || image_size > 163840u ||
        s_dfu_settings.boot_validation_app.type != VALIDATE_CRC ||
        stored_crc != expected_crc || !inktag_nrf_dfu_settings_pair_matches())
        return false;
    uint32_t const * vectors = (uint32_t const *)(uintptr_t)start;
    uint32_t const stack = vectors[0], reset = vectors[1];
    if ((stack & 7u) != 0u || stack <= 0x20004000u || stack > 0x20010000u ||
        (reset & 1u) == 0u || (reset & ~1u) < start ||
        (reset & ~1u) - start >= image_size ||
        crc32_compute((uint8_t const *)(uintptr_t)start, image_size, NULL) != expected_crc)
        return false;
    return true;
}
'''


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
