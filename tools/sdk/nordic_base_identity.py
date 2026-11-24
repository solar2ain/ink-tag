"""Ordinary base ELF identity, layout and byte consistency; no image patcher."""
import argparse
import hashlib
import json
import runpy
from pathlib import Path
from goodix_code_elf import Elf

def compiled_profile(elf):
    """Read the explicit non-loadable build identity, never a folder inference."""
    header = elf.unpack('HHIIIIIHHHHHH', 16)
    if header[12] >= len(elf.sections):
        raise ValueError('Invalid Nordic section-name table')
    strings = elf.sections[header[12]]
    names = elf.slice(strings[4], strings[5])
    found = [s for s in elf.sections
             if names[s[0]:].split(b'\0', 1)[0] == b'.inktag_source_profile']
    if not found:
        return None  # Historical full Pro ELF; its existing strict gate remains.
    if len(found) != 1:
        raise ValueError('Ambiguous Nordic source-profile identity')
    note = found[0]
    if note[1] != 1 or note[2] & 2 or note[5] != 1 or any(
            s[1] < note[4]+note[5] and note[4] < s[1]+s[4] for s in elf.loads):
        raise ValueError('Source-profile identity must be one non-loadable byte')
    value = elf.slice(note[4], 1)
    if value not in (b'\0', b'\1'):
        raise ValueError('Invalid Nordic source-profile identity')
    return 'pro' if value == b'\1' else 'base'


def allocated_memory(elf):
    """Project file-backed SHF_ALLOC sections using their real PT_LOAD LMAs.

    Objcopy omits inter-section ELF padding. Nordic App fills those gaps FF,
    while Recovery uses zero; PT_LOAD file padding is not a BIN/HEX section.
    """
    memory = {}
    for section in elf.sections:
        if not section[2] & 2 or section[1] == 8 or not section[5]:
            continue
        address, offset, size = section[3:6]
        matches = [s for s in elf.loads if s[2] <= address and address+size <= s[2]+s[4]
                   and s[1]+address-s[2] == offset]
        if len(matches) != 1:
            raise ValueError('Allocated section lacks a unique Nordic load mapping')
        physical = matches[0][3]+address-matches[0][2]
        for index, value in enumerate(elf.slice(offset, size)):
            at = physical+index
            if at in memory:
                raise ValueError('Overlapping Nordic allocated sections')
            memory[at] = value
    return memory


def verify(raw, role, binary=None, source_profile='base'):
    if source_profile != 'base' or role not in ('app', 'recovery'):
        raise ValueError('Explicit Nordic base role/profile required')
    elf = Elf(raw)
    if compiled_profile(elf) != 'base':
        raise ValueError('base source-profile ELF identity required')
    forbidden = {'parse_and_verify', 'verify_signature', 'apply_verified_record',
                 'verified_features', 'k_inktag_activation_product_public_key'}
    prefixes = ('inktag_code_', '__inktag_guard_', 'inktag_nrf_code_sha256',
                'inktag_nrf_verified_app_size', 'inktag_nrf_p224_',
                'inktag_nrf_findmy_', 'inktag_nfc_', 'nrf_clock_partial_',
                'uECC_', 'nrf_crypto_')
    if any(s['name'].startswith('__inktag_guard_') or (s['bytes'] and
           (s['name'].split('.')[0] in forbidden or s['name'].startswith(prefixes)))
           for s in elf.symbols):
        raise ValueError('Private implementation remains in base ELF')
    base, end = (0x26000, 0x4e000) if role == 'app' else (0x58000, 0x5e000)
    memory = allocated_memory(elf)
    image_memory = {at:v for at,v in memory.items() if base <= at < end}
    extras = {at:v for at,v in memory.items() if at not in image_memory}
    allowed = set(range(0x10001014,0x1000101c)) if role == 'recovery' else set()
    if set(extras) != allowed or not image_memory or min(image_memory) != base:
        raise ValueError('base native image placement rejected')
    if extras and (int.from_bytes(bytes(extras[a] for a in range(0x10001014,0x10001018)),'little') != 0x58000 or
                   int.from_bytes(bytes(extras[a] for a in range(0x10001018,0x1000101c)),'little') != 0x5e000):
        raise ValueError('base Recovery UICR selectors rejected')
    image = bytearray([255 if role == 'app' else 0]) * (max(image_memory)-base+1)
    for at,value in image_memory.items():image[at-base]=value
    image = bytes(image)
    stack, reset = int.from_bytes(image[:4],'little'), int.from_bytes(image[4:8],'little')
    if (len(image)<8 or stack & 7 or not 0x20004000 < stack <= 0x20010000 or
            not reset & 1 or not base <= reset & ~1 < base+len(image)):
        raise ValueError('base native stack/reset vectors rejected')
    if binary is not None and binary != image:
        raise ValueError('base ELF/BIN mismatch')
    if role == 'recovery':
        policy = elf.symbol('g_inktag_nrf_recovery_protocol')
        if policy['bytes'] != 1 or elf.slice(elf.offset(policy['address'],1),1) != b'\xe1':
            raise ValueError('base Recovery CRC-only E1 policy required')
    return dict(role=role,buildTarget='nrf52832_priceetag',sourceProfile='base',
                compiledSourceProfile='base',rawBytes=len(image),
                rawSha256=hashlib.sha256(image).hexdigest(),privateProtection=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf',type=Path,required=True)
    parser.add_argument('--role',choices=('app','recovery'),required=True)
    parser.add_argument('--source-profile',choices=('base',),required=True)
    parser.add_argument('--bin',type=Path)
    parser.add_argument('--hex',type=Path)
    args=parser.parse_args();raw=args.elf.read_bytes()
    report=verify(raw,args.role,args.bin.read_bytes() if args.bin else None,args.source_profile)
    if args.hex:
        actual=runpy.run_path(str(Path(__file__).with_name('merge-ihex.py')))['read_hex'](args.hex)
        if actual != allocated_memory(Elf(raw)):raise ValueError('base ELF/HEX mismatch')
    args.elf.with_suffix('.base-identity.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('Nordic base identity/layout verified:',args.role,report['rawBytes'],'bytes')


if __name__ == '__main__':main()
