"""Fixed RAM-only HS identity read; no Flash mutations or new MCU firmware.

Uses only read-ID and read-nodma from the existing pinned vendor loader. In
particular never calls Init/unlock, erase, program or write-status. Assembly
source is tools/sdk/om-full-identity.S; public users need no compiler or SDK.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct

LOADER_SHA='9a53c01b9363371e8eff44a7a9de80e2942e96025aede3ade8053875ab6ad640'
CODE_SHA='a25f95c0742d591abb460281d4f213d9c1a0d1c557ab044af1fd7381c22d91a4'
TRAMPOLINE=bytes.fromhex('72b61d4c4ff0a2461c4d30460021a847606030460021a847a060000c1027852808d0eb2806d0c82804d00827e02801d068281fd1e76086b04ff096400090002001900290049028208df80800adf8107004f1100003900a4d304600216a46a84704f120000390304600216a46a84706b00448206000befde70030012035140020d92700204f494431')
TRAMPOLINE_SHA='cd3063fc7f7095d3aa9a5929e1af94ce150275a135a5a87b8d3aca32f1d6c0c7'
MAGIC=0x3144494f

def loader_code(blob):
    if hashlib.sha256(blob).hexdigest()!=LOADER_SHA:
        raise ValueError('HS unit-name reader requires the maintained vendor read-loader bytes')
    if blob[:7]!=b'\x7fELF\x01\x01\x01':raise ValueError('unexpected vendor loader format')
    shoff=struct.unpack_from('<I',blob,32)[0]
    entsize,count,names=struct.unpack_from('<HHH',blob,46)
    if entsize!=40 or not 0<names<count<=128 or shoff+count*40>len(blob):
        raise ValueError('invalid bounded vendor section directory')
    sections=[struct.unpack_from('<10I',blob,shoff+i*40) for i in range(count)]
    string=sections[names];strings=blob[string[4]:string[4]+string[5]]
    candidates=[s for s in sections if strings[s[0]:].split(b'\0',1)[0]==b'PrgCode']
    if len(candidates)!=1:raise ValueError('ambiguous read-code section')
    s=candidates[0];code=blob[s[4]:s[4]+s[5]]
    if s[1]!=1 or s[3]!=0x20000000 or len(code)!=12572 or hashlib.sha256(code).hexdigest()!=CODE_SHA:
        raise ValueError('vendor read-code/RAM binding changed')
    return code

def address_from_result(result):
    if type(result) is not bytes or len(result)!=64:raise ValueError('incomplete HS identity result')
    magic,a,b,n=struct.unpack_from('<4I',result)
    if magic!=MAGIC or a!=b:raise ValueError('HS identity read did not complete consistently')
    expected=16 if a>>16 in (0x85,0xeb,0xc8) else 8 if a>>16 in (0xe0,0x68) else 0
    if not expected or n!=expected:raise ValueError('unsupported HS immutable UID format')
    first=result[16:16+n];second=result[32:32+n]
    if first!=second or first in (bytes(n),b'\xff'*n):raise ValueError('invalid/unstable HS immutable UID')
    if result[16+n:32]!=bytes(16-n) or result[32+n:48]!=bytes(16-n):
        raise ValueError('unowned HS identity result bytes')
    # Byte-for-byte rule of inktag_om_hsela_default_address, including 8-byte
    # manufacturer UIDs padded to the same 16-byte root payload.
    return hashlib.sha256(b'HS'+first+bytes(16-n)).digest()[:5]+b'\xd2'

def capacity_from_result(result):
    """Repeated actual main-Flash ID, not debugger window or Config capacity."""
    address_from_result(result) # Complete repeated ID/UID grammar first.
    jedec=struct.unpack_from('<I',result,4)[0]
    exponent=jedec&255
    if exponent!=20:
        raise ValueError('selected OM backend requires an actual 1 MiB main Flash')
    return 1<<exponent

def stage(directory,loader):
    root=Path(directory)
    code=loader_code(Path(loader).read_bytes())
    if hashlib.sha256(TRAMPOLINE).hexdigest()!=TRAMPOLINE_SHA:raise ValueError('fixed identity trampoline changed')
    request=bytes(48)+os.urandom(16)
    for name,body in (('identity-loader-RAM.bin',code),('identity-trampoline-RAM.bin',TRAMPOLINE),('identity-request.bin',request)):
        with (root/name).open('xb') as stream:stream.write(body)

def verify(directory):
    root=Path(directory);request=(root/'identity-request.bin').read_bytes();result=(root/'identity.bin').read_bytes()
    if len(request)!=64 or len(result)!=64 or request[48:]!=result[48:]:
        raise ValueError('HS identity read belongs to another operation')
    address=address_from_result(result)
    return {'identity_result_sha256':hashlib.sha256(result).hexdigest(),
        'address_hex':address.hex(),'flash_written':False,'hardware_regression_passed':False}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=('stage','verify'));p.add_argument('--directory',required=True);p.add_argument('--loader')
    a=p.parse_args()
    if a.action=='stage':
        if not a.loader:p.error('stage needs installed vendor loader')
        stage(a.directory,a.loader)
    else:print(json.dumps(verify(a.directory)))
if __name__=='__main__':main()
