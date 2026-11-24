"""Exercise actual button-only Make/Full entries and compile-time policy."""
import os
from pathlib import Path
import runpy
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
env = {k:v for k,v in os.environ.items() if k not in
       ('MAKEFLAGS','MFLAGS','MAKEOVERRIDES','INKTAG_AUX_RESERVED_ENABLED')}
entries = [('Makefile','print-build-profile'),
           ('app/Makefile','print-delivery-config'),
           ('app/bootloader/Makefile','print-delivery-config')]
for entry,goal in entries:
    for value in (None,'0'):
        result = subprocess.run(['make','-f',entry,goal,
            *(['INKTAG_AUX_RESERVED_ENABLED='+value] if value is not None else [])],
            cwd=ROOT,env=env,capture_output=True,text=True)
        assert result.returncode == 0, result.stderr
        config = dict(line.split('=',1) for line in result.stdout.splitlines() if '=' in line)
        if entry == 'Makefile':
            assert config['auxReservedEnabled']=='0'
        else:
            assert shlex.split(config['CFLAGS']).count('-DINKTAG_AUX_RESERVED_ENABLED=0')==1
            sources=shlex.split(config['SOURCES'])
            assert not any(Path(n).name.startswith(('nrfx_pwm','nrf_drv_pwm','app_pwm')) for n in sources)
            if '/bootloader/' not in entry:
                assert 'compat/common/input/inktag_input.c' in sources
    for value in ('1','2','-1',''):
        for args,custom in [(['INKTAG_AUX_RESERVED_ENABLED='+value],env),
                            ([],{**env,'INKTAG_AUX_RESERVED_ENABLED':value})]:
            result=subprocess.run(['make','-f',entry,goal,*args],cwd=ROOT,
                                  env=custom,capture_output=True,text=True)
            assert result.returncode and 'requires INKTAG_AUX_RESERVED_ENABLED=0' in result.stderr

builder=runpy.run_path(str(ROOT/'build-support/build/build-full.py'))
good='CFLAGS=-Oz -DINKTAG_AUX_RESERVED_ENABLED=0\nSOURCES=compat/common/input/inktag_input.c'
builder['aux_reserved_policy'](good)
for bad in ('SOURCES=compat/common/input/inktag_input.c',
            good.replace('=0','=1'),good+' nrfx_pwm.c',good+' nrf_drv_pwm.c',
            good+' app_pwm.c',good.replace('=0','=0 -DINKTAG_AUX_RESERVED_ENABLED=1')):
    try: builder['aux_reserved_policy'](bad)
    except ValueError: pass
    else: raise AssertionError('Full builder accepted incorrect actual role config')

with tempfile.TemporaryDirectory() as directory:
    source=Path(directory)/'policy.c'
    source.write_text('#include "inktag_board.h"\n#include "inktag_base_policy.h"\n#include "inktag_release_v1.h"\n'
        '_Static_assert(INKTAG_RELEASE_AUX_VERSION==1u && INKTAG_RELEASE_AUX_BYTES==12u,"wire ABI");\n'
        '_Static_assert(INKTAG_AUX_RESERVED_ENABLED==0,"button-only");\nint main(void){return 0;}\n')
    for value in (None,'0','1','2'):
        binary=Path(directory)/'policy'
        result=subprocess.run([os.environ.get('CC','cc'),'-std=c11','-DNRF52811_XXAA',
            '-Ifirmware/ports/nordic/common/config','-Ifirmware/boards','-Ifirmware/common/protocol',
            *(['-DINKTAG_AUX_RESERVED_ENABLED='+value] if value is not None else []),
            str(source),'-o',str(binary)],cwd=ROOT,capture_output=True)
        assert (result.returncode==0)==(value in (None,'0')), (value,result.stderr.decode(errors='replace'))
        if result.returncode==0:subprocess.run([str(binary)],check=True)
print('PASS actual App/Recovery/Full button-only routes, conflicting builds rejected, stock source retained and no PWM dependency')

# Reserved numeric allocations and wire size are part of this source closure.
header=(ROOT/'compat/common/epd/inktag_protocol_v2.h').read_text()
assert 'INKTAG_TEST_RESERVED_03 = 0x03' in header
print('PASS reserved 0x03 ID and unchanged AUX v1/12B allocation')
