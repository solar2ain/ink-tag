#!/usr/bin/env python3
"""Hash-locked SDK Settings selection: retain highest valid native epoch floor."""
import argparse
import hashlib
from pathlib import Path

EXPECTED_SHA256 = 'd3605f167072a19f2e98ac900e54cb7f703cc2f792eb2e2ad919f984d84b0486'


def materialize(path):
    data = Path(path).read_bytes()
    if hashlib.sha256(data).hexdigest() != EXPECTED_SHA256:
        raise ValueError('Nordic Settings source hash mismatch')
    text = data.decode().replace('\r\n', '\n')
    old = '''    bool settings_valid        = settings_crc_ok();
    bool settings_backup_valid = settings_backup_crc_ok();'''
    new = '''    nrf_dfu_settings_t const * primary =
        (nrf_dfu_settings_t const *)m_dfu_settings_buffer;
    nrf_dfu_settings_t const * backup =
        (nrf_dfu_settings_t const *)mp_dfu_settings_backup_buffer;
    bool settings_valid = settings_crc_ok() &&
        primary->settings_version == NRF_DFU_SETTINGS_VERSION &&
        boot_validation_crc_ok(primary);
    bool settings_backup_valid = settings_backup_crc_ok() &&
        backup->settings_version == NRF_DFU_SETTINGS_VERSION;'''
    assert text.count(old) == 1
    text = text.replace(old, new)
    old = '    if (NRF_DFU_SETTINGS_COMPATIBILITY_MODE && !NRF_DFU_IN_APP && (s_dfu_settings.settings_version == 1))'
    new = '''    /* The SDK's backup-owned bank/proof selection is retained. Before its
     * init rewrites either page, preserve the highest CRC-valid epoch from
     * BOTH pages. A torn confirmation must never resurrect a lower floor.
     * Same-epoch release ordering remains the native policy, not rollback. */
    if (settings_valid && (primary->app_version >> 16) >
                          (s_dfu_settings.app_version >> 16))
        s_dfu_settings.app_version = primary->app_version;
    if (settings_backup_valid && (backup->app_version >> 16) >
                                 (s_dfu_settings.app_version >> 16))
        s_dfu_settings.app_version = backup->app_version;

''' + old
    assert text.count(old) == 1
    return '/* Generated Settings policy from hash-locked SDK17.1. */\n' + text.replace(old, new)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--check-only', action='store_true')
    args = parser.parse_args()
    text = materialize(args.source)
    if not args.check_only:
        if args.output is None:
            parser.error('--output is required')
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding='utf8', newline='\n')
