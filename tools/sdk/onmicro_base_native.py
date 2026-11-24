"""Ordinary base native format/address/ELF binding checks, without sealing."""
import hashlib
import struct
from onmicro_base_elf import Elf

BASES = {'app': (0x42a000, 319352), 'recovery': (0x404000, 155648)}


def verify(raw, role, binary=None, profile='base'):
    if profile != 'base' or role not in BASES:
        raise ValueError('Explicit base App/Recovery image required')
    elf = Elf(raw)
    base, limit = BASES[role]
    start, image = elf.image()
    if start != base or not 0x10c <= len(image) <= limit:
        raise ValueError('Native load extent is outside its fixed partition')
    if binary is not None and image != binary:
        raise ValueError('ELF/BIN loadable bytes differ')
    for segment in elf.loads:
        _, _, address, physical, size, memory, _, _ = segment
        if not base <= physical < physical + size <= base + len(image):
            raise ValueError('Native load segment escapes the image')
        if address != physical and not (
                0x00200000 <= address < address + memory <= 0x00201fc0 or
                0x20000000 <= address < address + memory <= 0x20014000):
            raise ValueError('Native RAM load/copy segment exceeds physical RAM')
    symbols = {symbol['name']: symbol for symbol in elf.symbols}
    forbidden = ('inktag_code_measure', 'inktag_code_digests', 'inktag_code_result',
                 'admission_code_difference', 'verified_image_bytes',
                 'k_inktag_activation_product_public_key', 'parse_and_verify',
                 'verify_signature', 'apply_verified_record', 'verified_features')
    if any(name.split('.')[0] in forbidden for name in symbols):
        raise ValueError('Private protection/activation implementation in base image')
    # Ordinary startup copy-table admission remains real, without a private
    # measurement region or a replacement startup/heap/framebuffer layout.
    table = elf.symbol('__copy_table_start__')['address']
    source, destination, words = struct.unpack(
        '<III', elf.slice(elf.offset(table, 12), 12))
    if not (base <= source < source + words * 4 <= base + len(image) and
            0x00200000 <= destination < destination + words * 4 <= 0x00201fc0):
        raise ValueError('Startup RAM copy table exceeds native load/RAM bounds')
    stack, reset = struct.unpack_from('<II', image)
    if not (0x20000000 < stack <= 0x20013ea0 and not stack & 7 and
            reset & 1 and base <= (reset & ~1) < base + len(image)):
        raise ValueError('Native vectors exceed this image or physical RAM')
    if role == 'app':
        magic, entry, marker = struct.unpack_from('<III', image, 0x100)
        if magic != 0x314c4d4f or entry != reset or marker != 0xb007a55a:
            raise ValueError('Native App independent launch record mismatch')
    return dict(role=role, sourceProfile='base', protection=False,
                rawBytes=len(image), rawSha256=hashlib.sha256(image).hexdigest(),
                ordinaryNativeChecks=True, startupCopyBytes=words * 4,
                regions=[], measuredBytes=0)
