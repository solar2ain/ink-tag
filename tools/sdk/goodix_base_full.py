"""Unsigned, CRC-only GR first-install Full format. No signing or device I/O.

This format is distinct from signed Pro Full. CRC detects corruption, not
publisher authenticity. Existing Pro producers and consumers are unchanged.
"""
from pathlib import Path
import base64
import ast
import hashlib
import io
import json
import re
import stat
import struct
import sys
import zipfile
import zlib

from goodix_app_integrity import verify as verify_integrity
from goodix_base_elf import Elf, verify as verify_code
from goodix_native_package import BOARDS, checksum, image_info, json_bytes, record, write_zip
from goodix_recovery_version import recovery_binary_identity

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = 'inktag-gr-base-full-v1'
MEMBERS = {'full.json': 16384, 'app-ota.zip': 1048576,
           'recovery.bin': 114688, 'gui-font.bin': 32768}
CHIPS = {'gr5331_zkc21v': ('GR5331',),
         'gr551x_zkc42v': ('GR5513', 'GR5515'),
         'gr5513_zkel_internal': ('GR5513',)}
FAMILY_MARKERS = {'gr5331_zkc21v': b'gr5331_zkel\0',
                  'gr551x_zkc42v': b'gr551x_zkel\0',
                  'gr5513_zkel_internal': b'gr5513_zkel_internal\0'}


def require(value, message):
    if not value:
        raise ValueError(message)


def _unique(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'Duplicate JSON field: '+key)
        result[key] = value
    return result


def _json(raw):
    return json.loads(raw.decode('utf-8'), object_pairs_hook=_unique)


def archive_bytes(raw, limits):
    require(type(raw) is bytes and 0 < len(raw) <= sum(limits.values())+65536,
            'Invalid bounded archive size')
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        infos = archive.infolist()
        require(len(infos) == len(limits) and
                {item.filename for item in infos} == set(limits), 'Unexpected archive members')
        result = {}
        for item in infos:
            require(item.filename not in result and not item.flag_bits & 1 and
                    item.compress_type == zipfile.ZIP_STORED and
                    stat.S_IFMT(item.external_attr >> 16) in (0, stat.S_IFREG) and
                    0 < item.file_size <= limits[item.filename] and
                    item.compress_size == item.file_size, 'Invalid archive member')
            result[item.filename] = archive.read(item)
    return result


def base_ota(raw):
    data = archive_bytes(raw, {'manifest.json': 16384, 'application.bin': 1048576})
    manifest = _json(data['manifest.json'])
    family = manifest.get('board')
    require(family in BOARDS and manifest.get('format') == 'inktag-ota-v1' and
            manifest.get('sourceProfile') == 'base' and
            manifest.get('transport') == 'goodix-otas-v2' and
            manifest.get('layout') == 'layout-v1' and manifest.get('compatibilityEpoch') == 1 and
            manifest.get('signature') == {'required': False, 'algorithm': 'none', 'policy': 'crc-only'},
            'An explicit CRC-only base App is required; Pro packages are separate')
    app = manifest['application']; firmware = data['application.bin']; size = app['size']
    require(type(size) is int and 40 < size <= BOARDS[family]['slot_size']-904 and
            len(firmware) == size+48 and app.get('transfer_size') == len(firmware) and
            app.get('active_address') == BOARDS[family]['active'] and
            app.get('slot_size') == BOARDS[family]['slot_size'] and
            app.get('sha256') == record(firmware)['sha256'] and
            app.get('checksum') == checksum(firmware[:size]) and
            firmware[size:] == image_info(firmware[:size], family, 1),
            'base App native size/checksum/family descriptor mismatch')
    release = manifest.get('release')
    match = re.fullmatch(r'(\d+)\.(\d+)\.(\d+)(?:\.(\d+))?', release) if isinstance(release, str) else None
    require(match is not None, 'Invalid base App semantic release')
    parts = tuple(int(part) for part in match.groups() if part is not None)
    protocol = manifest.get('protocol')
    require(type(protocol) is int and 0 <= protocol <= 255 and all(part <= 255 for part in parts) and
            manifest.get('releaseVersion') == parts[0]*10000 + parts[1]*100 + parts[2] and
            bytes((protocol, *parts)) in firmware[:size-40], 'base App GATT release mismatch')
    integrity = verify_integrity(firmware[:size], board=family, epoch=1)
    linked = firmware[:integrity['bodyBytes']]
    require(app.get('integrity') == integrity and app.get('linked_size') == len(linked) and
            app.get('linked_sha256') == record(linked)['sha256'] and
            FAMILY_MARKERS[family] in linked, 'base App IKCI/native family mismatch')
    return manifest, firmware, linked


def recovery_policy(raw, expected_version, policy_offset=None):
    require(type(raw) is bytes and 0 < len(raw) <= 0x1c000 and
            b'IKSP\x01\x02' in raw and b'IKSP\x01\x00' not in raw and
            b'IKSP\x01\x01' not in raw, 'Actual unambiguous CRC-only Recovery policy is required')
    if policy_offset is not None:
        require(type(policy_offset) is int and 0 <= policy_offset <= len(raw)-6 and
                raw[policy_offset:policy_offset+6] == b'IKSP\x01\x02',
                'Bound Recovery policy offset mismatch')
    identity = recovery_binary_identity(raw)
    require(identity == {'version': expected_version, 'epoch': 1}, 'Recovery version/epoch mismatch')
    return identity


def default_gui(repo=ROOT):
    """Use only the fixed built-in template; never invoke a CLI fallback."""
    template = _json((repo/'firmware/factory/native-font-template.json').read_bytes())
    raw = base64.b64decode(template['package_base64'], validate=True)
    require(template.get('format_version') == 1 and template.get('package_schema') == 1 and
            88 <= len(raw) <= 32768 and raw[:8] == b'IKFA\x01\x00\x58\x00' and
            hashlib.sha256(raw).hexdigest() == template['package_sha256'],
            'Invalid fixed native GUI template')
    source = (repo/'firmware/common/gui/inktag_gui_fonts.c').read_text(encoding='utf-8')
    fonts = []
    for name in ('u8g2_font_wqy9_t_lunar', 'u8g2_font_wqy12_t_lunar',
                 'u8g2_font_helvB14_tn', 'u8g2_font_helvB18_tn'):
        match = re.search(r'const\s+uint8_t\s+'+name+r'\[(\d+)\][\s\S]*?=\s*((?:"(?:\\.|[^"\\])*"\s*)+);', source)
        require(match is not None, 'Missing maintained native source font')
        font = b''.join(ast.literal_eval(s).encode('latin-1') for s in
                        re.findall(r'"(?:\\.|[^"\\])*"', match[2]))+b'\0'
        require(len(font) == int(match[1]), 'Source font extent mismatch')
        fonts.append(struct.pack('<I', len(font))+font)
    require(hashlib.sha256(b''.join(fonts)).hexdigest() == template['source_fonts_sha256'],
            'Native GUI template does not match the maintained source fonts')
    candidate = bytearray(raw); stored_crc = struct.unpack_from('<I', candidate, 16)[0]
    candidate[16:20] = bytes(4)
    require(zlib.crc32(candidate) == stored_crc, 'Native GUI CRC mismatch')
    struct.pack_into('<I', candidate, 8, 1)
    struct.pack_into('<I', candidate, 16, zlib.crc32(candidate))
    return bytes(candidate)


def gui_integrity(raw):
    require(type(raw) is bytes and 88 <= len(raw) <= 32768 and
            raw[:8] == b'IKFA\x01\x00\x58\x00', 'Invalid native GUI header/size')
    candidate = bytearray(raw); expected = struct.unpack_from('<I', candidate, 16)[0]
    candidate[16:20] = bytes(4)
    require(zlib.crc32(candidate) == expected, 'Native GUI CRC mismatch')
    require(struct.unpack_from('<I', raw, 12)[0] == len(raw) and raw[20:24] == b'\x04\0\0\0',
            'Invalid default native GUI table')
    end = 88
    for i in range(4):
        kind, face, encoding, flags, offset, length, crc = struct.unpack_from('<4B3I', raw, 24+16*i)
        require((kind, face, encoding, flags) == (1, i, 1, 0) and offset == end and
                length >= 31 and length <= len(raw)-offset and
                zlib.crc32(raw[offset:offset+length]) == crc, 'Invalid native GUI face entry')
        end += length
    require(end == len(raw), 'Unbound native GUI trailing bytes')
    return dict(**record(raw), crc32=expected)


def _host_nvds():
    # This namespace contains pure, frozen programmer codecs, not the user CLI.
    path = str(ROOT/'tools/hardware')
    if path not in sys.path:
        sys.path.insert(0, path)
    import goodix_host_nvds
    return goodix_host_nvds


def build_base(ota_path, app_elf, recovery_path, recovery_elf, output, *, repo=ROOT):
    """Produce one deterministic base from real native base ELF/BIN products."""
    require(not output.exists(), 'Refusing to overwrite base Full output')
    ota = ota_path.read_bytes(); manifest, firmware, linked = base_ota(ota)
    family = manifest['board']; recovery = recovery_path.read_bytes()
    version = _json((repo/'version.json').read_bytes())
    require(manifest['release'] == version['version'], 'App software version differs from source')
    verify_code(app_elf.read_bytes(), 'app', linked, family)
    verify_code(recovery_elf.read_bytes(), 'recovery', recovery, family)
    identity = recovery_policy(recovery, version['recoveryVersion'])
    rec = Elf(recovery_elf.read_bytes()); policy = rec.symbol('inktag_recovery_policy_info')
    require(policy['bytes'] == 6 and rec.slice(rec.offset(policy['address'], 6), 6) == b'IKSP\x01\x02',
            'Recovery ELF does not bind the CRC-only policy')
    policy_offset = policy['address']-(BOARDS[family]['active']-0x20000+0x4000)
    recovery_policy(recovery, version['recoveryVersion'], policy_offset)
    elf = Elf(app_elf.read_bytes()); qr = elf.symbol('inktag_default_device_qr')
    require(qr['bytes'] == 79, 'Unreviewed App default QR extent')
    matrix = elf.slice(elf.offset(qr['address'], 79), 79)
    off = struct.pack('<BBBHI', 1, 0, 25, len(matrix), zlib.crc32(matrix))+matrix
    nvds = _host_nvds(); gui = default_gui(repo)
    payloads = {'app-ota.zip': ota, 'recovery.bin': recovery, 'gui-font.bin': gui}
    metadata = dict(schema=SCHEMA, sourceProfile='base', family=family,
        chips=list(CHIPS[family]), layout='layout-v1', compatibilityEpoch=1,
        integrityPolicy='crc-only', publisherSignature=False, release=version['version'],
        recovery=identity, recoveryPolicyOffset=policy_offset,
        hostNvds={chip: nvds.recipe(chip, off) for chip in CHIPS[family]},
        gui=record(gui), files={name: record(value) for name, value in payloads.items()})
    write_zip(output, {'full.json': json_bytes(metadata), **payloads})
    return inspect_base(output.read_bytes())


def inspect_base(raw):
    data = archive_bytes(raw, MEMBERS); metadata = _json(data['full.json'])
    family = metadata.get('family')
    require(metadata.get('schema') == SCHEMA and metadata.get('sourceProfile') == 'base' and
            family in BOARDS and metadata.get('chips') == list(CHIPS[family]) and
            metadata.get('layout') == 'layout-v1' and metadata.get('compatibilityEpoch') == 1 and
            metadata.get('integrityPolicy') == 'crc-only' and metadata.get('publisherSignature') is False,
            'Not an explicit compatible base Full container')
    manifest, firmware, linked = base_ota(data['app-ota.zip'])
    require(manifest['board'] == family and manifest['release'] == metadata['release'],
            'base Full/App family/version mismatch')
    require(set(metadata['files']) == set(MEMBERS)-{'full.json'}, 'Incomplete payload bindings')
    for name in metadata['files']:
        require(record(data[name]) == metadata['files'][name], 'Full payload hash/size mismatch: '+name)
    require(metadata['gui'] == record(data['gui-font.bin']), 'GUI binding mismatch')
    gui_integrity(data['gui-font.bin'])
    recovery_policy(data['recovery.bin'], metadata['recovery']['version'], metadata['recoveryPolicyOffset'])
    require(metadata['recovery']['epoch'] == 1 and set(metadata['hostNvds']) == set(CHIPS[family]),
            'Incomplete Recovery/NVDS metadata')
    nvds = _host_nvds()
    for chip in CHIPS[family]:
        nvds.check_recipe(metadata['hostNvds'][chip], chip)
    return dict(metadata=metadata, appManifest=manifest, payloads=data,
                app=firmware, linked=linked, hardwareAccessed=False)


def geometry(family, chip):
    require(family in CHIPS and chip in CHIPS[family], 'Incompatible exact chip/family')
    small = chip == 'GR5331'; internal = family == 'gr5513_zkel_internal'
    base = 0x00200000 if small else 0x01000000
    font = 0x60000 if small else 0x64000 if internal else 0x72000
    nvds, nvds_bytes = _host_nvds().extent(chip, family)
    return dict(base=base, capacity=0x100000 if chip == 'GR5515' else 0x80000,
                recovery=0x4000, app=0x20000, appEnd=font, gui=font,
                guiBytes=0x8000, journal=font+0x8000, journalBytes=0x2000,
                nvds=nvds, nvdsBytes=nvds_bytes)


def initial_journal(family):
    require(family in BOARDS, 'Unknown native Recovery journal family')
    _host_nvds()
    from goodix_offline.generated_release_v1 import RECOVERY_JOURNAL_MAGIC
    # Current common C layout-v1 serializer: confirmed generation1/sequence1,
    # metadata1, epoch1. This is first-install construction, not OTA rollback.
    body = struct.pack('<4IBBH2I', RECOVERY_JOURNAL_MAGIC, BOARDS[family]['layout_marker'],
                       1, 1, 3, 1, 0xffff, 1, 0xffffffff)
    return (body+struct.pack('<I', zlib.crc32(body))).ljust(8192, b'\xff')


def configured_image(raw, config, *, address=None, address_kind=None):
    """Offline construction. Live identity/erase admission belongs to the tool.

    No default MAC exists. The physical front must supply an attested typed
    address only after fresh exact-chip/capacity and vendor-data capture.
    """
    reviewed = inspect_base(raw); metadata = reviewed['metadata']; family = metadata['family']
    _host_nvds()  # Adds only the maintained tool-private pure codec directory.
    import goodix_pin_config as pins
    config_bytes = config.read_bytes(); selected = pins.read_board_config(config)
    require(config.read_bytes() == config_bytes, 'Board configuration changed during preparation')
    backend = selected['backend']; facts = pins.FACTS[backend]
    chip = {0x31: 'GR5331', 0x13: 'GR5513', 0x15: 'GR5515'}[facts['mcu']]
    g = geometry(family, chip)
    screen = bytes.fromhex(selected['screen_wire_hex'])
    require(screen[1] not in (pins.ScreenControllerV2.JD79665CA,
                             pins.ScreenControllerV2.IST7513,
                             pins.ScreenControllerV2.IST7601),
            'This screen controller is not compiled into GR base')
    require((family == 'gr5513_zkel_internal') == (backend == 'gr5513_zkc21v'),
            'Internal executable layout requires the exact internal board')
    selector, profile = pins.factory_records(backend,
        bytes.fromhex(selected['pin_wire_hex']), screen)
    app = reviewed['app']; recovery = reviewed['payloads']['recovery.bin']
    gui = reviewed['payloads']['gui-font.bin']
    require(g['app']+len(app) <= g['appEnd'] and len(recovery) <= 0x1c000 and
            len(gui) <= g['guiBytes'] and g['journal']+g['journalBytes'] <= g['nvds'],
            'Native payload overlaps a fixed partition')
    image = bytearray(b'\xff'*g['capacity']); boot = bytearray(b'\xff'*0x4000)
    entry = g['base']+g['recovery']
    header = struct.pack('<6I', len(recovery), checksum(recovery), entry, entry, 3, 0x744)
    boot[:24] = header
    descriptor = struct.pack('<HH', 0x4744, 1)+header+b'inktag_boot'.ljust(12, b'\0')
    boot[0x40:0x70 if chip == 'GR5331' else 0x68] = descriptor.ljust(48, b'\0') if chip == 'GR5331' else descriptor
    boot[0xf00:0xf0c] = selector; boot[0xf20:0xf60] = profile
    boot[0x2000:0x2030] = app[-48:]
    boot[0x3ff8:0x4000] = struct.pack('<II', 0x31444649, ~0x31444649 & 0xffffffff)
    image[:0x4000] = boot; image[0x4000:0x4000+len(recovery)] = recovery
    image[0x20000:0x20000+len(app)] = app
    image[g['gui']:g['gui']+len(gui)] = gui
    image[g['journal']:g['journal']+0x2000] = initial_journal(family)
    identity_prepared = address is not None or address_kind is not None
    if identity_prepared:
        require(type(address) is bytes and len(address) == 6 and address_kind in (0, 1),
                'Explicit typed hardware identity is required')
        native = _host_nvds().prepare_full(chip, address, address_kind,
            metadata['hostNvds'][chip], family=family)
        require(len(native) == g['nvdsBytes'], 'Native NVDS extent mismatch')
        image[g['nvds']:g['nvds']+len(native)] = native
    return dict(schema='inktag-gr-base-configured-full-v1', sourceProfile='base',
                family=family, chip=chip, backend=backend, layout='layout-v1',
                compatibilityEpoch=1, geometry=g, config=record(config_bytes),
                image=record(image), nativeIdentityPrepared=identity_prepared,
                physicalIdentityAttested=False, hardwareAccessed=False,
                firstInstallOnly=True), bytes(image)


def first_install_image(raw, config, *, current_image, current_nvds, second_nvds,
                        uid, factory_address, factory_type):
    """Pure first-install guard; inputs must come from the front's fresh probe.

    Existing InkTag installation/migration is deliberately outside this entry.
    It never clears an unknown log or changes an existing ordinary identity.
    """
    report, _ = configured_image(raw, config)
    g = report['geometry']; chip = report['chip']; family = report['family']
    require(type(current_image) is bytes and len(current_image) == g['capacity'],
            'Incomplete current physical MCU capture; no erase')
    require(type(uid) is bytes and len(uid) == 16 and uid not in (bytes(16), b'\xff'*16),
            'A fresh complete immutable UID is required; no erase')
    require(type(current_nvds) is bytes and current_nvds == second_nvds and
            len(current_nvds) == g['nvdsBytes'] and
            current_image[g['nvds']:g['nvds']+g['nvdsBytes']] == current_nvds,
            'Physical MCU/NVDS captures disagree; no erase')
    markers = (b'INKTAG-GR-RELEASE1\0', b'IKT_GR_', b'IBD1', b'GPF1',
               b'G3V1', b'G5V1', b'GIV1', b'InkTag', b'INKTAG')
    require(not any(marker in current_image for marker in markers),
            'Existing InkTag installation requires its maintenance path; no erase')
    nvds = _host_nvds(); found, deleted = nvds.read_records(current_nvds, chip, family)
    require(not deleted and not any(0x5201 <= tag <= 0x5212 for tag in found),
            'Existing or deleted InkTag authority/user records; no erase')
    from ble_factory_address import validate
    require(type(factory_address) is bytes and type(factory_type) is int,
            'Explicit immutable Factory address/type is required; no erase')
    validate(factory_address, factory_type)
    require(0xc001 not in found or found[0xc001] == factory_address[::-1],
            'Existing SDK address differs from attested Factory identity; no erase')
    report, image = configured_image(raw, config, address=factory_address, address_kind=factory_type)
    image = bytearray(image)
    reviewed = inspect_base(raw)
    vendor = tuple((tag, value) for tag, value in sorted(found.items()) if tag != 0xc001)
    native = nvds.prepare_full(chip, factory_address, factory_type,
        reviewed['metadata']['hostNvds'][chip], extra_records=vendor, family=family)
    decoded, deleted = nvds._linear_records(native, 1, nvds.full_data_limit(chip, family))
    require(not deleted, 'Unexpected tombstone in fresh prepared NVDS')
    for tag, value in vendor:
        require(decoded.get(tag) == value,
                'Prepared vendor data roundtrip mismatch; no erase')
    image[g['nvds']:g['nvds']+len(native)] = native
    report.update(image=record(image), currentImage=record(current_image),
                  currentNvds=record(current_nvds), uidSha256=record(uid)['sha256'],
                  vendorRecordsPreserved=len(vendor), twoFreshNvdsReadsMatched=True,
                  nativeRecoveryReinitialized=True,
                  address=':'.join(f'{v:02X}' for v in factory_address), addressType=factory_type)
    return report, bytes(image)
