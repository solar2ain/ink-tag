"""Bounded offline nRF52832 HW56 Full bundle. No transport or device access.

The signed init ABI is SDK17 dfu-cc.proto; verify the exact InitCommand bytes,
native little-endian P256 key/signature and reversed SHA256 just as Recovery.
This is a host container, not a new OTA format or device qualification.
"""
from __future__ import annotations
import hashlib
import io
import json
from pathlib import Path
import runpy
import stat
import struct
import tempfile
import zipfile
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils
from nordic_board_family import FIRMWARE_FAMILY, BOARD_ID

ROOT = Path(__file__).resolve().parents[2]
PUBLIC_KEY_LE = bytes.fromhex(
    '0edf3d346f2be4a0a641930c3bba0f21fff2bf1913ed68d1a81b47a87e0bf9ae'
    'c2a2697fc77f10daba686704f3323fed849591ec9539b288ae5be98f221fe7d1')
MEMBERS = {'base.json': 4096, 'full.hex': 1500000, 'app-ota.zip': 1048576}


def require_release_binding(app: bytes, recovery: bytes, *, development_validation=False):
    """Source-owned acceptance only; no user/package/path override exists.

    Root adds exact tuples after reviewed combined source/compiler closure.
    A signed HW56 DAT alone cannot establish this new InkTag format baseline.
    """
    if type(development_validation) is not bool:
        raise ValueError('Development-validation selector must be explicit boolean')
    path = Path(__file__).with_name('nordic_development_artifacts.json' if
        development_validation else 'nordic_release_artifacts.json')
    try:
        accepted = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError) as error:
        raise ValueError('Source-owned release-v1 artifact acceptance is unavailable') from error
    expected = dict(schemaVersion=1, baseline='inktag-release-v1',
                    target=FIRMWARE_FAMILY, layoutMarker='N3V1')
    if development_validation:
        expected['purpose'] = 'development-validation-only'
    if (type(accepted) is not dict or set(accepted) != set(expected) | {'acceptedArtifacts'} or
        any(type(accepted[k]) is not type(v) or accepted[k] != v for k,v in expected.items()) or
        type(accepted['acceptedArtifacts']) is not list):
        raise ValueError('Invalid source-owned release-v1 acceptance authority')
    fields = {'appSha256', 'recoverySha256', 'sourceCommit', 'compilerClosureSha256', 'reviewCommit'}
    for entry in accepted['acceptedArtifacts']:
        if (type(entry) is not dict or set(entry) != fields or
            any(type(entry[k]) is not str or len(entry[k]) != (40 if k.endswith('Commit') else 64) or
                any(c not in '0123456789abcdef' for c in entry[k]) for k in fields)):
            raise ValueError('Malformed accepted-artifact entry')
        if entry['appSha256'] == sha(app) and entry['recoverySha256'] == sha(recovery):
            return dict(entry)
    raise ValueError('App/Recovery pair has no reviewed release-v1 source/compiler binding; packaging refused')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def verify_input_pair(application_hex, recovery_bin, *, development_validation=False, normal_build_inputs=None):
    """Check capacity and exact source/compiler admission before key use."""
    app_path, rec_path = Path(application_hex), Path(recovery_bin)
    for path, limit in ((app_path, 1500000), (rec_path, 24576)):
        if path.is_symlink() or not path.is_file() or not 0 < path.stat().st_size <= limit:
            raise ValueError('Package input must be a bounded regular file')
    memory = runpy.run_path(str(ROOT/'tools/sdk/merge-ihex.py'))['read_hex'](app_path)
    if not memory or min(memory) != 0x26000 or max(memory) >= 0x4e000:
        raise ValueError('Application HEX exceeds the exact HW56 execution region')
    app = bytes(memory.get(a, 255) for a in range(0x26000, max(memory)+1))
    recovery = rec_path.read_bytes()
    if normal_build_inputs is not None:
        if development_validation: raise ValueError('Normal build and historical development admission are distinct')
        from nordic_build_candidate import validate
        accepted = validate(normal_build_inputs, app, recovery, ROOT)
    else:
        accepted = require_release_binding(app, recovery,
            **({'development_validation': True} if development_validation else {}))
    return dict(app_sha256=sha(app), recovery_sha256=sha(recovery),
        app_bytes=len(app), recovery_bytes=len(recovery),
        accepted_release_binding=accepted if normal_build_inputs is None else None,
        normal_build_binding=accepted if normal_build_inputs is not None else None,
        admission_purpose='normal-local-candidate' if normal_build_inputs is not None else
            'development-validation-only' if development_validation else 'release')


def read_archive(data, limits=None):
    """Read fixed/bounded members in memory; never extract supplied paths."""
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            entries = archive.infolist()
            if len(entries) != 3 or len({e.filename for e in entries}) != 3:
                raise ValueError('ZIP requires three unique members')
            if limits is not None and {e.filename for e in entries} != set(limits):
                raise ValueError('Full ZIP requires exactly base.json, full.hex, app-ota.zip')
            for e in entries:
                limit = (limits[e.filename] if limits is not None else
                         4096 if e.filename == 'manifest.json' else
                         512 if e.filename.endswith('.dat') else 163840)
                if (e.flag_bits & 1 or e.is_dir() or stat.S_ISLNK(e.external_attr >> 16)
                        or '/' in e.filename or '\\' in e.filename or e.filename in ('.', '..')
                        or not 0 < e.file_size <= limit):
                    raise ValueError('Invalid/encrypted/oversized ZIP member')
            return {e.filename: archive.read(e) for e in entries}
    except (zipfile.BadZipFile, RuntimeError, KeyError) as error:
        raise ValueError('Invalid Full/OTA ZIP') from error


def varint(data, offset):
    value = 0
    for shift in range(0, 35, 7):
        if offset >= len(data):
            break
        byte = data[offset]; offset += 1
        value |= (byte & 127) << shift
        if byte < 128:
            return value, offset
    raise ValueError('Invalid bounded Nordic init varint')


def fields(data):
    """Only the SDK init's varint/length-delimited subset, no generic decoder."""
    if type(data) is not bytes or len(data)>512:
        raise ValueError('Invalid Nordic init field type/size')
    result, offset = {}, 0
    while offset < len(data):
        tag, offset = varint(data, offset)
        key, wire = tag >> 3, tag & 7
        if not key or key in result or wire not in (0, 2):
            raise ValueError('Duplicate/unsupported Nordic init field')
        value, offset = varint(data, offset)
        if wire == 2:
            if value > len(data) - offset:
                raise ValueError('Truncated Nordic init field')
            value, offset = data[offset:offset + value], offset + value
        result[key] = value
    return result


def signed_app(ota_data):
    files = read_archive(ota_data)
    manifest = json.loads(files['manifest.json'])
    root = manifest['manifest']
    if set(root) != {'application'}:
        raise ValueError('Nordic OTA must be application-only')
    item = root['application']; bn, dn = item['bin_file'], item['dat_file']
    if bn == dn or not bn.endswith('.bin') or not dn.endswith('.dat') or set(files) != {'manifest.json', bn, dn}:
        raise ValueError('Unknown Nordic OTA members')
    app, packet = files[bn], fields(files[dn])
    if set(packet) != {2}:
        raise ValueError('Signed Nordic init required')
    signed = fields(packet[2])
    if set(signed) != {1, 2, 3} or signed[2] != 0 or len(signed[3]) != 64:
        raise ValueError('ECDSA-P256 Nordic init required')
    command = fields(signed[1])
    if set(command) != {1, 2} or command[1] != 1:
        raise ValueError('INIT command required')
    init = fields(command[2]); digest = fields(init[8])
    if (set(init) - set(range(1, 11)) or init.get(2) != 56 or init.get(4, 0) != 0
            or init.get(5, 0) or init.get(6, 0) or init.get(7) != len(app)
            or not 8 <= len(app) <= 163840 or init.get(9, 0)
            or init.get(3) != b'\x81\x02' or set(digest) != {1, 2}
            or digest[1] != 3 or digest[2] != hashlib.sha256(app).digest()[::-1]
            or type(init.get(1)) is not int or not 0 <= init[1] <= 0xffffffff or not init[1] & 65535):
        raise ValueError('Nordic signed App HW56/epoch/hash/size context mismatch')
    validation = fields(init[10])
    if validation != {1: 1, 2: b''}:
        raise ValueError('Nordic generated-CRC boot validation required')
    raw = signed[3]
    key = ec.EllipticCurvePublicNumbers(int.from_bytes(PUBLIC_KEY_LE[:32], 'little'),
        int.from_bytes(PUBLIC_KEY_LE[32:], 'little'), ec.SECP256R1()).public_key()
    try:
        key.verify(utils.encode_dss_signature(int.from_bytes(raw[:32], 'little'),
                   int.from_bytes(raw[32:], 'little')), command[2], ec.ECDSA(hashes.SHA256()))
    except (InvalidSignature, ValueError) as error:
        raise ValueError('Nordic App signature verification failed') from error
    return app, init[1]


def verify_components(full_data, ota_data):
    full = runpy.run_path(str(ROOT / 'tools/sdk/nordic-full-settings.py'))
    with tempfile.TemporaryDirectory(prefix='inktag-nordic-full-') as temporary:
        path = Path(temporary) / 'full.hex'; path.write_bytes(full_data)
        memory = full['HEX']['read_hex'](path)
    if not memory or any(a < 0 or a >= full['IMAGE'] for a in memory):
        raise ValueError('Full overlaps images/UICR/unknown space')
    image = bytes(memory.get(a, 255) for a in range(full['FLASH_BYTES']))
    full['TAIL']['validate_selectors'](image)
    recovery_bytes = full['recovery_extent'](image)
    size, crc, version = full['dfu_validate'](image)
    full['assets_validate'](image)
    _, records = full['journal_decode'](image[full['SETTINGS']:full['RECOVERY']])
    full['validate_template_records'](records,size,crc)
    if image[full['FONTS']:full['SETTINGS']] != b'\xff' * 32768:
        raise ValueError('Full base must have matching basic records and empty user fonts only')
    app, signed_version = signed_app(ota_data)
    if app != image[0x26000:0x26000 + size] or version != signed_version:
        raise ValueError('Full and signed OTA App/version mismatch')
    return memory, dict(app_bytes=size, app_crc32=f'{crc:08X}', app_sha256=sha(app),
        application_limit=163840, application_headroom=163840-size,
        recovery_bytes=recovery_bytes, recovery_limit=24576,
        recovery_headroom=24576-recovery_bytes,
        compatibility_epoch=version >> 16,
        signature_verified=True, recovery_source_authenticated=False,
        recovery_trust='Full member hash integrity only; no independent Recovery signature',
        production_qualified=False)


def verify_bundle(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 2097152:
        raise ValueError('Full ZIP must be a bounded regular file')
    data = read_archive(path.read_bytes(), MEMBERS)
    manifest = json.loads(data['base.json'])
    expected = dict(format_version=1, mcu='nrf52832', firmware_family=FIRMWARE_FAMILY,
        backend_ids=[BOARD_ID], hw_version=56, config_schema=1,
        baseline='inktag-release-v1', layout_version=1, layout_marker='N3V1')
    if (type(manifest) is not dict or set(manifest) != set(expected) | {'images','compatibility_epoch'} or any(
            type(manifest[k]) is not type(v) or manifest[k] != v for k, v in expected.items()) or
            type(manifest['compatibility_epoch']) is not int or not 0 <= manifest['compatibility_epoch'] <= 0xffff):
        raise ValueError('Full ZIP MCU/family/layout/config contract mismatch')
    expected_images = {n: dict(bytes=len(data[n]), sha256=sha(data[n])) for n in ('full.hex', 'app-ota.zip')}
    if manifest['images'] != expected_images:
        raise ValueError('Full ZIP artifact hash/size mismatch')
    memory, review = verify_components(data['full.hex'], data['app-ota.zip'])
    if manifest['compatibility_epoch'] != review['compatibility_epoch']:
        raise ValueError('Full epoch disagrees with signed App/native DFU context')
    review.update(package_verified=True, package_sha256=sha(path.read_bytes()),
                  base_firmware_family=manifest['firmware_family'])
    return memory, review


def create_bundle(full_path, ota_path, output, *, development_validation=False, normal_build_inputs=None):
    output = Path(output)
    if output.exists():
        raise ValueError('Full bundle already exists; preserve it and choose a new output')
    full_data, ota_data = Path(full_path).read_bytes(), Path(ota_path).read_bytes()
    memory, review = verify_components(full_data, ota_data)
    # Candidate/source approval is internal production/signing policy ONLY.
    # User Full verification above deliberately does not invoke either table.
    full = runpy.run_path(str(ROOT/'tools/sdk/nordic-full-settings.py'))
    image = bytes(memory.get(a,255) for a in range(full['FLASH_BYTES']))
    if normal_build_inputs is not None:
        n = len((ROOT/'_build/nordic/nrf52832_priceetag/bootloader/InkTag-nrf52832_priceetag-bootloader.bin').read_bytes())
        if not 0 < n <= 24576 or image[full['RECOVERY']+n:full['MBR_PARAMS']] != b'\xff' * (24576-n):
            raise ValueError('Normal Full Recovery extent disagrees with its actual linked output')
    else:
        n = full['known_recovery'](image,candidate=True)
    app = image[0x26000:0x26000+review['app_bytes']]
    recovery = image[full['RECOVERY']:full['RECOVERY']+n]
    if normal_build_inputs is not None:
        if development_validation: raise ValueError('Normal build and historical development admission are distinct')
        from nordic_build_candidate import validate
        validate(normal_build_inputs, app, recovery, ROOT)
    else:
        require_release_binding(app, recovery,
            **({'development_validation':True} if development_validation else {}))
    files = {'full.hex': full_data, 'app-ota.zip': ota_data}
    manifest = dict(format_version=1, mcu='nrf52832', firmware_family=FIRMWARE_FAMILY,
        backend_ids=[BOARD_ID], hw_version=56, compatibility_epoch=review['compatibility_epoch'], config_schema=1,
        baseline='inktag-release-v1', layout_version=1, layout_marker='N3V1',
        images={n: dict(bytes=len(v), sha256=sha(v)) for n, v in files.items()})
    files['base.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, payload in sorted(files.items()):
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, payload)
    return verify_bundle(output)[1]
