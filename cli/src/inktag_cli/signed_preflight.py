"""Fixed production public-key OTA checks. Pure host code; no device or signer."""
from __future__ import annotations
import hashlib
import re
import struct
import zlib
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

DA_KEY = bytes.fromhex('04' + '4b7e9a9e6d38e0caf507cb25dfbf859f3753b97e5b7cd21bd20d156f07a14672' + 'd71b4e32a19307f91654a45d4ce165a4207bab50443aee65e6ea6cc02911bbfa')
NRF_KEY_LE = bytes.fromhex('0edf3d346f2be4a0a641930c3bba0f21fff2bf1913ed68d1a81b47a87e0bf9ae' + 'c2a2697fc77f10daba686704f3323fed849591ec9539b288ae5be98f221fe7d1')
ORDER = int('ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551',16)

def _signature(key, signature, message, *, little=False, low_s=False):
    if len(signature)!=64: raise ValueError('Missing publisher signature')
    endian='little' if little else 'big'
    r=int.from_bytes(signature[:32],endian);s=int.from_bytes(signature[32:],endian)
    if not 0<r<ORDER or not 0<s<(ORDER//2+1 if low_s else ORDER):
        raise ValueError('Invalid publisher signature encoding')
    try:
        key.verify(utils.encode_dss_signature(r,s),message,ec.ECDSA(hashes.SHA256()))
    except InvalidSignature as error: raise ValueError('OTA publisher signature verification failed') from error

def verify_dialog(image: bytes) -> dict:
    if not 160<len(image)<=64+0x17600 or image[:4]!=b'pQ\xaa\xff':
        raise ValueError('Wrong DA14585 native image/size')
    size,crc=struct.unpack_from('<II',image,4);payload=image[64:]
    if size!=len(payload) or zlib.crc32(payload)&0xffffffff!=crc:
        raise ValueError('DA14585 native size/CRC mismatch')
    raw,trailer=payload[:-96],payload[-96:]
    magic,version,algorithm,extent,raw_size,epoch,board,layout,verifier,release=struct.unpack('<IBBHIIIIII',trailer[:32])
    context=(int.from_bytes(b'DAXB','little'),int.from_bytes(b'DAV1','little'),int.from_bytes(b'DAK1','little'))
    if (magic,version,algorithm,extent)!=(0x31534b49,1,1,96) or raw_size!=len(raw) or not raw or (board,layout,verifier)!=context or epoch!=1:
        raise ValueError('DA14585 signed layout/epoch/context mismatch')
    if struct.unpack_from('<IIIIII',image,40)!=(0x31504b49,raw_size,epoch,board,layout,verifier):
        raise ValueError('DA14585 native/signed context mismatch')
    text=image[12:28].split(b'\0',1)[0].decode('ascii',errors='strict')
    if not re.fullmatch(r'\d+\.\d+\.\d+\.\d+',text):raise ValueError('DA14585 native version format')
    major,minor,patch,_=map(int,text.split('.'))
    if major*10000+minor*100+patch!=release:raise ValueError('DA14585 header/signed release mismatch')
    key=ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(),DA_KEY)
    _signature(key,trailer[32:],raw+trailer[:32],low_s=True)
    return dict(board='da14585_aesl',layout='DAV1',compatibilityEpoch=epoch,releaseVersion=release,version=text,publisherVerified=True)

def _fields(data: bytes) -> dict:
    if not isinstance(data,bytes) or len(data)>512:raise ValueError('Invalid Nordic init packet bound')
    at=0;result={}
    def uint():
        nonlocal at
        value=0
        for shift in range(0,35,7):
            if at>=len(data):raise ValueError('Truncated Nordic init varint')
            b=data[at];at+=1;value|=(b&127)<<shift
            if b<128:
                if value>0xffffffff:raise ValueError('Nordic uint32 overflow')
                return value
        raise ValueError('Invalid Nordic init varint')
    while at<len(data):
        tag=uint();field,wire=tag>>3,tag&7
        if not field or field in result or wire not in (0,2):raise ValueError('Ambiguous Nordic init fields')
        value=uint()
        if wire==2:
            if value>len(data)-at:raise ValueError('Truncated Nordic init field')
            value,at=data[at:at+value],at+value
        result[field]=value
    return result

def verify_nordic(firmware: bytes, dat: bytes) -> dict:
    try:
        packet=_fields(dat)
        if set(packet)!={2}:raise ValueError('Signed Nordic init required')
        signed=_fields(packet[2])
        if set(signed)!={1,2,3} or signed[2]!=0 or len(signed[3])!=64:raise ValueError('Nordic P256 signature required')
        command=_fields(signed[1])
        if set(command)!={1,2} or command[1]!=1:raise ValueError('Nordic INIT command required')
        init=_fields(command[2]);digest=_fields(init[8]);hw=init[2]
        profiles={52:('aesl22_nrf','N1V1',69632,b'\xa6\x02'),56:('nrf52832_priceetag','N3V1',163840,b'\x81\x02')}
        if hw not in profiles:raise ValueError('Unsupported Nordic HW/layout')
        board,layout,limit,sd=profiles[hw];encoded=init[1]
        if (set(init)-set(range(1,11)) or init.get(4,0)!=0 or init.get(5,0) or init.get(6,0) or init.get(9,0)
            or not 8<=len(firmware)<=limit or init[7]!=len(firmware) or init[3]!=sd
            or digest!={1:3,2:hashlib.sha256(firmware).digest()[::-1]}
            or type(encoded) is not int or encoded>>16!=1 or not encoded&65535
            or _fields(init[10])!={1:1,2:b''}):raise ValueError('Nordic signed HW/epoch/hash/size context mismatch')
        key=ec.EllipticCurvePublicNumbers(int.from_bytes(NRF_KEY_LE[:32],'little'),int.from_bytes(NRF_KEY_LE[32:],'little'),ec.SECP256R1()).public_key()
        _signature(key,signed[3],command[2],little=True)
        return dict(board=board,layout=layout,hwVersion=hw,compatibilityEpoch=encoded>>16,releaseVersion=encoded,publisherVerified=True)
    except (KeyError,TypeError,struct.error) as error:
        raise ValueError('Malformed Nordic signed init packet') from error
