#!/usr/bin/env python3
"""Build-local SDK17.1 BLE event-buffer placement; never edits the pinned SDK.

Retain the SDK's complete licensed source, FIFO/observer/error behavior and
event geometry. Serialize a static buffer against nested main/IRQ polls;
atomic last-empty release prevents a skipped nested poll from losing a wakeup.
"""
import argparse
import hashlib
from pathlib import Path

SDK_SHA256 = 'cf2aec65411af6cb56cc37990dcb1b0d02d92daa0bd613019bc2bf4ede33e48a'


def adapt(raw: bytes) -> str:
    if hashlib.sha256(raw).hexdigest() != SDK_SHA256:
        raise ValueError('unsupported nrf_sdh_ble.c; review the pinned SDK before adapting')
    text = raw.decode('utf-8').replace('\r\n', '\n')
    start = text.index('static void nrf_sdh_ble_evts_poll(void * p_context)')
    end = text.index('NRF_SDH_STACK_OBSERVER(m_nrf_sdh_ble_evts_poll', start)
    poll = text[start:end]
    buffer = '__ALIGN(4) uint8_t evt_buffer[NRF_SDH_BLE_EVT_BUF_SIZE];'
    if poll.count(buffer) != 1:
        raise ValueError('SDK event buffer declaration changed')
    poll = poll.replace(buffer, 'static ' + buffer)
    entry = '    while (true)\n    {'
    if poll.count(entry) != 1:
        raise ValueError('SDK event-drain loop changed')
    poll = poll.replace(entry, '''    static volatile bool poll_active, poll_again;
    bool acquired = false;
    CRITICAL_REGION_ENTER();
    if (poll_active) poll_again = true;
    else { poll_active = true; acquired = true; }
    CRITICAL_REGION_EXIT();
    if (!acquired) return;

    for (;;)
    {
    while (true)
    {''')
    exit_point = '    if (ret_code != NRF_ERROR_NOT_FOUND)'
    if poll.count(exit_point) != 1:
        raise ValueError('SDK terminal error handling changed')
    poll = poll.replace(exit_point, '''    bool retry;
    CRITICAL_REGION_ENTER();
    retry = poll_again && ret_code == NRF_ERROR_NOT_FOUND;
    poll_again = false;
    if (!retry) poll_active = false;
    CRITICAL_REGION_EXIT();
    if (!retry) break;
    }

''' + exit_point)
    return text[:start] + poll + text[end:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        parser.error('generated source must not replace the SDK source')
    generated = adapt(args.input.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generated, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
