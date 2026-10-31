"""Operation-local last-page fix for the pinned OM no-MBR loader.

No device IO and no status-register/unlock change. Keep the original SDK and
installed pack intact. Only range erase and program capacity clipping changes;
chip erase, CPFT exclusions, Verify, Init and UnInit remain byte-identical.
"""
import argparse
import hashlib
import json
from pathlib import Path

SOURCE_SHA256 = '9a53c01b9363371e8eff44a7a9de80e2942e96025aede3ade8053875ab6ad640'
DEVICE = 'OM662XX_NOMBR_INKTAG_LAST_PAGE'
# Hash-pinned ELF PT_LOAD maps file 0xA0 to RAM 0x20000000.
# Remove only `capacity -= 4096` in sf_erase(range) and sf_write.
PATCHES = ((0x19C8, bytes.fromhex('a1f58051')), (0x1FE2, bytes.fromhex('a3f58053')))
NOP = bytes.fromhex('aff30080')  # Thumb-2 nop.w


def patched_loader(raw):
    if hashlib.sha256(raw).hexdigest() != SOURCE_SHA256:
        raise ValueError('unsupported OM loader; original bytes must match the pinned SDK')
    result = bytearray(raw)
    for address, instruction in PATCHES:
        at = 0xA0 + address
        if result[at:at+4] != instruction:
            raise ValueError('OM loader instruction binding mismatch')
        result[at:at+4] = NOP
    return bytes(result)


def prepare(source, output):
    raw = Path(source).read_bytes()
    fixed = patched_loader(raw)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    loader = output / 'OM662XX_nombr_last_page.elf'
    loader.write_bytes(fixed)
    xml = output / 'JLinkDevices.xml'
    xml.write_text(
        '<DataBase><Device><ChipInfo Vendor="Onmicro" Name="'+DEVICE+'" '
        'Core="JLINK_CORE_CORTEX_M4" WorkRAMAddr="0x20000000" WorkRAMSize="0x00010000" />'
        '<FlashBankInfo Name="SPI Flash" BaseAddr="0x00400000" MaxSize="0x00100000" '
        'Loader="OM662XX_nombr_last_page.elf" LoaderType="FLASH_ALGO_TYPE_OPEN" AlwaysPresent="1"/>'
        '</Device></DataBase>\n', encoding='ascii')
    receipt = dict(device=DEVICE, directory=str(output.resolve()),
                   sourceSha256=SOURCE_SHA256, loaderSha256=hashlib.sha256(fixed).hexdigest(),
                   instructionAddresses=[hex(0x20000000+a) for a, _ in PATCHES],
                   statusRegisterCodeChanged=False, cpftCodeChanged=False)
    (output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    return receipt


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', required=True)
    p.add_argument('--out', required=True)
    a = p.parse_args()
    print(json.dumps(prepare(a.source, a.out)))
