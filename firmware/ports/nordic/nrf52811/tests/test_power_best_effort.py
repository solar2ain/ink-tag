"""Standalone shared power oracles, without importing another port's helper.

The single admission entry retains valid thresholds and UNKNOWN telemetry.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

FIXTURE = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "inktag_power.h"
SOURCE
int main(void) {
    const inktag_power_thresholds_t thresholds = INKTAG_POWER_POLICY_THRESHOLDS_INITIALIZER;
    for (unsigned mv = 0; mv <= UINT16_MAX; ++mv) {
        assert(inktag_power_classify(mv, false, &thresholds) == INKTAG_POWER_UNKNOWN);
        for (unsigned op = 0; op < 3; ++op) {
            assert(inktag_power_operation_allowed(op, mv, false, &thresholds));
            unsigned threshold = op == INKTAG_POWER_OPERATION_DISPLAY ? 0u :
                op == INKTAG_POWER_OPERATION_FLASH ? 2400u : 2600u;
            assert(inktag_power_operation_allowed(op, mv, true, &thresholds) == (mv >= threshold));
        }
    }
    for (unsigned valid = 0; valid < 2; ++valid) {
        assert(!inktag_power_operation_allowed(99, 3300, valid, &thresholds));
        assert(!inktag_power_operation_allowed(INKTAG_POWER_OPERATION_FLASH, 3300, valid, NULL));
    }
    assert(!inktag_power_warning_cleared(3300, false, &thresholds));
    for (unsigned op = 0; op < 3; ++op) {
        inktag_power_thresholds_t disabled = thresholds;
        if (op == 0) disabled.display_min_mv = UINT16_MAX;
        if (op == 1) disabled.flash_min_mv = UINT16_MAX;
        if (op == 2) disabled.ota_min_mv = UINT16_MAX;
        assert(!inktag_power_operation_allowed(op, 0, false, &disabled));
        assert(!inktag_power_operation_allowed(op, 3300, true, &disabled));
    }
    assert(!inktag_power_operation_allowed(INKTAG_POWER_OPERATION_FLASH, 0, true, &thresholds));
    assert(!inktag_power_operation_allowed(INKTAG_POWER_OPERATION_OTA, 0, true, &thresholds));
    return 0;
}
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence-dir', type=Path)
    args = parser.parse_args()
    compiler = os.environ.get('CC') or shutil.which('gcc') or shutil.which('cc')
    if not compiler:
        raise RuntimeError('Host C compiler unavailable; actual C regression is blocked, not passed.')
    source = (ROOT / 'compat/common/power/inktag_power.c').read_text(encoding='utf-8')
    with tempfile.TemporaryDirectory() as temporary:
        directory = args.evidence_dir or Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        unit = directory / 'shared-power-best-effort.c'
        unit.write_text(FIXTURE.replace('SOURCE', source), encoding='utf-8')
        variants = (('normal', ['-O2']), ('asan-ubsan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']))
        for variant, base_flags in (*variants, *((name+'-packed', flags+['-fshort-enums','-fpack-struct=1']) for name, flags in variants)):
            flags = list(base_flags)
            binary = directory / ('shared-power-' + variant)
            subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror', *flags,
                            '-Ifirmware/common/power', str(unit), '-o', str(binary)], cwd=ROOT, check=True)
            subprocess.run([str(binary)], check=True)
            print('PASS shared exhaustive power fixture ' + variant, flush=True)


if __name__ == '__main__':
    main()
