"""Prepare pinned, local-only Mac CoreADI emulation inputs; never install them.

Apple's binary remains outside Git and publication exports. Only four MIT
emulator-core files are downloaded; upstream auth/keychain scripts are excluded.
Archive/format reference: sacca97/os-findmy-e (MIT; third_party/os-findmy-e.LICENSE).
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import shutil
import struct
import subprocess
import urllib.request
from pathlib import Path

UPSTREAM = "2096d875dac6b5ed2347a9ae8c39c7251c0d6ed6"
ENGINE_HASHES = {
    "adi_run.py": "defd99b6baa271a14770698e36d5026f22f69ca226c5eae8f6c457e0f3cabafe",
    "harness.py": "2df372afa7f67a19135bf2c744a6ce9f3d5bb7a53a9a32d870bd8566c7c8e139",
    "macho.py": "6775e284e904debbae59234cb8d0732df5c199f7aaa9c285767d1f3bea09b6d4",
    "stubs.py": "be2ef85170b2dfa2fc23514d487dedada0d9d1f60aeefdebec8fe318a892f24c",
    "LICENSE": "d13de33ca644eb1350ca7ab815075d2499621f24dad01ee9be532360934483d6",
}
DMG_URL = "https://secure-appldnld.apple.com/itunes12/041-56365-20210413-79A5AC6D-6B0A-46EC-B399-5A381183E3BE/iTunes12.8.3.dmg"
DMG_SHA256 = "dd08b9967db3e5847955fa168ce880303abf5f36de12abb2061e7ef1a25be214"
PACKAGE_SHA256 = "b1b77e3a2f435f889496791aa103dbdb9aa280dc8a2581525f8e07f0ac42e3f4"
COREADI_SHA256 = "2eafc3c79c7f41a8afbfbeaaf23ee5dab5ad5bda55ec8e54dbc0ddf1d20ce630"
FRAMEWORK = "System/Library/PrivateFrameworks/CoreADI.framework/Versions/A/CoreADI"


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def download(url: str, path: Path, expected: str) -> None:
    if not path.exists():
        temporary = path.with_suffix(path.suffix + ".download")
        with urllib.request.urlopen(url, timeout=60) as response, temporary.open("wb") as output:
            shutil.copyfileobj(response, output)
        if digest(temporary) != expected:
            raise ValueError("Downloaded input hash mismatch; nothing was executed")
        temporary.replace(path)
    if digest(path) != expected:
        raise ValueError("Existing input hash mismatch; refusing to replace it")


def verify_engine(directory: Path) -> None:
    for name, expected in ENGINE_HASHES.items():
        if digest(directory / name) != expected:
            raise ValueError("Emulator source hash mismatch")


def cpio_framework(data: bytes) -> bytes:
    """Read one regular file; do not extract archive paths, links or devices."""
    offset = 0
    found = None
    while offset < len(data):
        magic = data[offset:offset+6]
        if magic == b"070707":
            widths, base, mode_at, name_at, size_at, padding = [6]*7 + [11, 6, 11], 8, 2, 8, 9, False
        elif magic in (b"070701", b"070702"):
            widths, base, mode_at, name_at, size_at, padding = [8]*13, 16, 1, 11, 6, True
        else:
            raise ValueError("Invalid cpio header")
        offset += 6
        values = []
        for width in widths:
            values.append(int(data[offset:offset+width], base))
            offset += width
        namesize, size = values[name_at], values[size_at]
        if not 1 <= namesize <= 4096 or size > len(data) or offset + namesize > len(data):
            raise ValueError("Invalid cpio bounds")
        name = data[offset:offset+namesize-1].decode("utf-8")
        start = offset + namesize
        if padding:
            start = (start + 3) & ~3
        if start + size > len(data):
            raise ValueError("Truncated cpio payload")
        if name == "TRAILER!!!":
            break
        if name.removeprefix("./") == FRAMEWORK:
            if values[mode_at] & 0o170000 != 0o100000 or found is not None:
                raise ValueError("Unexpected CoreADI archive entry")
            found = data[start:start+size]
        offset = start + size
        if padding:
            offset = (offset + 3) & ~3
    if found is None:
        raise ValueError("CoreADI regular file not present")
    return found


def x86_slice(data: bytes) -> bytes:
    if data[:4] != b"\xca\xfe\xba\xbe":
        raise ValueError("Expected pinned universal Mach-O")
    count = struct.unpack_from(">I", data, 4)[0]
    if not 1 <= count <= 8:
        raise ValueError("Invalid Mach-O slice count")
    for index in range(count):
        cpu, _sub, offset, size, _align = struct.unpack_from(">IIIII", data, 8 + index*20)
        if cpu == 0x01000007 and offset + size <= len(data):
            result = data[offset:offset+size]
            if result[:4] != b"\xcf\xfa\xed\xfe":
                raise ValueError("Invalid x86_64 slice")
            return result
    raise ValueError("No x86_64 slice")


def prepare(cache: Path, seven_zip: str) -> Path:
    cache.mkdir(parents=True, exist_ok=True)
    engine = cache / "upstream"
    engine.mkdir(exist_ok=True)
    for name, expected in ENGINE_HASHES.items():
        relative = name if name == "LICENSE" else "tools/machoadi/" + name
        download(f"https://raw.githubusercontent.com/sacca97/os-findmy-e/{UPSTREAM}/{relative}", engine / name, expected)
    apple = cache / "apple"
    apple.mkdir(exist_ok=True)
    dmg = apple / "iTunes12.8.3.dmg"
    download(DMG_URL, dmg, DMG_SHA256)
    package = apple / "Install iTunes.pkg"
    if not package.exists():
        with package.with_suffix(".tmp").open("wb") as output:
            subprocess.run([seven_zip, "x", "-so", str(dmg), "iTunes/Install iTunes.pkg"],
                           stdout=output, stderr=subprocess.PIPE, check=True, timeout=120)
        package.with_suffix(".tmp").replace(package)
    if digest(package) != PACKAGE_SHA256:
        raise ValueError("Derived package hash mismatch; refusing to extract")
    payload = subprocess.run([seven_zip, "x", "-so", str(package), "CoreADI.pkg/Payload"],
                             capture_output=True, check=True, timeout=60).stdout
    library = x86_slice(cpio_framework(gzip.decompress(payload)))
    if hashlib.sha256(library).hexdigest() != COREADI_SHA256:
        raise ValueError("Derived library hash mismatch; refusing to write")
    destination = apple / "CoreADI.x86_64"
    destination.write_bytes(library)
    return destination


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache-dir", required=True, type=Path)
    parser.add_argument("--seven-zip", required=True)
    args = parser.parse_args()
    output = prepare(args.cache_dir.resolve(), args.seven_zip)
    print("Pinned emulator prepared; no machine provisioning or account login performed.")
    print(f"CoreADI SHA256: {digest(output)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
