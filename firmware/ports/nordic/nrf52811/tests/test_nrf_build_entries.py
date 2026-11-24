"""Actual public Make routing: one CRC-only role, no key/policy switches."""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
env = {k:v for k,v in os.environ.items() if k not in (
    'MAKEFLAGS','MFLAGS','MAKEOVERRIDES','INKTAG_NRF_REQUIRE_SIGNED_DFU')}


def make(*args, environment=env):
    return subprocess.run(['make', *args], cwd=ROOT, env=environment,
                          capture_output=True, text=True)


for cache, output in [('.cache/build/nordic/nrf52811_aesl','_build/nordic/nrf52811_aesl'),
                      ('custom-cache','custom-output')]:
    result = make('print-build-profile', 'NRF_BUILD_ROOT='+cache,
        'NRF_OUTPUT_ROOT='+output, 'NRF_SIGNATURE_PROFILE=required')
    assert result.returncode==0, result.stderr
    profile = dict(line.split('=',1) for line in result.stdout.splitlines())
    assert profile['profile']=='crc-only' and profile['verificationPolicy']=='crc-sha256'
    assert profile['recoveryVersion']=='1.0.1' and profile['signatureRequired']=='0'
    assert profile['build']==cache+'/crc-only' and profile['output']==output+'/crc-only'
    assert profile['signing']=='no' and profile['auxReservedEnabled']=='0'
for entry in ('Makefile','app/bootloader/Makefile'):
    goal='print-build-profile' if entry=='Makefile' else 'print-delivery-config'
    for value in ('0','1',''):
        for result in (make('-f',entry,goal,'INKTAG_NRF_REQUIRE_SIGNED_DFU='+value),
                       make('-f',entry,goal,environment={**env,'INKTAG_NRF_REQUIRE_SIGNED_DFU':value})):
            assert result.returncode and 'retired' in result.stderr
result=make('-n','full')
assert result.returncode==0 and 'python3 build.py' in result.stdout
assert '--signed-ota' not in result.stdout and 'Makefile.signed' not in result.stdout
result=make('-f','app/bootloader/Makefile','print-delivery-config')
assert result.returncode==0,result.stderr
assert 'inktag_nrf_dfu_hash.c' in result.stdout
assert not any(s in result.stdout for s in ('uECC','micro-ecc','dfu_public_key','inktag_nrf_dfu_crypto'))
assert not (ROOT/'Makefile.signed').exists()
print('PASS actual CRC-only Make routes, isolated cache, retired options rejected, no ECC/key dependency or signer')
