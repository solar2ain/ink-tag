"""Clean source Full fixtures: no production packages, private keys or devices."""
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch
import zipfile
import zlib

from fixtures.nrf11_full_fixture import fixture, fields, zip_bytes

ROOT = Path(__file__).resolve().parents[1]


def module(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT/relative)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


builder = module('source_full_builder', 'build-support/build/build-full.py')
packer = module('source_ota_packer', 'build-support/ota/package-nrf52811.py')


class SourceFull(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.full, self.original, self.config, self.ficr, self.files, self.memory = fixture(
            self.root, b'N11PINCFG1\0\0', app_version=201)
        self.app, self.version = self.full.signed_app(self.files['app-ota.zip'])
        self.rec = bytes(self.memory[a] for a in sorted(self.memory) if 0x2a000 <= a < 0x2e000)
        self.sd = {a: v for a, v in self.memory.items() if a < 0x19000}
        self.gui = self.files['gui-assets.bin']
        self.crc = struct.unpack_from('<I', self.gui, 12)[0]

    def compose(self, profile='crc-only', **changes):
        args = dict(full=self.full, packer=packer, sd_memory=self.sd, app=self.app,
                    recovery=self.rec, assets=self.gui, ota_version=201,
                    profile=profile, expected_gui_crc=self.crc)
        args.update(changes)
        return builder.compose(**args)

    def package(self, name='source.zip', profile='crc-only'):
        memory, ota, manifest, seed = self.compose(profile)
        path = self.root/name
        review = builder.write_package(self.full, path, memory, ota, self.gui, manifest)
        return path, review, seed

    def rebound(self, path, name, members=None, manifest=None):
        with zipfile.ZipFile(path) as z:
            files = {n: z.read(n) for n in z.namelist()}
        base = json.loads(files['base.json'])
        if members:
            files.update(members)
        if manifest:
            base.update(manifest)
        base['images'] = {n: {'bytes': len(b), 'sha256': hashlib.sha256(b).hexdigest()}
                          for n, b in files.items() if n != 'base.json'}
        files['base.json'] = json.dumps(base).encode()
        output = self.root/name
        output.write_bytes(zip_bytes(files))
        return output

    def test_default_unsigned_full_is_explicit_and_not_publisher_verified(self):
        path, review, seed = self.package()
        self.assertTrue(review['selfBuilt'])
        self.assertFalse(review['signatureVerified'])
        self.assertFalse(review['publisherVerified'])
        self.assertEqual(review['minimumToolsVersion'], '0.2.1.2')
        self.assertEqual(review['recoverySignaturePolicy'], 'crc-only')
        self.assertEqual(review['recoveryPolicySource'], 'self-built-manifest')
        self.assertEqual(review['recoveryVerificationPolicy'], 'crc-sha256')
        self.assertEqual(review['declaredRecoveryProtocol'], 0xe1)
        memory, files, _ = self.full.verify_bundle(path)
        self.assertEqual(files['gui-assets.bin'], self.gui)
        self.assertEqual(bytes(memory[0x19000+i] for i in range(len(self.app))), self.app)
        for origin in (0x2e000, 0x2f000):
            self.assertEqual(bytes(memory[origin+i] for i in range(0x323)), seed)
        self.assertEqual(struct.unpack_from('<I', seed, 8)[0], 65737)
        self.assertEqual(struct.unpack_from('<I', seed, 12)[0], 0)
        self.assertEqual(seed[0x5c:0x25c], bytes(512))

    def test_retired_builder_profiles_fail(self):
        for profile in ('optional', 'required', 'unknown'):
            with self.assertRaisesRegex(ValueError, 'Unknown Recovery profile'):
                self.compose(profile)

    def test_invalid_supplied_signature_never_falls_back(self):
        with zipfile.ZipFile(io.BytesIO(self.files['app-ota.zip'])) as z:
            files = {n: z.read(n) for n in z.namelist()}
        item = json.loads(files['manifest.json'])['manifest']['application']
        dat = bytearray(files[item['dat_file']]);dat[-1] ^= 1
        files[item['dat_file']] = bytes(dat)
        with self.assertRaisesRegex(ValueError, 'signature verification failed'):
            self.full.signed_app(zip_bytes(files))

    def test_unsigned_cannot_disguise_as_required(self):
        path, _, _ = self.package()
        with self.assertRaisesRegex(ValueError, 'Signed Nordic'):
            self.full.verify_bundle(self.rebound(path, 'required.zip', manifest={
                'recovery_signature_policy': 'required', 'minimum_tools_version': '0.2.1.0'}))

    def test_legacy_optional_label_is_preserved_without_inventing_crc_only(self):
        path, _, _ = self.package()
        legacy = self.rebound(path, 'legacy.zip', manifest={
            'recovery_signature_policy': 'optional', 'minimum_tools_version': '0.2.1.0'})
        _, _, review = self.full.verify_bundle(legacy)
        self.assertEqual(review['recoverySignaturePolicy'], 'optional')
        self.assertEqual(review['minimumToolsVersion'], '0.2.1.0')
        self.assertNotIn('declaredRecoveryProtocol', review)
        self.assertNotIn('recoveryVerificationPolicy', review)

    def test_crc_only_label_requires_new_tools_minimum(self):
        path, _, _ = self.package()
        for version in ('0.2.1.0', '0.2.1.1'):
            with self.subTest(version=version), self.assertRaisesRegex(ValueError, 'format/identity'):
                self.full.verify_bundle(self.rebound(path, version+'.zip', manifest={
                    'minimum_tools_version': version}))

    def test_crc_only_metadata_does_not_disable_host_signature_checks(self):
        path, _, _ = self.package()
        signed = self.rebound(path, 'signed.zip', members={'app-ota.zip': self.files['app-ota.zip']})
        _, _, review = self.full.verify_bundle(signed)
        self.assertTrue(review['appSignatureVerified'])
        self.assertFalse(review['publisherVerified'])
        with zipfile.ZipFile(io.BytesIO(self.files['app-ota.zip'])) as archive:
            ota = {name: archive.read(name) for name in archive.namelist()}
        entry = json.loads(ota['manifest.json'])['manifest']['application']
        damaged = bytearray(ota[entry['dat_file']]); damaged[-1] ^= 1
        ota[entry['dat_file']] = bytes(damaged)
        invalid = self.rebound(path, 'invalid-signed.zip', members={'app-ota.zip': zip_bytes(ota)})
        with self.assertRaisesRegex(ValueError, 'signature verification failed'):
            self.full.verify_bundle(invalid)

    def test_unsigned_cannot_disguise_as_official(self):
        path, _, _ = self.package()
        with zipfile.ZipFile(path) as z:
            files = {n: z.read(n) for n in z.namelist()}
        manifest = json.loads(files['base.json'])
        for key in ('container_kind', 'recovery_signature_policy', 'minimum_tools_version', 'app_gui_crc32'):
            manifest.pop(key)
        files['base.json'] = json.dumps(manifest).encode()
        forged = self.root/'official.zip';forged.write_bytes(zip_bytes(files))
        with self.assertRaisesRegex(ValueError, 'Signed Nordic'):
            self.full.verify_bundle(forged)

    def test_unknown_identity_policy_and_minimum_tools_fail_closed(self):
        path, _, _ = self.package()
        cases = [('container_kind', 'unknown'), ('recovery_signature_policy', 'ignore-all'),
                 ('minimum_tools_version', '0.2.0.4'), ('app_gui_crc32', True),
                 ('compatibility_epoch', 2), ('hw_version', 56), ('layout_marker', 'N3V1')]
        for index, (key, value) in enumerate(cases):
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.full.verify_bundle(self.rebound(path, f'wrong-{index}.zip', manifest={key: value}))

    def test_wrong_gui_pair_rejected_even_with_valid_payload_crc(self):
        other = bytearray(self.gui);other[20] ^= 1
        struct.pack_into('<I', other, 12, zlib.crc32(other[16:12244]))
        with self.assertRaisesRegex(ValueError, 'compiled resource binding'):
            self.compose(assets=bytes(other))
        path, _, _ = self.package()
        with self.assertRaisesRegex(ValueError, 'GUI binding'):
            self.full.verify_bundle(self.rebound(path, 'other-gui.zip', members={'gui-assets.bin': bytes(other)}))

    def test_vectors_sizes_and_softdevice_boundaries(self):
        cases = [dict(app=self.app*100), dict(recovery=self.rec*300),
                 dict(recovery=struct.pack('<II', 0x20007000, 0x2a009)+self.rec[8:]),
                 dict(app=struct.pack('<II', 0x20006000, 0x2a001)+self.app[8:]),
                 dict(sd_memory={0x19000: 0}), dict(ota_version=65536)]
        for index, change in enumerate(cases):
            with self.subTest(case=index), self.assertRaises(ValueError):
                self.compose(**change)

    def test_rebound_full_tamper_is_not_accepted(self):
        path, _, _ = self.package()
        memory = self.full.verify_bundle(path)[0]
        memory[0x19008] ^= 1
        altered = self.root/'changed.hex';self.full.write_hex(altered, memory)
        with self.assertRaises(ValueError):
            self.full.verify_bundle(self.rebound(path, 'tampered.zip', members={'full.hex': altered.read_bytes()}))

    def test_package_is_deterministic(self):
        first, _, _ = self.package('one.zip')
        second, _, _ = self.package('two.zip')
        self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_existing_official_format_stays_signed_only(self):
        review = self.full.verify_bundle(self.original)[2]
        self.assertTrue(review['signatureVerified'])
        self.assertNotIn('selfBuilt', review)

    def test_archive_boundaries_and_ambiguous_commands(self):
        with zipfile.ZipFile(io.BytesIO(self.files['app-ota.zip'])) as archive:
            files = {n: archive.read(n) for n in archive.namelist()}
        item = json.loads(files['manifest.json'])['manifest']['application']
        signed = self.full.fields(files[item['dat_file']])[2]
        command = self.full.fields(signed)[1]
        both = {**files, item['dat_file']: fields({1: command, 2: signed})}
        with self.assertRaises(ValueError):
            self.full.signed_app(zip_bytes(both))
        for member in ['../application.bin', 'C:application.bin', 'a\\application.bin']:
            bad = {'manifest.json': files['manifest.json'], member: self.app, 'application.dat': b'x'}
            with self.subTest(member=member), self.assertRaises(ValueError):
                self.full.read_archive(zip_bytes(bad))
        for manifest in [b'[]', b'{"manifest":null}', b'{"manifest":{"application":[]}}']:
            bad = {**files, 'manifest.json': manifest}
            with self.subTest(manifest=manifest), self.assertRaises(ValueError):
                self.full.signed_app(zip_bytes(bad))

    def test_optional_does_not_admit_signed_packet_with_missing_signature(self):
        with zipfile.ZipFile(io.BytesIO(self.files['app-ota.zip'])) as z:
            files = {n: z.read(n) for n in z.namelist()}
        item = json.loads(files['manifest.json'])['manifest']['application']
        signed = self.full.fields(self.full.fields(files[item['dat_file']])[2])
        files[item['dat_file']] = fields({2: fields({1: signed[1], 2: 0, 3: b''})})
        with self.assertRaisesRegex(ValueError, 'ECDSA-P256'):
            self.full.signed_app(zip_bytes(files))

    def test_required_cli_fails_without_signed_input_before_building(self):
        result = subprocess.run([sys.executable, '-B', str(ROOT/'build.py'), '--profile', 'required'],
                                cwd=ROOT, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'invalid choice', result.stderr)

    def test_cli_rejects_conflicting_environment_before_build(self):
        result = subprocess.run([sys.executable, '-B', str(ROOT/'build.py')], cwd=ROOT,
            env={**os.environ, 'INKTAG_NRF_REQUIRE_SIGNED_DFU': '1'}, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'Retired Recovery signature policy', result.stderr)

    def test_compiler_policy_rejects_missing_integrity_or_retired_verifier(self):
        good = 'CFLAGS=-DINKTAG_AUX_RESERVED_ENABLED=0\nSOURCES=compat/nordic/src/inktag_nrf_dfu_hash.c generated/nrf_dfu_validation.c'
        builder.compiler_policy(good)
        for bad in ['', good+' uECC.c', good+' dfu_public_key.c',
                    good+' INKTAG_NRF_REQUIRE_SIGNED_DFU=1']:
            with self.assertRaises(ValueError):
                builder.compiler_policy(bad)

    @unittest.skipUnless((ROOT/'tools/delivery/normal_build.py').exists(), 'private publisher is deliberately not exported')
    def test_private_official_reuse_rejects_self_built_container(self):
        path, _, _ = self.package()
        sys.path.insert(0, str(ROOT/'tools/delivery'))
        normal = module('normal_source_full_guard', 'tools/delivery/normal_build.py')
        normal.native = types.SimpleNamespace(full=self.full)
        with self.assertRaisesRegex(ValueError, 'cannot select a self-built'):
            normal.reusable(path, {}, Path('unused'), 65737)

    @unittest.skipUnless((ROOT/'../../../../tools/platforms/nordic/nrf52811/programmer/internal/nrf11_full.py').exists(),
                         'public source builder exports only the pure Full format')
    def test_actual_programmer_prepares_unsigned_full_without_native_or_cli(self):
        path, _, _ = self.package()
        memory = self.full.verify_bundle(path)[0]
        spans = {span: bytes([255])*span[1] for span in self.full.missing_low_ranges(memory)}
        with patch('subprocess.Popen', side_effect=AssertionError('No native, CLI or service process allowed')):
            plan = self.full.prepare(path, self.config, self.ficr, spans, self.root/'prepared')
        self.assertTrue(plan['review']['selfBuilt'])
        self.assertFalse(plan['review']['publisherVerified'])
        self.assertFalse(plan['bleAccess'])
        self.assertEqual((self.root/'prepared/flash.bin').stat().st_size, 196608)
        self.assertEqual((self.root/'prepared/settings.bin').stat().st_size, 8192)

    @unittest.skipUnless(os.environ.get('INKTAG_NRF_SDK_ROOT'), 'SDK ABI cross-check requires the documented Nordic SDK')
    def test_seed_bytes_match_actual_sdk_struct_and_crc(self):
        sdk = Path(os.environ['INKTAG_NRF_SDK_ROOT'])
        source = (sdk/'components/libraries/bootloader/dfu/nrf_dfu_types.h').read_text(encoding='utf-8')
        source = source[source.index('#define NRF_DFU_BANK_INVALID'):].rsplit('#ifdef __cplusplus', 1)[0]
        sd_end = max(a for a in self.sd if a >= 0x1000)+1
        sd = bytes(self.sd.get(a, 255) for a in range(0x1000, sd_end))
        header = '''#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "crc32.h"
#define NRF_DFU_TRANSPORT_BLE 0
#define ANON_UNIONS_ENABLE
#define ANON_UNIONS_DISABLE
#define INIT_COMMAND_MAX_SIZE 512
#define STATIC_ASSERT(a,...) _Static_assert(a,"SDK type ABI")
#pragma pack(1)
'''
        main = '''
_Static_assert(offsetof(nrf_dfu_settings_t, init_command)==0x5c,"init ABI");
_Static_assert(offsetof(nrf_dfu_settings_t, boot_validation_crc)==0x25c,"validation ABI");
_Static_assert(offsetof(nrf_dfu_settings_t, peer_data)==0x323,"seed extent ABI");
int main(void) {
 nrf_dfu_settings_t s;memset(&s,0,sizeof(s));
 s.settings_version=2;s.app_version=65737;
 s.bank_0.image_size=APP_SIZE;s.bank_0.image_crc=APP_CRC;s.bank_0.bank_code=1;s.sd_size=SD_SIZE;
 s.boot_validation_softdevice.type=VALIDATE_CRC;s.boot_validation_app.type=VALIDATE_CRC;
 uint32_t a=APP_CRC,d=SD_CRC;memcpy(s.boot_validation_softdevice.bytes,&d,4);memcpy(s.boot_validation_app.bytes,&a,4);
 s.boot_validation_crc=crc32_compute((const uint8_t*)&s.boot_validation_softdevice,3*sizeof(boot_validation_t),NULL);
 s.crc=crc32_compute((const uint8_t*)&s+4,offsetof(nrf_dfu_settings_t,init_command)-4,NULL);
 return fwrite(&s,1,offsetof(nrf_dfu_settings_t,peer_data),stdout)==0x323?0:1;
}
'''
        unit = self.root/'sdk-seed.c';unit.write_text(header+source+main, encoding='utf-8')
        (self.root/'sdk_common.h').write_text('#include <stddef.h>\n#define NRF_MODULE_ENABLED(x) 1\n', encoding='utf-8')
        binary = self.root/'sdk-seed'
        subprocess.run(['cc','-std=c11','-fshort-enums','-I'+str(self.root),
            '-I'+str(sdk/'components/libraries/crc32'),
            '-DAPP_SIZE='+str(len(self.app)), '-DAPP_CRC='+str(zlib.crc32(self.app)),
            '-DSD_SIZE='+str(len(sd)), '-DSD_CRC='+str(zlib.crc32(sd)),
            str(unit), str(sdk/'components/libraries/crc32/crc32.c'),'-o',str(binary)], check=True)
        self.assertEqual(subprocess.check_output([str(binary)]), builder.settings(self.app, sd,65737))


if __name__ == '__main__':
    unittest.main(verbosity=2)
