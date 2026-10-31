"""Offline full-flash package verification/configuration. No programmer or BLE."""
from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import stat
import sys
import tempfile
import zipfile

from tlsr_full_flash_config import ROOT, PCB_BACKENDS, read_board_config
from tlsr_factory_config import layout as factory_layout, pin_record, screen_record, aux_record, AUX_CAPABILITY, PIN_CONFIG_CAPABILITY
from tlsr_full_flash_address import typed_pair
import tlsr_full_flash_format as package

from inktag_programmer_formats.ota import inspect_package
from inktag_programmer_formats.tlsr_layout import TLSR_LAYOUT, tlsr_layout_profile, tlsr_package_profile



def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def verify_bundle(path: Path) -> tuple[dict, bytes]:
    """Validate a bounded Full ZIP without extracting archive-provided paths.

    Source/header hashes belong to build provenance, not device compatibility.
    The existing signed OTA inspector and layout policy remain authoritative.
    """
    if path.is_symlink() or path.stat().st_size > 2 * 1024 * 1024:
        raise ValueError('Full ZIP must be a bounded regular file')
    names = {'base.json', 'app-ota.zip', 'recovery.bin'}
    limits = {'base.json': 4096, 'app-ota.zip': 1024 * 1024,
              'recovery.bin': package.RECOVERY_SIZE, 'stage0.bin': 4096}
    try:
        with zipfile.ZipFile(path) as archive:
            entries = archive.infolist()
            actual = {entry.filename for entry in entries}
            if (actual not in (names, names | {'stage0.bin'}) or len(entries) != len(actual)):
                raise ValueError('Full ZIP requires base.json, app-ota.zip, recovery.bin and only the selected profile stage0')
            names = actual
            for entry in entries:
                mode = entry.external_attr >> 16
                if (entry.flag_bits & 1 or entry.is_dir() or stat.S_ISLNK(mode)
                        or entry.file_size > limits[entry.filename]):
                    raise ValueError('Invalid/encrypted/oversized Full ZIP member')
            data = {name: archive.read(name) for name in names}
    except (zipfile.BadZipFile, RuntimeError) as exc:
        raise ValueError('Invalid Full ZIP') from exc
    manifest = json.loads(data['base.json'].decode('utf-8'))
    policy = package.POLICY
    expected_fields = {'format_version', 'required_baseline', 'mcu', 'layout', 'compatibility_epoch',
                       'config_schema', 'backend_ids', 'images'}
    if (type(manifest) is not dict or set(manifest) not in (
            expected_fields, expected_fields | {'peripheral_backend_ids'})
            or type(manifest['format_version']) is not int or manifest['format_version'] != 1
            or manifest['required_baseline'] != 'inktag-release-v1'
            or manifest['mcu'] != 'tlsr8359' or manifest['layout'] != TLSR_LAYOUT
            or type(manifest['compatibility_epoch']) is not int
            or not 0<=manifest['compatibility_epoch']<=65535
            or type(manifest['config_schema']) is not int or manifest['config_schema'] != 1
            or type(manifest['backend_ids']) is not list
            or manifest['backend_ids'] != sorted({entry[4] for entry in PCB_BACKENDS.values()})
            or any(type(value) is not int for value in manifest['backend_ids'])):
        raise ValueError('Full ZIP MCU/layout/config/backend contract mismatch')
    profile = tlsr_layout_profile(manifest['layout'])
    images = manifest['images']
    if type(images) is not dict or set(images) != names - {'base.json'}:
        raise ValueError('Full ZIP requires exactly two bound firmware members')
    for name, item in images.items():
        if (type(item) is not dict or set(item) != {'bytes', 'sha256'}
                or type(item['bytes']) is not int or item['bytes'] != len(data[name])
                or item['sha256'] != sha(data[name])):
            raise ValueError('Full ZIP artifact hash/size mismatch: ' + name)
    # Only an internally chosen filename is written. ZIP names never reach an
    # extraction API; the temporary signed OTA is removed on every exit.
    with tempfile.TemporaryDirectory(prefix='inktag-full-verify-') as temporary:
        ota = Path(temporary) / 'app-ota.zip'
        ota.write_bytes(data['app-ota.zip'])
        attestation = inspect_package(ota)
    if (attestation.board != policy['board']
            or attestation.details['compatibilityEpoch'] != manifest['compatibility_epoch']
            or attestation.details['signedLayout'] != manifest['layout']):
        raise ValueError('Full ZIP signed App context mismatch')
    with zipfile.ZipFile(io.BytesIO(data['app-ota.zip'])) as ota:
        transfer = ota.read('application.bin')
        ota_manifest = json.loads(ota.read('manifest.json'))
    profile = tlsr_package_profile(manifest['layout'], transfer)
    if ('stage0.bin' in data) != (profile['start'] != 0):
        raise ValueError('Full ZIP signed entry context/stage0 mismatch; no layout reinterpretation')
    if PIN_CONFIG_CAPABILITY not in transfer[:-96]:
        raise ValueError('Config schema1 requires the signed current recipe-derived GPIO App')
    if ota_manifest.get('requiredBaseline') != 'inktag-release-v1':
        raise ValueError('Signed OTA producer must declare the fixed v1 baseline')
    peripherals = manifest.get('peripheral_backend_ids', [])
    if (type(peripherals) is not list or any(type(value) is not int for value in peripherals)
            or peripherals != sorted(set(peripherals))
            or not set(peripherals) <= set(manifest['backend_ids'])
            or peripherals != ota_manifest.get('peripheral_backend_ids', [])):
        raise ValueError('Full ZIP peripheral backend declaration/OTA binding mismatch')
    package.validate_telink_image(data['recovery.bin'], package.RECOVERY_SIZE, 'Recovery')
    from tlsr_trusted_entry_profile import check_pair
    check_pair(transfer[:-96], data['recovery.bin'], manifest['layout'], data.get('stage0.bin'))
    manifest['typed_addresses']=typed_pair(transfer,data['recovery.bin'])
    if len(transfer) > package.APPLICATION_SIZE:
        raise ValueError('Full ZIP signed App exceeds safe payload limit')
    neutral = bytearray(b'\xff' * 524288)
    if 'stage0.bin' in data:
        neutral[:len(data['stage0.bin'])] = data['stage0.bin']
    at = profile['start']
    neutral[at:at+len(transfer)] = transfer
    neutral[package.RECOVERY_ADDRESS:package.RECOVERY_ADDRESS + len(data['recovery.bin'])] = data['recovery.bin']
    manifest.update(verified_manifest_sha256=sha(data['base.json']),
                    verified_package_sha256=sha(path.read_bytes()), package_verified=True,
                    font_protocol_status=package.FONT_PROTOCOL_STATUS,
                    artifact_integrity_verified=True, application_start=profile['start'])
    return manifest, bytes(neutral)


def verify_base(directory: Path) -> tuple[dict, bytes]:
    if directory.is_symlink():
        raise ValueError('Full base must not be a symlink')
    directory = directory.resolve(strict=True)
    if not directory.is_file() or directory.suffix.lower() != '.zip':
        raise ValueError('Full requires one current app-full.zip; loose historical base directories are retired')
    return verify_bundle(directory)


def configure(base_directory: Path, config_path: Path) -> tuple[bytes, dict]:
    config = read_board_config(config_path)
    manifest, neutral = verify_base(base_directory)
    if (manifest.get('package_verified') and config['board_id'] not in manifest['backend_ids']):
        raise ValueError('backend ID not included in the Full ZIP')
    if config['board_id'] in (5,8) and config['board_id'] not in manifest.get('peripheral_backend_ids', []):
        raise ValueError(f"Board requires an updated Full package with peripheral backend {config['board_id']}; old screen-only App refused")
    image = bytearray(neutral)
    if AUX_CAPABILITY not in neutral[:package.APPLICATION_SIZE]:
        raise ValueError('Configured Full requires the current complete AUX consumer')
    body = struct.pack("<4sBBH", b"ITB1", 1, config["board_id"], 2 |
                       (4 if manifest.get('typed_addresses') else 0) |
                       (1 if config["base_image_store_only"] else 0))
    checksum = 2166136261
    for byte in body:
        checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
    image[0x7F100:0x7F10C] = body + struct.pack("<I", checksum)
    # The maintained Full writer holds the CPU through same-session readback
    # and only starts App after success (or an explicit unverified write).
    # Request the common one-shot device-information page after startup LEDs. App consumes
    # this byte only after the comparison; later boots follow saved policy.
    image[0x7F10C] = 0xA5
    factory=factory_layout()
    at=factory['PIN_FACTORY_ADDRESS']
    image[at:at+32]=pin_record(config['board_id'],bytes.fromhex(config['pin_wire_hex']))
    at=factory['SCREEN_FACTORY_ADDRESS']
    image[at:at+32]=screen_record(config['board_id'],bytes.fromhex(config['screen_wire_hex']))
    at=factory['AUX_FACTORY_ADDRESS']
    image[at:at+16]=aux_record(bytes.fromhex(config['aux_wire_hex']),
        bytes.fromhex(config['pin_wire_hex']),config['board_id'])
    # Settings and identity are NEVER supplied by this template. The writer
    # must capture this unit and use the maintained preservation/default codec.
    assert image[:0x7F100] == neutral[:0x7F100] and image[0x7F160:] == neutral[0x7F160:]
    public_config = {key: value for key, value in config.items() if key != 'hardware'} if manifest.get('package_verified') else config
    return bytes(image), {"schema": "inktag-tlsr-full-configured-template-v1", **public_config,
        "package_verified": manifest.get('package_verified', False),
        "required_baseline": manifest['required_baseline'],
        "compatibility_epoch": manifest['compatibility_epoch'],
        "peripheral_backend_ids": manifest.get('peripheral_backend_ids', []),
        "typed_addresses": manifest.get('typed_addresses', False),
        "layout": manifest['layout'],
        "application_start": manifest['application_start'],
        "base_package_sha256": manifest.get('verified_package_sha256'),
        "base_directory": str(base_directory.resolve()), "base_manifest_sha256": manifest["verified_manifest_sha256"],
        "base_sha256": sha(neutral), "configured_template_sha256": sha(image),
        "application_bytes_unchanged": True, "recovery_bytes_unchanged": True,
        "first_boot_display": "one-shot-device-info-after-startup",
        "font_protocol_status": manifest['font_protocol_status'],
        "per_unit_preservation_required": True, "hardware_started": False}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    image, review = configure(args.base, args.config)
    if args.output:
        if args.output.exists() or args.output.with_suffix(".json").exists():
            raise ValueError("configured template output already exists; no overwrite")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(image)
        args.output.with_suffix(".json").write_text(json.dumps(review, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(review, indent=2))


if __name__ == "__main__":
    main()
