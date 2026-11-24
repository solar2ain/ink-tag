"""Ordinary InkTag native ELF/layout and CRC checks; never modifies ELF bytes."""
import argparse
import json
from pathlib import Path
import zlib
from elftools.elf.elffile import ELFFile


def verify(path, role, binary=None, seal=False, source_profile='base'):
    if source_profile!='base' or seal or role not in ('app','recovery'):
        raise ValueError('Explicit unmodified InkTag native role required')
    base,capacity=(0x1000,0x3f000) if role=='app' else (0x40000,0x11000)
    with Path(path).open('rb') as f:
        elf=ELFFile(f)
        if elf.elfclass!=32 or not elf.little_endian or elf.header['e_machine']!=0x8800:
            raise ValueError('Expected native little-endian ELF32')
        symbols={s.name:s for s in elf.get_section_by_name('.symtab').iter_symbols()}
        profile=symbols.get('inktag_tlsr_source_profile')
        if profile is None or profile['st_shndx']!='SHN_ABS' or profile['st_value']!=0:
            raise ValueError('Native source profile differs from InkTag')
        prefix=symbols['__RAM_RETENTION_SIZE']['st_value']
        if elf.get_section_by_name('.text')['sh_addr']!=base+prefix:
            raise ValueError('Native physical entry mapping mismatch')
        if role=='recovery':
            entries=[s for name,s in symbols.items() if name=='enter_native_ram' or name.startswith('enter_native_ram.')]
            ram=elf.get_section_by_name('.ram_code')
            if not entries or any(not ram['sh_addr']<=(s['st_value'] & ~1)<(s['st_value'] & ~1)+s['st_size']<=ram['sh_addr']+ram['sh_size'] for s in entries):
                raise ValueError('Native RAM handoff is not placed in SRAM')
        if binary is not None:
            raw=Path(binary).read_bytes()
            if len(raw)<36 or raw[8:12]!=b'KNLT' or raw[14:16]!=b'\x88\0':
                raise ValueError('Native header invalid')
            declared=int.from_bytes(raw[24:28],'little')
            ram=int.from_bytes(raw[12:14],'little')*16
            if declared+4!=len(raw) or not 32<=ram<=min(0x8000,declared) or len(raw)>capacity:
                raise ValueError('Native body or RAM-prefix bounds invalid')
            image=bytearray(declared);end=0
            for segment in elf.iter_segments():
                if segment['p_type']!='PT_LOAD' or not segment['p_filesz']:continue
                at=segment['p_paddr'];last=at+segment['p_filesz']
                if at<0 or last>declared:raise ValueError('Initializer outside native body')
                image[at:last]=segment.data();end=max(end,last)
            if end!=declared or bytes(image)!=raw[:declared]:
                raise ValueError('ELF/native loadable bytes differ')
            if zlib.crc32(image)!=int.from_bytes(raw[-4:],'big'):
                raise ValueError('Native CRC mismatch')
    return dict(role=role,sourceProfile='base',integrity='native-crc',elfBytesUnmodified=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--role',choices=['app','recovery'],required=True)
    p.add_argument('--elf',type=Path);p.add_argument('--bin',type=Path)
    p.add_argument('--source-profile',choices=['base'],default='base')
    p.add_argument('--linker-input',type=Path);p.add_argument('--output',type=Path)
    a=p.parse_args()
    if a.linker_input:
        a.output.write_bytes(a.linker_input.read_bytes())
    else:print(json.dumps(verify(a.elf,a.role,a.bin,source_profile=a.source_profile)))
