#!/usr/bin/env python3
"""Bind the shipping Recovery build to its package inputs (host-only)."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

from nordic_recovery_identity import recovery_identity

SDK_PATH = ".sdk/nordic/nRF5_SDK_17.1.0_ddde560/"
INPUTS = (
    "firmware/common/ota/inktag_recovery_version.h",
    "firmware/common/ota/inktag_recovery_integrity_policy.h",
    "firmware/ports/nordic/common/src/inktag_nrf_integrity_hash.h",
    "firmware/ports/nordic/common/src/inktag_nrf_integrity_hash.inc",
    "firmware/ports/nordic/common/src/inktag_nrf_recovery_hash.c",
    "firmware/common/ble/inktag_ble_address.c",
    "firmware/common/ble/inktag_ble_address.h",
    "firmware/ports/nordic/common/src/inktag_nrf_ble_address.c",
    "firmware/ports/nordic/common/src/inktag_nrf_ble_address.h",
    "firmware/ports/nordic/common/bootloader/Makefile",
    "firmware/ports/nordic/common/bootloader/src/sdk_config.h",
    "firmware/ports/nordic/nrf52832/bootloader/Makefile",
    "firmware/ports/nordic/nrf52832/bootloader/bootloader.ld",
    "firmware/ports/nordic/nrf52832/bootloader/inktag_dfu_layout.h",
    "tools/sdk/materialize-nordic-dfu-resume.py",
    "tools/sdk/materialize-nordic-base-dfu-validation.py",
    "tools/sdk/materialize-nordic-dfu-settings.py",
    "tools/sdk/materialize-nordic-base-sdk.py",
    "tools/sdk/nordic_base_identity.py",
    "tools/sdk/goodix_code_elf.py",
    "firmware/common/security/inktag_code_guard.h",
    "firmware/common/security/inktag_code_guard.c",
    "firmware/ports/nordic/common/src/inktag_nrf_boot_integrity.h",
    "tools/sdk/nrf52832-recovery-binding.py",
    "tools/sdk/nordic_recovery_identity.py",
    "tools/sdk/inktag_source_profiles.py",
    "tools/sdk/materialize-nordic-source-profile.py",
    "firmware/source-profiles.json",
    "firmware/common/power/inktag_power.c",
    "firmware/common/power/inktag_power.h",
    "firmware/boards/nrf52832_priceetag.h",
    "firmware/ports/nordic/common/config/inktag_board.h",
    "firmware/ports/nordic/common/config/inktag_nrf_clock.h",
    ".sdk/nordic/nRF5_SDK_17.1.0_ddde560/components/libraries/bootloader/dfu/nrf_dfu_types.h",
    ".sdk/nordic/nRF5_SDK_17.1.0_ddde560/components/libraries/bootloader/nrf_bootloader_info.h",
    ".sdk/nordic/nRF5_SDK_17.1.0_ddde560/components/libraries/bootloader/nrf_bootloader_app_start_final.c",
    ".sdk/nordic/nRF5_SDK_17.1.0_ddde560/components/softdevice/s132/headers/nrf52/nrf_mbr.h",
)
GENERATED = ("nrf_common.ld", "nrf_bootloader_app_start.c", "nrf_dfu_ble.c", "nrf_dfu_req_handler.c", "nrf_bootloader.c",
             "nrf_dfu_validation.c", "nrf_dfu_settings.c", "crc32.c")
REQUIRED_SYMBOLS = ("inktag_nrf_dfu_resume_allowed", "resume_after_ack",
                    "inktag_nrf_boot_integrity_valid", "g_inktag_nrf_recovery_version", "g_inktag_nrf_recovery_protocol")


def fingerprint(path: Path) -> dict:
    payload = path.read_bytes()
    return {"bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}


def inputs(repo: Path, build: Path, source_profile='base', sdk_root: Path | None = None) -> dict:
    if source_profile != 'base':
        raise ValueError('Common Recovery requires the base source profile')
    paths = list(INPUTS)
    paths += [path.relative_to(repo).as_posix() for path in sorted(
        (repo / "firmware/ports/nordic/common/src").glob("inktag_nrf_dfu*"))]
    paths += [path.relative_to(repo).as_posix() for path in sorted(
        (repo / "firmware/ports/nordic/common/bootloader/src").glob("*.[ch]"))]
    paths += [(build / "generated" / name).relative_to(repo).as_posix()
              for name in GENERATED]
    paths.append((build / "nrf-recovery-mac.config").relative_to(repo).as_posix())
    return {
        path: fingerprint(sdk_root / path.removeprefix(SDK_PATH)
                          if sdk_root is not None and path.startswith(SDK_PATH) else repo / path)
        for path in sorted(set(paths))
    }


def record(repo: Path, build: Path, hex_path: Path, bin_path: Path,
           elf_path: Path, nm: str, same_mac: int, sdk_root: Path | None = None) -> dict:
    config = (repo / "firmware/ports/nordic/common/bootloader/src/sdk_config.h").read_text()
    if not re.search(r"^#define NRF_BL_DFU_INACTIVITY_TIMEOUT_MS 0$", config, re.M):
        raise ValueError("Shipping Recovery must not have an inactivity return deadline")
    symbols = subprocess.check_output([nm, str(elf_path)], text=True)
    found = [name for name in REQUIRED_SYMBOLS if any(
        line.split()[-1].startswith(name) for line in symbols.splitlines()
        if line.split())]
    if found != list(REQUIRED_SYMBOLS):
        raise ValueError("Recovery ELF lacks the guarded-resume implementation")
    # Verify the actual linked policy byte and its measured dependency closure.
    from goodix_code_elf import Elf
    from nordic_base_identity import compiled_profile
    source_profile = compiled_profile(Elf(elf_path.read_bytes()))
    if source_profile != 'base':
        raise ValueError('Common Recovery requires the base source profile')
    from nordic_base_identity import verify
    linked = Elf(elf_path.read_bytes())
    verify(linked.raw, 'recovery', bin_path.read_bytes(), source_profile=compiled_profile(linked))
    version,epoch,identity_offset=recovery_identity(bin_path.read_bytes(),repo)
    if (build / "nrf-recovery-mac.config").read_text().strip() != str(same_mac):
        raise ValueError("Recovery MAC build stamp does not match build arguments")
    return {"schemaVersion": 1, "board": "nrf52832_priceetag",
            "recoveryVersion":version,"compatibilityEpoch":epoch,"identityOffset":identity_offset,
            "sameMac": same_mac, "guardedResume": True, "idleTimeoutMs": 0, "appVerificationPolicy": "crc-only", "protocolVersion": 0xe1,
            "requiredSymbols": found, "sourceProfile": source_profile,
            "inputs": inputs(repo, build, source_profile, sdk_root),
            "artifacts": {"hex": fingerprint(hex_path), "bin": fingerprint(bin_path),
                          "elf": fingerprint(elf_path)}}


def verify(repo: Path, build: Path, binding: dict, hex_path: Path,
           bin_path: Path, elf_path: Path, sdk_root: Path | None = None) -> None:
    if (binding.get("schemaVersion") != 1 or
        binding.get("board") != "nrf52832_priceetag" or
        binding.get("sameMac") != 1 or binding.get("guardedResume") is not True or
        binding.get("idleTimeoutMs") != 0 or
        binding.get("appVerificationPolicy") != "crc-only" or binding.get("protocolVersion") != 0xe1 or
        binding.get("requiredSymbols") != list(REQUIRED_SYMBOLS)):
        raise ValueError("Recovery binding is missing the shipping guarded/same-MAC contract")
    if binding.get("inputs") != inputs(repo, build, binding.get("sourceProfile"), sdk_root):
        raise ValueError("Recovery sources/generated configuration changed; rebuild Recovery")
    actual = {"hex": fingerprint(hex_path), "bin": fingerprint(bin_path),
              "elf": fingerprint(elf_path)}
    if binding.get("artifacts") != actual:
        raise ValueError("Recovery artifacts do not match their qualified build binding")
    version,epoch,offset=recovery_identity(bin_path.read_bytes(),repo)
    if (binding.get('recoveryVersion'),binding.get('compatibilityEpoch'),binding.get('identityOffset')) != (version,epoch,offset):
        raise ValueError('Recovery release identity disagrees with its build binding')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("record", "verify"))
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--sdk-root", type=Path)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--hex", type=Path, required=True)
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--binding", type=Path, required=True)
    parser.add_argument("--nm")
    parser.add_argument("--same-mac", type=int, choices=(0, 1), default=1)
    args = parser.parse_args()
    repo, build = args.repo.resolve(), args.build.resolve()
    if args.action == "record":
        if not args.nm:
            parser.error("record requires --nm")
        result = record(repo, build, args.hex, args.bin, args.elf, args.nm, args.same_mac, args.sdk_root)
        args.binding.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    else:
        verify(repo, build, json.loads(args.binding.read_text(encoding="utf-8")),
               args.hex, args.bin, args.elf, args.sdk_root)
    print("Recovery package binding OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
