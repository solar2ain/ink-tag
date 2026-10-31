"""Readback prediction of the maintained OM662XX loader, NOT an MBR writer.

ROM MBR1000/2000 differs from SDK runtime MBR80000/81000. This pure host
model is shared by the historical control and offline writer. It does not
qualify a physical loader, cold boot or any unknown vendor MBR.
"""
import binascii
import struct

def predict_rom_metadata(before,after,code):
    if len(before)!=0x100000 or len(after)!=len(before):
        raise ValueError('exact one MiB snapshots required')
    if before[0x1182]!=0x60 or before[0x11fe:0x1200]!=b'\x55\xaa':
        raise ValueError('Unknown ROM boot MBR; do not initialize or repair it')
    pages=sorted({address&~4095 for address in code})
    if not pages or pages[0]!=0x404000 or any(not 0x404000<=p<0x478000 for p in pages):
        raise ValueError('Code must start with fixed Recovery and stay below fonts')
    stream=b''.join(bytes(code.get(page+i,255) for i in range(4096)) for page in pages)
    crc=binascii.crc_hqx(stream,0)
    expected=bytearray(after)
    entry=struct.pack('<HII',crc,4,len(stream))
    receipt={'romBootMbrProgrammedStreamBytes':len(stream),'romBootMbrCrc16':hex(crc)}
    # The pinned SDK mbr_write_part returns without a swap when all existing
    # entry fields match (ELF 0x20000964..0x2000097C). In particular, it leaves
    # the old, invalidated backup page untouched on an identical reflash.
    if before[0x1184:0x118e]==entry:
        return bytes(expected),{**receipt,'romBootMbrUpdated':False}
    expected[0x2000:0x2200]=before[0x1000:0x1200]
    expected[0x21fe:0x2200]=b'\0\0'
    expected[0x1184:0x118e]=entry
    return bytes(expected),{**receipt,'romBootMbrUpdated':True}
