"""Offline-only, purpose-signed GR factory-init envelope and base inspection.

No key loading, SDK/ELF dependency, GPIO, debugger, flash writer or executor.
The unchanged physical runner does not consume this format. Verification is
not authorization to execute it; private helper semantics/qualification remain
separate gates. Public verification first uses the maintained App trust chain.
"""
from __future__ import annotations

import hashlib
import io
import json
from pathlib import Path
import stat
import struct
import sys
import tempfile
import zipfile

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, padding, rsa
from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from goodix_offline import goodix_package as ota

PURPOSE = 'InkTag factory-init'
DOMAIN = PURPOSE.encode('ascii') + b'\0'
PREFIX = struct.Struct('<4sHHII')
MAX_METADATA = 4096
MAX_OTA_MANIFEST = 16384
MEMBERS = ('base.json', 'app-ota.zip', 'recovery.bin', 'factory-init.ikfi')
MAX_MEMBERS = dict(zip(MEMBERS, (4096, 1048576, 114688, 32768)))
FAMILY_CHIPS = ('GR5513', 'GR5515')
FAMILY_MEMBERS = ('base.json', 'app-ota.zip', 'recovery.bin',
                  'factory-init-GR5513.ikfi', 'factory-init-GR5515.ikfi')
MEMBER_LIMITS = {**MAX_MEMBERS, **{name: 32768 for name in FAMILY_MEMBERS[3:]}}

# Fixed reviewed physical RAM and execution ABI, not manifest-selected ranges.
RAM = {
    'GR5331': dict(board='gr5331_zkc21v', layout='layout-v1',
        marker=int.from_bytes(b'G3V1','little'), packages=[5331], load=0x2000af00, limit=0x2000d400,
        stack=[0x2000d500, 0x2000de80], mailbox=0x2000d400, recovery=0x00204000),
    'GR5513': dict(board='gr551x_zkc42v', layout='layout-v1',
        marker=int.from_bytes(b'G5V1','little'), packages=[5, 7], load=0x30018000, limit=0x3001c000,
        stack=[0x3001c000, 0x3001e000], mailbox=0x30017c00, recovery=0x01004000),
    'GR5515': dict(board='gr551x_zkc42v', layout='layout-v1',
        marker=int.from_bytes(b'G5V1','little'), packages=[4], load=0x30018000, limit=0x3001c000,
        stack=[0x3001c000, 0x3001e000], mailbox=0x30017c00, recovery=0x01004000),
}
INTERNAL_FAMILY = 'gr5513_zkel_internal'


def profile(chip, family=None):
    """Select fixed executable geometry, never an untrusted address/size."""
    require(chip in RAM, 'Unknown Full MCU')
    if family == INTERNAL_FAMILY:
        require(chip == 'GR5513', 'Internal layout requires exact GR5513')
        return {**RAM[chip], 'board': INTERNAL_FAMILY, 'packages': [7],
                'marker': int.from_bytes(b'GIV1', 'little')}
    require(family in (None, RAM[chip]['board']), 'Unknown MCU/family tuple')
    return RAM[chip]

FIELDS = frozenset(('purpose', 'version', 'chip', 'packages', 'layout',
    'layoutMarker', 'compatibilityEpoch', 'algorithm', 'ramAbi', 'operationProfile',
    'appSha256', 'signedAppSha256', 'recoverySha256', 'helperSha256',
    'helperBytes', 'loadAddress', 'entry', 'stack', 'bssStart', 'bssEnd',
    'mailbox', 'mailboxBytes', 'buildReceiptSha256','releaseBaseline'))
READINESS_FIELD = 'appBootStageAddress'
READY_CONTEXT_FIELD = 'appReadyContext'
HOST_NVDS_FIELD = 'hostNvds'
CONFIG_SCHEMA_FIELD = 'configSchema'
RELEASE_BASELINE = 'inktag-release-v1'
RELEASE_CONSUMER = b'INKTAG-GR-RELEASE1\0'


def require_release_consumer(metadata, signed_app):
    """Caller authenticates the complete package first; labels alone do not adopt v1.

    This pure guard does not switch current inspectors or programming recipes.
    The complete new vendor/Full batch must call it after authentication and
    before any target access; preparation alone is not that adoption.
    """
    schema = metadata.get(CONFIG_SCHEMA_FIELD)
    require(metadata.get('releaseBaseline') == RELEASE_BASELINE and
            type(schema) is int and schema == 1 and
            type(signed_app) is bytes and len(signed_app) > 904 and
            RELEASE_CONSUMER in signed_app[:-904],
            'Full requires the authenticated release-v1 App/Recovery binding; '
            'old configuration archives are not execution inputs')


def readiness_address(metadata: dict, chip: str) -> int:
    """One signed ELF-bound diagnostic word; no operator-selected reads."""
    require(chip in RAM, 'Unknown readiness MCU')
    address = metadata.get(READINESS_FIELD)
    cfg = RAM[chip]
    start = 0x20000000 if chip == 'GR5331' else 0x30000000
    require(type(address) is int and address % 4 == 0 and
            start <= address and address + 4 <= min(cfg['load'],cfg['mailbox']),
            'Full requires a signed App boot-stage binding; old archive is inspection-only')
    return address


def readiness_context(metadata: dict, chip: str, raw_app=None) -> dict:
    """Signed exact post-init WFE PC/main stack, never arbitrary App code."""
    require(chip in RAM, 'Unknown readiness MCU')
    context = metadata.get(READY_CONTEXT_FIELD)
    require(type(context) is dict and set(context)=={'kind','pc','stack'} and
            context['kind']=='post-init-main-wfe-v1',
            'Full requires a signed App ready execution context; old archive is inspection-only')
    base = 0x00220000 if chip=='GR5331' else 0x01020000
    end = (0x01064000 if metadata.get('layoutMarker') == 0x31564947 else
           0x00260000 if chip=='GR5331' else 0x01072000)
    pc, stack = context['pc'], context['stack']
    require(type(pc) is int and pc%2==0 and base+2 <= pc < end,
            'Invalid App ready execution PC')
    ram_end = 0x20018000 if chip=='GR5331' else 0x30020000
    require(type(stack) is list and len(stack)==2 and all(type(n) is int for n in stack) and
            RAM[chip]['stack'][1] <= stack[0] < stack[1] <= ram_end and
            0 < stack[1]-stack[0] <= 0x2000 and all(n%8==0 for n in stack),
            'Invalid App main-stack context')
    if raw_app is not None:
        require(pc-base < len(raw_app) and raw_app[pc-base-2:pc-base]==b'\x20\xbf',
                'App ready PC does not follow WFE in the bound executable')
    return context


def require(ok: bool, message: str) -> None:
    if not ok:
        raise ValueError(message)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical(value: dict) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(',', ':'),
                      ensure_ascii=True, allow_nan=False).encode('ascii')


def _object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'Duplicate JSON key')
        result[key] = value
    return result


def _json(data: bytes) -> dict:
    try:
        value = json.loads(data.decode('ascii'), object_pairs_hook=_object,
            parse_constant=lambda _: (_ for _ in ()).throw(ValueError('Nonfinite JSON')))
    except (UnicodeError, RecursionError) as error:
        raise ValueError('Invalid bounded ASCII JSON') from error
    require(type(value) is dict, 'Expected JSON object')
    return value


def signing_message(metadata: dict, helper: bytes) -> bytes:
    """Private/fixture packaging input; does not sign, load keys or qualify code."""
    encoded = canonical(metadata)
    require(0 < len(encoded) <= MAX_METADATA and 64 < len(helper) <= 16384,
            'Envelope length outside reviewed bounds')
    return DOMAIN + PREFIX.pack(b'IKFI', 1, 0, len(encoded), len(helper)) + encoded + helper


def envelope(metadata: dict, helper: bytes, signature: bytes) -> bytes:
    """Assemble an externally signed message. Verification is always separate."""
    require(len(signature) in (64, 256), 'Invalid family signature size')
    return signing_message(metadata, helper)[len(DOMAIN):] + signature


def _metadata(m: dict, helper: bytes, package, recovery: bytes, chip: str) -> dict:
    fields = set(m) - {HOST_NVDS_FIELD, CONFIG_SCHEMA_FIELD}
    require(chip in RAM and fields in (FIELDS,FIELDS | {READINESS_FIELD},
            FIELDS | {READINESS_FIELD,READY_CONTEXT_FIELD}),
            'Unknown chip or metadata fields')
    manifest = package.manifest
    cfg = profile(chip, manifest['board'])
    require(manifest['board'] == cfg['board'] and manifest['layout'] == cfg['layout'],
            'App MCU/layout mismatch')
    pin = ota.TRUSTED_SIGNING_KEYS[manifest['board']]
    size = manifest['application']['size']
    require_release_consumer(m,package.firmware)
    require(m['purpose'] == PURPOSE and type(m['version']) is int and m['version'] == 1,
            'Wrong signed purpose/version')
    require(m['chip'] == chip and m['packages'] == cfg['packages'] and
            all(type(p) is int for p in m['packages']), 'Wrong exact MCU/packages')
    require(m['layout'] == cfg['layout'] and type(m['layoutMarker']) is int and
            m['layoutMarker'] == cfg['marker'], 'Wrong signed layout')
    require(type(m['compatibilityEpoch']) is int and
            m['compatibilityEpoch'] == manifest['compatibilityEpoch'], 'Wrong compatibility epoch')
    require(m['algorithm'] == pin['algorithm'], 'Wrong family signature algorithm')
    for key in ('ramAbi', 'helperBytes', 'loadAddress', 'entry', 'bssStart',
                'bssEnd', 'mailbox', 'mailboxBytes'):
        require(type(m[key]) is int, 'Non-integer RAM ABI field')
    require((m['ramAbi'], m['operationProfile'], m['mailboxBytes']) in
            ((1, 'goodix-first-off-v1', 72), (2, 'goodix-identity-off-v2', 84)),
            'Unreviewed RAM ABI/operation profile')
    if READINESS_FIELD in m:
        require(m['ramAbi']==2,'Readiness binding requires typed ABI2')
        readiness_address(m,chip)
    if READY_CONTEXT_FIELD in m:
        readiness_context(m,chip,package.firmware[:size])
    if HOST_NVDS_FIELD in m:
        import goodix_host_nvds
        goodix_host_nvds.check_recipe(m[HOST_NVDS_FIELD], chip)
    for key in ('appSha256', 'signedAppSha256', 'recoverySha256', 'helperSha256',
                'buildReceiptSha256'):
        require(type(m[key]) is str and len(m[key]) == 64 and
                all(c in '0123456789abcdef' for c in m[key]) and m[key] != '0'*64,
                'Invalid signed digest')
    require(m['appSha256'] == sha(package.firmware[:size]) and
            m['signedAppSha256'] == sha(package.firmware) and
            m['recoverySha256'] == sha(recovery) and m['helperSha256'] == sha(helper),
            'Executable payload binding mismatch')
    require(m['helperBytes'] == len(helper) and 64 < len(helper) < cfg['limit']-cfg['load'],
            'Invalid helper extent')
    require(m['loadAddress'] == cfg['load'] and m['entry'] == cfg['load']+65 and
            m['stack'] == cfg['stack'] and all(type(n) is int for n in m['stack']) and
            m['mailbox'] == cfg['mailbox'],
            'Unreviewed fixed RAM/entry/stack/mailbox')
    require(m['bssStart'] == ((cfg['load']+len(helper)+7) & ~7) and
            m['bssStart'] <= m['bssEnd'] <= cfg['limit'], 'Invalid BSS envelope')
    vectors = struct.unpack_from('<16I', helper)
    require(vectors[0] == cfg['stack'][1] and vectors[1] == m['entry'] and
            len(set(vectors[2:7])) == 1 and vectors[2] & 1 and
            cfg['load']+64 <= (vectors[2] & ~1) < cfg['load']+len(helper) and
            not any(vectors[7:]), 'Unreviewed helper vector ABI')
    require(8 <= len(recovery) <= 114688, 'Invalid Recovery extent')
    stack, entry = struct.unpack_from('<II', recovery)
    ram_start = 0x20000000 if chip == 'GR5331' else 0x30000000
    # Existing qualified Recovery uses GR5331's SDK96KiB RAM envelope, not
    # the unrelated helper's much smaller reviewed scratch window.
    ram_end = ram_start + (0x18000 if chip == 'GR5331' else 0x20000)
    require(ram_start < stack <= ram_end and stack % 8 == 0 and entry & 1 and
            cfg['recovery'] <= (entry & ~1) < cfg['recovery']+len(recovery),
            'Invalid authenticated Recovery vectors')
    return pin


def _verify(payload: bytes, package, recovery: bytes, chip: str) -> dict:
    require(len(payload) >= PREFIX.size, 'Short IKFI envelope')
    magic, version, reserved, count, length = PREFIX.unpack_from(payload)
    require(magic == b'IKFI' and version == 1 and reserved == 0 and
            0 < count <= MAX_METADATA and 64 < length <= 16384, 'Invalid IKFI prefix')
    raw = payload[PREFIX.size:PREFIX.size+count]
    m = _json(raw)
    require(canonical(m) == raw, 'Noncanonical IKFI metadata')
    helper = payload[PREFIX.size+count:PREFIX.size+count+length]
    require(len(helper) == length, 'Truncated helper')
    pin = _metadata(m, helper, package, recovery, chip)
    signature = payload[PREFIX.size+count+length:]
    signature_size = 256 if pin['algorithm'] == 'rsa-pss-sha256' else 64
    require(len(signature) == signature_size, 'Truncated/trailing family signature')
    size = package.manifest['application']['size']
    public_size = ota.RSA_PUBLIC_KEY_SIZE if signature_size == 256 else ota.ECDSA_PUBLIC_KEY_SIZE
    public = package.firmware[size+ota.RSA_PUBLIC_KEY_OFFSET:
                              size+ota.RSA_PUBLIC_KEY_OFFSET+public_size]
    require(hashlib.sha256(public).digest()[:16] == pin['public_key_hash'],
            'Helper trust key is not the verified family App key')
    message = DOMAIN + payload[:-signature_size]
    try:
        if signature_size == 256:
            rsa.RSAPublicNumbers(int.from_bytes(public[256:260], 'little'),
                int.from_bytes(public[:256], 'little')).public_key().verify(
                    signature[::-1], message, padding.PSS(mgf=padding.MGF1(hashes.SHA256()),
                    salt_length=32), hashes.SHA256())
        else:
            ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), public).verify(
                encode_dss_signature(int.from_bytes(signature[:32], 'big'),
                    int.from_bytes(signature[32:], 'big')), message, ec.ECDSA(hashes.SHA256()))
    except (InvalidSignature, ValueError) as error:
        raise ValueError('Independent factory-init signature rejected') from error
    return m


def _bounded_app_members(blob: bytes, chip: str) -> dict[str, bytes]:
    """Bound the nested transport envelope, not OTA crypto or product policy."""
    cfg = RAM[chip]
    slot = ota.LAYOUT_PROFILES[(cfg['board'], cfg['layout'])]['slot_size']
    with zipfile.ZipFile(io.BytesIO(blob)) as archive:
        entries = archive.infolist()
        names = [item.filename for item in entries]
        require(len(names) == 2 and len(set(names)) == 2 and 'manifest.json' in names,
                'Nested OTA duplicate, missing or extra member')
        for item in entries:
            name = item.filename
            mode = stat.S_IFMT(item.external_attr >> 16)
            limit = MAX_OTA_MANIFEST if name == 'manifest.json' else slot
            require(bool(name) and name == item.orig_filename and name not in ('.', '..') and
                    not any(c in '/\\:' or ord(c) < 32 or ord(c) == 127 for c in name) and
                    mode in (0, stat.S_IFREG) and not item.flag_bits & (1 | 32 | 64) and
                    item.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED),
                    'Nested OTA abnormal member')
            # Check BOTH directory sizes before opening either compressed member.
            # The unsigned manifest's own slot_size cannot increase this limit.
            require(0 < item.file_size <= limit, 'Nested OTA expanded size exceeds limit')
        with archive.open('manifest.json') as stream:
            manifest_bytes = stream.read(MAX_OTA_MANIFEST + 1)
        require(len(manifest_bytes) == archive.getinfo('manifest.json').file_size and
                len(manifest_bytes) <= MAX_OTA_MANIFEST, 'Nested OTA manifest read exceeds limit')
        manifest = json.loads(manifest_bytes, object_pairs_hook=_object)
        firmware = next(name for name in names if name != 'manifest.json')
        require(type(manifest) is dict and type(manifest.get('application')) is dict and
                manifest['application'].get('file') == firmware, 'Nested OTA firmware member mismatch')
        fixed = profile(chip, manifest.get('board'))
        require(manifest.get('layout') == fixed['layout'], 'Nested OTA family/layout mismatch')
        slot = ota.LAYOUT_PROFILES[(fixed['board'],fixed['layout'])]['slot_size']
        require(archive.getinfo(firmware).file_size <= slot,
                'Nested OTA expanded size exceeds exact family limit')
        with archive.open(firmware) as stream:
            content = stream.read(slot + 1)
        require(len(content) == archive.getinfo(firmware).file_size and len(content) <= slot,
                'Nested OTA firmware read exceeds fixed slot')
    return {'manifest.json': manifest_bytes, firmware: content}


def _read_base(path: Path) -> tuple[bytes, dict[str, bytes]]:
    """Read a bounded exact-chip or shared-family archive, never extract paths."""
    with path.open('rb') as stream:
        blob = stream.read(2097153)
    require(len(blob) <= 2097152, 'Oversize portable base')
    with zipfile.ZipFile(io.BytesIO(blob)) as archive:
        names = archive.namelist()
        require(len(names) == len(set(names)) and
                (set(names) == set(MEMBERS) or set(names) == set(FAMILY_MEMBERS)),
                'Duplicate, missing or extra base ZIP member')
        # Validate every directory entry before opening any member.
        for item in archive.infolist():
            mode = stat.S_IFMT(item.external_attr >> 16)
            require(item.filename == item.orig_filename and mode in (0, stat.S_IFREG) and
                    not item.flag_bits & (1 | 32 | 64) and
                    item.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED) and
                    0 < item.file_size <= MEMBER_LIMITS[item.filename], 'Invalid base member envelope')
        data = {}
        for item in archive.infolist():
            with archive.open(item) as stream:
                content = stream.read(MEMBER_LIMITS[item.filename] + 1)
            require(len(content) == item.file_size and len(content) <= MEMBER_LIMITS[item.filename],
                    'Invalid base member expanded size')
            data[item.filename] = content
    return blob, data


def inspect_base(path: Path, chip: str | None = None) -> dict:
    """Pure offline inspection. Returns evidence, never an execution manifest."""
    try:
        blob, data = _read_base(path)
        base = _json(data['base.json'])
        family = set(data) == set(FAMILY_MEMBERS)
        if family:
            require(set(base) == {'schema', 'family', 'chips', 'layout', 'files'} and
                    base['schema'] == 'inktag-gr-full-family-v1' and base['family'] == 'GR551x' and
                    base['chips'] == list(FAMILY_CHIPS), 'Unknown family base descriptor')
            chips, names = FAMILY_CHIPS, FAMILY_MEMBERS
        else:
            require(set(base) == {'schema', 'chip', 'layout', 'files'} and
                    base['schema'] == 'inktag-gr-full-base-v1' and base['chip'] in RAM,
                    'Unknown base descriptor')
            chips, names = (base['chip'],), MEMBERS
        require(chip is None or chip in chips, 'Exact chip/base/PCB mismatch')
        require(base['layout'] == RAM[chips[0]]['layout'] and
                type(base['files']) is dict and set(base['files']) == set(names[1:]) and
                all(base['files'][name] == sha(data[name]) for name in names[1:]),
                'Base cross-binding mismatch')
        # All ordinary App trust/layout/bounds/signature checks remain owned by
        # the maintained client. There is no unsigned/fixture fallback option.
        members = _bounded_app_members(data['app-ota.zip'], chips[0])
        with tempfile.TemporaryDirectory(prefix='inktag-gr-base-review-') as folder:
            application = Path(folder) / 'app-ota.zip'
            # Feed identical, bounded payload bytes in a stored ZIP: the normal
            # verifier must not re-expand the original untrusted compressed stream.
            with zipfile.ZipFile(application, 'w', compression=zipfile.ZIP_STORED) as archive:
                for name, content in members.items():
                    archive.writestr(name, content)
            package = ota.load_package(application)
        # Verify BOTH exact-part signatures even when selecting only one part.
        # Family aliases never supply package IDs, capacities or helper ABI.
        metadata = {part: _verify(data['factory-init-'+part+'.ikfi' if family else 'factory-init.ikfi'],
                    package, data['recovery.bin'], part) for part in chips}
    except (zipfile.BadZipFile, KeyError, TypeError, struct.error, RecursionError) as error:
        raise ValueError('Invalid portable base') from error
    selected = chip or (chips[0] if not family else None)
    result = {'schema': 'inktag-gr-factory-init-review-v1',
            'firmwareFamily': package.manifest['board'],
            'files': base['files'], 'baseSha256': sha(blob),
            'executable': False, 'portableExecutionQualified': False,
            'applicationDeltaBytes': 0, 'recoveryDeltaBytes': 0}
    if family:
        result.update(family='GR551x', chips=list(chips), metadataByChip=metadata)
    if selected:
        result.update(chip=selected, metadata=metadata[selected])
    return result


def verified_payloads(path: Path, chip: str | None = None) -> tuple[dict, dict[str, bytes]]:
    """Private executor input, re-bound to the exact already-verified bytes.

    This adds no verifier bypass or qualification switch. Ordinary inspection
    still never returns executable bytes. No SDK, ELF or private key needed.
    """
    review = inspect_base(path, chip)
    require('chip' in review, 'Select an exact chip for shared-family payload preparation')
    blob, data = _read_base(path)
    require(sha(blob) == review['baseSha256'], 'Base changed during preparation')
    members = _bounded_app_members(data['app-ota.zip'], review['chip'])
    manifest = _json(members['manifest.json'])
    signed = members[manifest['application']['file']]
    offset = PREFIX.size + len(canonical(review['metadata']))
    initializer = 'factory-init-'+review['chip']+'.ikfi' if 'family' in review else 'factory-init.ikfi'
    helper = data[initializer][offset:offset+review['metadata']['helperBytes']]
    require(sha(signed) == review['metadata']['signedAppSha256'] and
            sha(helper) == review['metadata']['helperSha256'], 'Prepared payload binding changed')
    return review, {'app': signed, 'recovery': data['recovery.bin'], 'helper': helper}
