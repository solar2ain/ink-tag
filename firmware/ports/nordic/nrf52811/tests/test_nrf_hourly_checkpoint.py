"""Compile the shipping checkpoint function, without a radio or SDK device."""
from pathlib import Path
import re
import tempfile
import unittest

from nrf_sdk_host import ROOT, _function, _compile_run


class HourlyCheckpoint(unittest.TestCase):
    def test_save_boundary_and_failure_retry(self):
        source = (ROOT / 'compat/nordic/src/main.c').read_text()
        macros = '\n'.join(re.findall(
            r'^#define TIME_CHECKPOINT_(?:INTERVAL|RETRY_INTERVAL)\s+[^\n]+',
            source, re.M))
        unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
static uint32_t m_timestamp, m_time_checkpoint, m_time_checkpoint_retry_at;
static bool m_time_valid, m_time_checkpoint_valid;
static unsigned attempts;
static bool admitted = true;
static bool epd_time_write(uint32_t value) {
    assert(value == m_timestamp); ++attempts; return admitted;
}
''' + macros + '\n' + _function(source, 'static void time_checkpoint_try(') + r'''
int main(void) {
    const uint32_t base = 1704067200u;
    m_time_valid = true; m_time_checkpoint_valid = true;
    m_time_checkpoint = base;
    m_timestamp = base + 3599u; time_checkpoint_try(m_timestamp);
    assert(attempts == 0 && m_time_checkpoint == base);
    admitted = false; m_timestamp++; time_checkpoint_try(m_timestamp);
    assert(attempts == 1 && m_time_checkpoint == base);
    m_timestamp += 59u; time_checkpoint_try(m_timestamp); assert(attempts == 1);
    admitted = true; m_timestamp++; time_checkpoint_try(m_timestamp);
    assert(attempts == 2 && m_time_checkpoint == base + 3660u);
    time_checkpoint_try(m_timestamp); assert(attempts == 2);
    m_timestamp += 3599u; time_checkpoint_try(m_timestamp); assert(attempts == 2);
    m_timestamp++; time_checkpoint_try(m_timestamp); assert(attempts == 3);
    m_time_valid = false; m_timestamp += 3600u;
    time_checkpoint_try(m_timestamp); assert(attempts == 3);
    /* A late check saves one current snapshot, not several stale hours. */
    m_time_valid = true; m_timestamp += 7200u;
    time_checkpoint_try(m_timestamp); assert(attempts == 4);
    assert(m_time_checkpoint == m_timestamp);
    puts("PASS shipping hourly boundary, failed-save marker, 60s retry, invalid time and late coalescing");
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            for sanitizer in (False, True):
                _compile_run(Path(temporary), 'hourly', unit, sanitizers=sanitizer)


if __name__ == '__main__':
    unittest.main(verbosity=2)
