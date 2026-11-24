"""Execute CRC-only OTA and the actual resume CRC reader without crypto SDKs."""
from pathlib import Path
import runpy
import struct
import subprocess
import zlib

import pytest

ROOT = Path(__file__).resolve().parents[1]
OTA = runpy.run_path(str(ROOT/'build-support/build/package_ota.py'))['package']


def linux(path):
    p = Path(path).resolve().as_posix()
    return '/mnt/'+p[0].lower()+p[2:] if p[1:3] == ':/' else p


@pytest.fixture(scope='module', params=(False, True), ids=('normal', 'asan-ubsan'))
def native(request, tmp_path_factory):
    out = tmp_path_factory.mktemp('crc-only')
    for name in ('sha2.h', 'uECC.h', 'datasheet.h', 'rwip.h', 'arch_wdg.h'):
        (out/name).write_text('#error publisher-verification SDK must not be needed\n')
    source = (ROOT/'compat/dialog/src/inktag_da_recovery.c').read_text()
    crc = source[source.index('static uint32_t read_u32_le('):source.index('static bool recovery_markers(')]
    harness = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "inktag_da_ota_security.h"
#include "inktag_da_recovery.h"
#include "inktag_da_ota_policy.h"
#include "spi_flash.h"
static uint8_t image[95808];
static unsigned size,arms,releases,reads;
static int mode;
#define INKTAG_DA_RECOVERY_WDG_RELOAD() ((void)0)
static bool flash_begin(void) { return mode!=5; }
static void flash_end(void) {}
void inktag_da_recovery_service_ble(void) {}
uint16_t inktag_da_health_last_operation_voltage_mv(bool *valid) { *valid=true;return 3300; }
int spi_flash_release_from_power_down(void) { return 0; }
int spi_flash_read_data(uint8_t *out,uint32_t address,uint32_t length,uint32_t *actual) {
    *actual=0;++reads;
    if(address<0x4000 || address-0x4000>size || length>size-(address-0x4000))return 1;
    memcpy(out,image+address-0x4000,length);
    *actual=(mode==1 && address>=0x4040)?length-1:length;
    return 0;
}
inktag_recovery_result_t inktag_da_recovery_check_epoch(uint32_t epoch) {
    return epoch<1?INKTAG_RECOVERY_ROLLBACK_ERROR:INKTAG_RECOVERY_OK;
}
inktag_recovery_result_t inktag_da_recovery_arm_epoch(uint32_t epoch) {
    assert(epoch>=1);++arms;return mode==2?INKTAG_RECOVERY_IO_ERROR:INKTAG_RECOVERY_OK;
}
bool inktag_da_recovery_release_request(void) { ++releases;return mode!=3; }
''' + crc + r'''
int main(int argc,char **argv) {
    assert(argc==3);mode=atoi(argv[2]);
    FILE *f=fopen(argv[1],"rb");assert(f);size=fread(image,1,sizeof image,f);fclose(f);
    bool pre=inktag_da_ota_preflight(image,64);
    if(mode==4)image[60]^=1;
    bool accepted=inktag_da_ota_verify_and_arm();
    printf("%u %u %u %u %u\n",pre,accepted,arms,releases,reads);
    return 0;
}
'''
    src = out/'harness.c'
    src.write_text(harness)
    binary = out/'check'
    flags = ['-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-DINKTAG_DA_FIXED_RECOVERY=1',
        '-I'+linux(out), '-Itests/mocks/dialog', '-Icompat/dialog/src',
        '-Icompat/common/ota', '-Icompat/common/security', '-Icompat/common/power']
    if request.param:
        flags += ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    cmd = ['wsl', '--cd', linux(ROOT), '--exec', 'gcc', *flags, linux(src),
        'compat/dialog/src/inktag_da_ota_security.c', '-o', linux(binary)]
    subprocess.run(cmd, check=True, capture_output=True, text=True)
    names = subprocess.check_output(['wsl', '--exec', 'nm', linux(binary)], text=True)
    assert not any(s in names for s in ('uECC', 'sha256', 'verify_p256', 'k_inktag_da_ota_public_key'))
    return binary


CASES = ('unsigned', 'signed-opaque', 'bad-native-crc', 'bad-hash-crc-fixed', 'reserved',
    'wrong-board', 'wrong-layout', 'wrong-algorithm', 'wrong-magic', 'wrong-trailer-context',
    'epoch-rollback', 'zero-size', 'over-size', 'wrap-size', 'short-read', 'arm-fail',
    'release-fail', 'header-changed', 'flash-unavailable', 'max-size')


@pytest.mark.parametrize('case', CASES)
def test_integrity_admission(native, case, tmp_path):
    raw = struct.pack('<II', 0x07fd7000, 0x07fc0009) + bytes(range(120))
    image = bytearray(OTA(raw, '0.2.1.0'))
    tail = len(image)-96
    pre = accepted = arms = releases = 1
    mode = 0
    if case == 'signed-opaque':
        struct.pack_into('<I', image, 60, 0x314b4144)
        struct.pack_into('<I', image, tail, 0x31534b49)
        image[tail+5] = 1
        struct.pack_into('<I', image, tail+24, 0x314b4144)
        image[tail+32:] = bytes(64)
    elif case == 'bad-native-crc':
        image[75] ^= 1
        accepted = arms = releases = 0
    elif case == 'bad-hash-crc-fixed':
        image[tail+32] ^= 1
    elif case == 'reserved':
        image[-1] = 1
        accepted = arms = releases = 0
    elif case in ('wrong-board', 'wrong-layout', 'epoch-rollback', 'zero-size', 'over-size', 'wrap-size'):
        at, value = {'wrong-board': (52, 0), 'wrong-layout': (56, 0), 'epoch-rollback': (48, 0),
            'zero-size': (44, 0), 'over-size': (44, 95649), 'wrap-size': (44, 0xffffffa0)}[case]
        struct.pack_into('<I', image, at, value)
        pre = accepted = arms = releases = 0
    elif case in ('wrong-algorithm', 'wrong-magic', 'wrong-trailer-context'):
        at = {'wrong-algorithm': tail+5, 'wrong-magic': tail, 'wrong-trailer-context': tail+16}[case]
        image[at] ^= 1
        accepted = arms = releases = 0
    elif case in ('short-read', 'arm-fail', 'release-fail', 'header-changed', 'flash-unavailable'):
        mode = {'short-read': 1, 'arm-fail': 2, 'release-fail': 3, 'header-changed': 4, 'flash-unavailable': 5}[case]
        accepted = 0
        arms = int(mode in (2, 3))
        releases = int(mode == 3)
    elif case == 'max-size':
        image = bytearray(OTA(raw+bytes(95648-len(raw)), '0.2.1.0'))
    if case != 'bad-native-crc':
        struct.pack_into('<I', image, 8, zlib.crc32(image[64:]))
    path = tmp_path/'app.img'
    path.write_bytes(image)
    result = subprocess.check_output(['wsl', '--exec', linux(native), linux(path), str(mode)], text=True)
    actual = list(map(int, result.split()))
    assert actual[:4] == [pre, accepted, arms, releases]
