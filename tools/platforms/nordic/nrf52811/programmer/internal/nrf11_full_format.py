"""Pure nRF52811 Full format checks shared by source builds and offline programmers.

No CLI, transport, SDK or private producer is imported. Self-built CRC-only
and legacy optional containers permit unsigned Apps. Any signature supplied
to this host verifier is checked independently of the declared Recovery policy.
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
import zlib
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

HEX = runpy.run_path(str(Path(__file__).with_name('validate-full.py')))

PUBLIC_KEY_LE = bytes.fromhex(
    '0edf3d346f2be4a0a641930c3bba0f21fff2bf1913ed68d1a81b47a87e0bf9ae'
    'c2a2697fc77f10daba686704f3323fed849591ec9539b288ae5be98f221fe7d1')


MEMBERS = {'base.json': 4096, 'full.hex': 1500000, 'app-ota.zip': 1048576, 'gui-assets.bin': 12288}


FACTORY_PIN_MARKER=b'N11PINCFG1\0'


SELF_BUILT_CONTAINER = 'inktag-nrf52811-self-built-full-v1'


SELF_BUILT_MIN_TOOLS = '0.2.1.2'
LEGACY_SELF_BUILT_MIN_TOOLS = '0.2.1.0'


def sha(data):
    return hashlib.sha256(data).hexdigest()


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


def ota_app(ota_data, allow_unsigned=False, public_key=None):
    files = read_archive(ota_data)
    manifest = checked_json(files['manifest.json'])
    if type(manifest) is not dict or set(manifest) != {'manifest'} or type(manifest['manifest']) is not dict:
        raise ValueError('Invalid Nordic OTA manifest')
    root = manifest['manifest']
    if set(root) != {'application'}:
        raise ValueError('Nordic OTA must be application-only')
    item = root['application']
    if type(item) is not dict or set(item) != {'bin_file', 'dat_file'} or any(type(x) is not str for x in item.values()):
        raise ValueError('Invalid Nordic application manifest')
    bn, dn = item['bin_file'], item['dat_file']
    if bn == dn or not bn.endswith('.bin') or not dn.endswith('.dat') or set(files) != {'manifest.json', bn, dn}:
        raise ValueError('Unknown Nordic OTA members')
    app, packet = files[bn], fields(files[dn])
    signed = None
    if set(packet) == {2}:
        signed = fields(packet[2])
        if set(signed) != {1, 2, 3} or signed[2] != 0 or len(signed[3]) != 64:
            raise ValueError('ECDSA-P256 Nordic init required')
        command = fields(signed[1])
    elif allow_unsigned and set(packet) == {1}:
        command = fields(packet[1])
    else:
        raise ValueError('Signed Nordic init required')
    if set(command) != {1, 2} or command[1] != 1:
        raise ValueError('INIT command required')
    init = fields(command[2]); digest = fields(init[8])
    if (set(init) - set(range(1, 11)) or init.get(2) != 52 or init.get(4, 0) != 0
            or init.get(5, 0) or init.get(6, 0) or init.get(7) != len(app)
            or not 8 <= len(app) <= 69632 or init.get(9, 0)
            or init.get(3) != b'\xa6\x02' or set(digest) != {1, 2}
            or digest[1] != 3 or digest[2] != hashlib.sha256(app).digest()[::-1]
            or type(init.get(1)) is not int or not 0 <= init[1] <= 0xffffffff or not init[1] & 65535):
        raise ValueError('Nordic App HW52/epoch/hash/size context mismatch')
    validation = fields(init[10])
    if validation != {1: 1, 2: b''}:
        raise ValueError('Nordic generated-CRC boot validation required')
    if signed is not None:
        trust_key = PUBLIC_KEY_LE if public_key is None else public_key
        raw = signed[3]
        key = ec.EllipticCurvePublicNumbers(int.from_bytes(trust_key[:32], 'little'),
            int.from_bytes(trust_key[32:], 'little'), ec.SECP256R1()).public_key()
        try:
            key.verify(utils.encode_dss_signature(int.from_bytes(raw[:32], 'little'),
                       int.from_bytes(raw[32:], 'little')), command[2], ec.ECDSA(hashes.SHA256()))
        except (InvalidSignature, ValueError) as error:
            raise ValueError('Nordic App signature verification failed') from error
    return app, init[1], signed is not None


def signed_app(ota_data, public_key=None):
    app, version, _ = ota_app(ota_data, public_key=public_key)
    return app, version


def checked_json(data):
    def pairs(items):
        result={}
        for key,value in items:
            if key in result:raise ValueError('Duplicate JSON member')
            result[key]=value
        return result
    return json.loads(data,object_pairs_hook=pairs)


def verify_components(full_data,ota_data,assets,allow_unsigned=False,public_key=None):
    if not 0<len(full_data)<=1500000:raise ValueError('Full image size invalid')
    with tempfile.TemporaryDirectory(prefix='nrf11-verify-') as temporary:
        p=Path(temporary)/'full.hex';p.write_bytes(full_data)
        memory=HEX['parse_hex'](p)
    HEX['validate_assets'](assets)
    if any(a>=0x30000 for a in memory):raise ValueError('Full must not supply UICR/FICR')
    size,sd_size=HEX['validate'](memory,assets)
    app,version,signature_verified=ota_app(ota_data,allow_unsigned,public_key=public_key)
    if version>>16<1 or app.count(FACTORY_PIN_MARKER)!=1:raise ValueError('N1P1/N1R1 requires validated pin-contract App epoch1 or higher')
    image=bytes(memory.get(a,255) for a in range(0x30000))
    if (len(app)!=size or image[0x19000:0x19000+size]!=app or
        struct.unpack_from('<I',image,0x2f008)[0]!=version or struct.unpack_from('<I',image,0x2f00c)[0]!=0):
        raise ValueError('Full/OTA application and version disagree')
    end=max(a for a in memory if 0x2a000<=a<0x2e000)+1
    if not 0<end-0x2a000<=16384:raise ValueError('Recovery outside 16KiB')
    return memory,dict(appBytes=size,appLimit=69632,appHeadroom=69632-size,
        appSha256=sha(app),appCrc32=f'{zlib.crc32(app)&0xffffffff:08X}',
        recoveryBytes=end-0x2a000,recoveryLimit=16384,recoveryHeadroom=0x2e000-end,
        recoverySha256=sha(image[0x2a000:end]),version=version,compatibilityEpoch=version>>16,
        signatureVerified=signature_verified,recoveryTrust='hash integrity only; no separate Recovery publisher signature',
        hardwareTested=False,factoryDisplayPins=app.count(FACTORY_PIN_MARKER)==1)


def verify_bundle(path,public_key=None):
    path=Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size>2097152:raise ValueError('Invalid Full ZIP')
    data=read_archive(path.read_bytes(),MEMBERS)
    manifest=checked_json(data['base.json'])
    expected=dict(format_version=1,mcu='nrf52811',firmware_family='aesl22_nrf',
        backend_ids=['nrf52811_asel22'],hw_version=52,config_schema=1,
        baseline='inktag-release-v1',layout_version=1,layout_marker='N1V1',factory_format='N1P1',user_format='N1R1')
    self_built = manifest.get('container_kind') == SELF_BUILT_CONTAINER if type(manifest) is dict else False
    if self_built:
        policy = manifest.get('recovery_signature_policy')
        if policy not in ('crc-only', 'optional', 'required'):
            raise ValueError('Invalid self-built Recovery signature policy')
        minimum_tools = SELF_BUILT_MIN_TOOLS if policy == 'crc-only' else LEGACY_SELF_BUILT_MIN_TOOLS
        HEX['validate_assets'](data['gui-assets.bin'])
        expected.update(container_kind=SELF_BUILT_CONTAINER, recovery_signature_policy=policy,
                        minimum_tools_version=minimum_tools,
                        app_gui_crc32=manifest.get('app_gui_crc32'))
        binding = manifest.get('app_gui_crc32')
        if type(binding) is not int or not 0 <= binding <= 0xffffffff or binding != struct.unpack_from('<I', data['gui-assets.bin'], 12)[0]:
            raise ValueError('Self-built Full GUI binding mismatch')
    if type(manifest) is not dict or set(manifest)!=set(expected)|{'images','compatibility_epoch'} or any(type(manifest[k]) is not type(v) or manifest[k]!=v for k,v in expected.items()):
        raise ValueError('Wrong nRF52811 Full format/identity')
    if manifest['images']!={n:dict(bytes=len(v),sha256=sha(v)) for n,v in data.items() if n!='base.json'}:
        raise ValueError('Full member binding mismatch')
    memory,review=verify_components(data['full.hex'],data['app-ota.zip'],data['gui-assets.bin'],
                                    allow_unsigned=self_built and policy in ('crc-only', 'optional'),public_key=public_key)
    if type(manifest['compatibility_epoch']) is not int or manifest['compatibility_epoch']!=review['compatibilityEpoch']:
        raise ValueError('Full epoch differs from native DAT')
    if self_built and review['compatibilityEpoch'] != 1:
        raise ValueError('Self-built Full requires the current epoch1 contract')
    review.update(packageSha256=sha(path.read_bytes()),packageVerified=True)
    if self_built:
        review.update(selfBuilt=True,containerKind=SELF_BUILT_CONTAINER,
                      recoverySignaturePolicy=policy,minimumToolsVersion=minimum_tools,
                      recoveryPolicySource='self-built-manifest',
                      publisherVerified=False,appSignatureVerified=review['signatureVerified'],
                      publisherTrust='App signature verified; self-built Recovery is not publisher-authenticated' if review['signatureVerified'] else
                                     'UNSIGNED: integrity only; publisher is not authenticated')
        if policy == 'crc-only':
            review.update(recoveryVerificationPolicy='crc-sha256',
                          declaredRecoveryProtocol=0xe1)
    return memory,data,review


def write_hex(path,memory):
    def record(kind,address,data):
        raw=bytes((len(data),address>>8,address&255,kind))+data
        return ':'+raw.hex().upper()+f'{(-sum(raw))&255:02X}'
    rows=[];upper=None;addresses=sorted(memory);i=0
    while i<len(addresses):
        start=addresses[i];high=start>>16
        if high!=upper:rows.append(record(4,0,high.to_bytes(2,'big')));upper=high
        chunk=bytearray()
        while i<len(addresses) and len(chunk)<16 and addresses[i]==start+len(chunk) and addresses[i]>>16==high:
            chunk.append(memory[addresses[i]]);i+=1
        rows.append(record(0,start&65535,bytes(chunk)))
    rows.append(':00000001FF')
    with Path(path).open('x',encoding='ascii',newline='\n') as stream:stream.write('\n'.join(rows)+'\n')



def read_archive(data, limits=None):
    """Read bounded members in memory; never extract archive-controlled paths."""
    if type(data) is not bytes or not 0 < len(data) <= 2097152:
        raise ValueError('Invalid Full/OTA ZIP size')
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            entries = archive.infolist()
            names = [e.filename for e in entries]
            if len(entries) != (len(limits) if limits else 3) or len(set(n.casefold() for n in names)) != len(entries):
                raise ValueError('ZIP member count or uniqueness invalid')
            if limits is not None and set(names) != set(limits):
                raise ValueError('Full ZIP requires exactly the declared Full members')
            result = {}
            for e in entries:
                limit = (limits[e.filename] if limits is not None else
                         4096 if e.filename == 'manifest.json' else
                         512 if e.filename.endswith('.dat') else 69632)
                if (e.flag_bits & 1 or e.is_dir() or stat.S_ISLNK(e.external_attr >> 16)
                    or '/' in e.filename or '\\' in e.filename or ':' in e.filename
                    or e.filename in ('.','..') or len(e.filename)>128
                    or e.compress_type not in (zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED)
                    or not 0 < e.file_size <= limit):
                    raise ValueError('Invalid/encrypted/oversized ZIP member')
                with archive.open(e) as stream:
                    value = stream.read(limit+1)
                if len(value) != e.file_size or len(value)>limit:
                    raise ValueError('ZIP inflated size mismatch')
                result[e.filename] = value
            return result
    except (zipfile.BadZipFile, RuntimeError, KeyError, NotImplementedError) as error:
        raise ValueError('Invalid Full/OTA ZIP') from error

