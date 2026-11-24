"""Exercise actual CRC-only protobuf/admission with no ECC headers or library."""
import importlib.util
import io
import os
import shutil
import subprocess
from pathlib import Path
import struct
import tempfile
import zipfile
from nrf_sdk_host import _compile_run, _function

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ['INKTAG_NRF_SDK_ROOT'])
config = (ROOT/'app/bootloader/src/sdk_config.h').read_text(encoding='utf-8')
for option in ('NRF_BL_APP_CRC_CHECK_SKIPPED_ON_GPREGRET2',
               'NRF_BL_APP_CRC_CHECK_SKIPPED_ON_SYSTEMOFF_RESET'):
    assert '#define '+option+' 0' in config


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def array(name, data):
    return 'static const uint8_t ' + name + '[]={' + ','.join(map(str, data)) + '};\n'


packer = load('crc_packer', 'build-support/ota/package-nrf52811.py')
app = struct.pack('<II', 0x20006000, 0x19009) + bytes(range(56))
archive, _ = packer.package(app, 201)
with zipfile.ZipFile(io.BytesIO(archive)) as z:
    unsigned = z.read('application.dat')
init = unsigned[6:]
# A structurally valid signed envelope with an invalid publisher signature.
# No test key, cryptography package, ECC include or ECC implementation is used.
signed = packer.fields([(2, packer.fields([
    (1, packer.fields([(1, 1), (2, init)])), (2, 0), (3, bytes(64))]))])
materializer = load('crc_validation', 'build-support/sdk/materialize-nordic-dfu-validation.py')
vendor = SDK / 'components/libraries/bootloader/dfu'
source = materializer.materialize(vendor / 'nrf_dfu_validation.c')
assert all(s not in source for s in ('nrf_dfu_validation_signature_check(',
    'signature_required(', 'nrf_crypto_', '#include "nrf_crypto', 'pk[64]'))
versions = (ROOT/'compat/nordic/src/inktag_nrf_dfu_ver_validation.c').read_text(encoding='utf-8')
functions = '\n'.join(_function(source, s) for s in (
    'static void pb_decoding_callback(', 'static bool stored_init_cmd_decode(',
    'nrf_dfu_result_t nrf_dfu_validation_prevalidate(',
    'static nrf_dfu_result_t update_data_size_get(',
    'bool nrf_dfu_validation_boot_validate('))
functions += '\n' + '\n'.join(_function(versions, s) for s in (
    'static bool softdevice_requirement_ok(', 'nrf_dfu_result_t nrf_dfu_ver_validation_check('))
# Declare version checking before the actual generated prevalidation function.
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "pb_decode.h"
#include "pb_common.h"
#include "dfu-cc.pb.h"
#include "crc32.h"
#include "inktag_nrf_dfu_hash.h"
#include "inktag_nrf_dfu_epoch.h"
#include "inktag_nrf_dfu_policy.h"
#include "inktag_recovery_version.h"
typedef unsigned nrf_dfu_result_t;
typedef struct { unsigned type; uint8_t bytes[64]; } boot_validation_t;
#define VALIDATE_CRC 1u
static nrf_dfu_result_t nrf_dfu_ver_validation_check(dfu_init_command_t const *);
#define ASSERT(x) assert(x)
#define NRF_LOG_ERROR(...)
#define NRF_LOG_DEBUG(...)
#define EXT_ERR(x) (0x100u+(x))
#define NRF_DFU_RES_CODE_SUCCESS 1u
#define NRF_DFU_EXT_ERROR_INIT_COMMAND_INVALID 3u
#define NRF_DFU_EXT_ERROR_HW_VERSION_FAILURE 4u
#define NRF_DFU_EXT_ERROR_SD_VERSION_FAILURE 5u
#define NRF_DFU_EXT_ERROR_FW_VERSION_FAILURE 6u
#define NRF_DFU_HW_VERSION 52u
#define MBR_SIZE 4096u
#define SD_FWID_GET(x) 0x0126u
static dfu_packet_t m_packet;
static pb_istream_t m_pb_stream;
static dfu_init_command_t *mp_init;
static bool m_init_packet_valid;
static uint8_t *m_init_packet_data_ptr;
static uint32_t m_init_packet_data_len;
static struct {
    uint8_t init_command[512];
    struct {unsigned command_size;} progress;
    uint32_t app_version;
} s_dfu_settings;
''' + functions + '\n'
harness += array('unsigned_packet', unsigned) + array('signed_packet', signed)
harness += array('both_packet', unsigned+signed) + array('app', app)
harness += r'''
static bool decode(const uint8_t *p, unsigned n) {
    memcpy(s_dfu_settings.init_command,p,n);s_dfu_settings.progress.command_size=n;
    return stored_init_cmd_decode();
}
int main(void) {
    s_dfu_settings.app_version=0x10000u;
    assert(INKTAG_RECOVERY_VERSION_PATCH==1 && INKTAG_RECOVERY_EPOCH==1);
    assert(INKTAG_NRF_DFU_RESUME_PROTOCOL==0xe1u);
    assert(decode(unsigned_packet,sizeof(unsigned_packet)));
    assert(nrf_dfu_validation_prevalidate()==NRF_DFU_RES_CODE_SUCCESS);
    assert(inktag_nrf_dfu_sha256_matches_le(mp_init->hash.hash.bytes,app,sizeof(app)));
    uint8_t corrupt[sizeof(app)];memcpy(corrupt,app,sizeof(app));corrupt[10]^=1;
    assert(!inktag_nrf_dfu_sha256_matches_le(mp_init->hash.hash.bytes,corrupt,sizeof(app)));
    /* The real cold-boot/resume validation function only admits matching CRC.
     * Non-PIE keeps these host fixture addresses inside the native 32-bit ABI. */
    assert((uintptr_t)app <= UINT32_MAX);
    boot_validation_t validation={.type=VALIDATE_CRC};
    uint32_t crc=crc32_compute(app,sizeof(app),NULL);
    memcpy(validation.bytes,&crc,4);
    assert(nrf_dfu_validation_boot_validate(&validation,(uint32_t)(uintptr_t)app,sizeof(app)));
    validation.bytes[0]^=1;
    assert(!nrf_dfu_validation_boot_validate(&validation,(uint32_t)(uintptr_t)app,sizeof(app)));
    validation.type=0;
    assert(!nrf_dfu_validation_boot_validate(&validation,(uint32_t)(uintptr_t)app,sizeof(app)));
    validation.type=3;
    assert(!nrf_dfu_validation_boot_validate(&validation,(uint32_t)(uintptr_t)app,sizeof(app)));
    mp_init->hw_version=56;assert(nrf_dfu_validation_prevalidate()!=NRF_DFU_RES_CODE_SUCCESS);
    mp_init->hw_version=52;mp_init->sd_req[0]=0;assert(nrf_dfu_validation_prevalidate()!=NRF_DFU_RES_CODE_SUCCESS);
    mp_init->sd_req[0]=294;mp_init->fw_version=201;assert(nrf_dfu_validation_prevalidate()!=NRF_DFU_RES_CODE_SUCCESS);
    assert(decode(signed_packet,sizeof(signed_packet)));
    assert(nrf_dfu_validation_prevalidate()==NRF_DFU_RES_CODE_SUCCESS);
    m_packet.signed_command.signature.bytes[0]^=1;
    assert(nrf_dfu_validation_prevalidate()==NRF_DFU_RES_CODE_SUCCESS);
    m_packet.signed_command.signature_type=DFU_SIGNATURE_TYPE_ED25519;
    assert(nrf_dfu_validation_prevalidate()==NRF_DFU_RES_CODE_SUCCESS);
    mp_init->is_debug=true;mp_init->has_is_debug=true;
    assert(nrf_dfu_validation_prevalidate()!=NRF_DFU_RES_CODE_SUCCESS);
    mp_init->is_debug=false;mp_init->type=DFU_FW_TYPE_BOOTLOADER;
    assert(nrf_dfu_validation_prevalidate()!=NRF_DFU_RES_CODE_SUCCESS);
    mp_init->type=DFU_FW_TYPE_APPLICATION;uint32_t size=0;
    mp_init->app_size=0;assert(update_data_size_get(mp_init,&size)!=NRF_DFU_RES_CODE_SUCCESS);
    mp_init->app_size=sizeof(app);assert(update_data_size_get(mp_init,&size)==NRF_DFU_RES_CODE_SUCCESS && size==sizeof(app));
    assert(!decode(both_packet,sizeof(both_packet)));
    assert(!decode(unsigned_packet,sizeof(unsigned_packet)-1));
    assert(!decode(unsigned_packet,0));
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    for san in (False, True):
        # The immutable SDK SHA has a signed left-shift in byte decoding.
        # Compile that third-party object with ASan only; all owned admission,
        # hash adapter and protobuf parsing below retain full ASan/UBSan.
        sha_object = Path(directory)/('sha-'+str(san)+'.o')
        subprocess.run([shutil.which('gcc') or 'cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
            *(['-fsanitize=address'] if san else []),
            '-I'+str(ROOT/'tests/mocks/nrf_crypto'),
            '-I'+str(SDK/'components/libraries/sha256'),
            '-I'+str(SDK/'components/libraries/util'),
            '-I'+str(SDK/'components/softdevice/s112/headers'),
            '-c', str(SDK/'components/libraries/sha256/sha256.c'), '-o', str(sha_object)], check=True)
        _compile_run(Path(directory), 'crc-'+str(san), harness,
            includes=('tests/mocks/nrf_crypto', 'compat/common/ota', 'compat/nordic/src', str(vendor),
                str(SDK/'external/nano-pb'), str(SDK/'components/libraries/sha256'),
                str(SDK/'components/libraries/crc32'),
                str(SDK/'components/libraries/util'), str(SDK/'components/softdevice/s112/headers')),
            sources=('compat/nordic/src/inktag_nrf_dfu_hash.c',
                str(sha_object), str(SDK/'components/libraries/crc32/crc32.c'), str(vendor/'dfu-cc.pb.c'),
                str(SDK/'external/nano-pb/pb_common.c'), str(SDK/'external/nano-pb/pb_decode.c')),
            sanitizers=san, defines=('-Wno-unused-function', '-Wno-unused-parameter',
                '-Wno-int-to-pointer-cast', '-fno-pie', '-no-pie', '-DNRF_MODULE_ENABLED(x)=1'))
    boot = load('crc_boot', 'build-support/sdk/materialize-nordic-dfu-resume.py').materialize(
        SDK/'components/libraries/bootloader/nrf_bootloader.c')
    decision = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "sdk_config.h"
static unsigned reset_bits, retained_bits, writes;
static unsigned nrf_power_resetreas_get(void) { return reset_bits; }
static unsigned nrf_power_gpregret2_get(void) { return retained_bits; }
static void nrf_power_resetreas_clear(unsigned bits) { (void)bits; writes++; }
static void nrf_power_gpregret2_set(unsigned bits) { (void)bits; writes++; }
#define NRF_POWER_RESETREAS_OFF_MASK 1u
#define BOOTLOADER_DFU_SKIP_CRC_MASK 1u
#define BOOTLOADER_DFU_SKIP_CRC 1u
''' + _function(boot, 'static bool crc_on_valid_app_required(') + r'''
int main(void) {
    for(reset_bits=0;reset_bits<256;reset_bits++)
        for(retained_bits=0;retained_bits<256;retained_bits++)
            assert(crc_on_valid_app_required());
    assert(writes==0);
    return 0;
}
'''
    _compile_run(Path(directory), 'cold-crc-required', decision, sanitizers=True,
        includes=('app/bootloader/src',
                  'compat/nordic/config', '../../../boards',
                  'compat/nordic/src'), defines=('-Wno-unused-function', '-DNRF52811_XXAA'))
print('PASS CRC-only SDK decoder/boot CRC: envelope, hash/CRC, target/epoch/type/debug/size, malformed input; strict + owned-code ASan/UBSan (vendor SHA ASan only), no ECC')
