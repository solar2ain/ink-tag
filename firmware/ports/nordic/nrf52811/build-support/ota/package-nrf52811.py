#!/usr/bin/env python3
"""Create an unsigned application-only Nordic OTA ZIP without signing keys."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
APP_START = 0x19000
APP_LIMIT = 69632


def varint(value):
    if not 0 <= value <= 0xffffffff:
        raise ValueError('Protobuf integer outside uint32')
    out = bytearray()
    while value >= 128:
        out.append((value & 127) | 128)
        value >>= 7
    out.append(value)
    return bytes(out)


def fields(values):
    out = bytearray()
    for tag, value in values:
        out.extend(varint((tag << 3) | (2 if isinstance(value, bytes) else 0)))
        if isinstance(value, bytes):
            out.extend(varint(len(value)))
            out.extend(value)
        else:
            out.extend(varint(value))
    return bytes(out)


def package(app, version):
    if not 8 <= len(app) <= APP_LIMIT or len(app) % 4:
        raise ValueError('App must be word aligned and 8..69632 bytes')
    sp, reset = struct.unpack_from('<II', app)
    if not 0x20000000 < sp <= 0x20006000 or sp % 8:
        raise ValueError('App vector has an invalid nRF52811 stack pointer')
    if not reset & 1 or not APP_START <= (reset & ~1) < APP_START + len(app):
        raise ValueError('App reset vector is outside the native App image')
    if type(version) is not int or not 0 <= version <= 0xffff:
        raise ValueError('otaVersion must fit the low 16 bits of epoch1')
    encoded_version = (1 << 16) | version
    init = fields([(1, encoded_version), (2, 52), (3, varint(0x0126)),
                   (4, 0), (7, len(app)),
                   (8, fields([(1, 3), (2, hashlib.sha256(app).digest()[::-1])])),
                   (10, fields([(1, 1), (2, b'')]))])
    # Packet.command -> Command(op_code=INIT, init=InitCommand).
    # No SignedCommand, signature or package-supplied trust key is present.
    dat = fields([(1, fields([(1, 1), (2, init)]))])
    manifest = {'manifest': {'application': {
        'bin_file': 'application.bin', 'dat_file': 'application.dat'}}}
    payloads = {'application.bin': app, 'application.dat': dat,
                'manifest.json': (json.dumps(manifest, separators=(',', ':')) + '\n').encode()}
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in payloads.items():
            entry = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data)
    return stream.getvalue(), {'signature': 'absent', 'publisherVerified': False,
        'compatibilityEpoch': 1, 'firmwareVersion': encoded_version,
        'hwVersion': 52, 'softdeviceRequired': 0x0126, 'appBytes': len(app),
        'appSha256': hashlib.sha256(app).hexdigest(), 'appCrc32': f'{zlib.crc32(app):08x}',
        'requiredRecoveryPolicy': 'unsigned-allowed (exact protocol 0xc1 or 0xe1)'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    version = json.loads((ROOT / 'version.json').read_text(encoding='utf-8'))
    data, report = package(args.app.read_bytes(), version['otaVersion'])
    # Never silently replace an existing package.
    with args.output.open('xb') as stream:
        stream.write(data)
    report.update(output=str(args.output), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
