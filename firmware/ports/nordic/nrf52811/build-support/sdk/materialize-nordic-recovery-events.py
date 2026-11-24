#!/usr/bin/env python3
"""Compact GCC section traversal; preserve native SoftDevice event behavior.

Request/state observers and private SVC handlers are empty in this Recovery.
Linker assertions refuse a later build registering one. SDK GCC sections are
already contiguous and ordered; BLE/SoC fetching, callbacks and errors remain.
"""
import argparse
import hashlib
from pathlib import Path
import runpy
import re

HASHES = {
    'nrf_sdh.c': '8f485109ea71b3f7c4455fdd2d24a820e75bcfb959e7e37fad6a62a302dbf856',
    'nrf_sdh_ble.c': 'cf2aec65411af6cb56cc37990dcb1b0d02d92daa0bd613019bc2bf4ede33e48a',
    'nrf_sdh_soc.c': 'e1188748a1d94c9e8803106207e3fad6b27a0296f93efdf251d3b64ea3c2c25b',
    'nrf_svc_handler.c': '928ff5ea2d63f864439630721e841769319dc4ff48508a351def3aabbaa2632e',
}


def materialize(path):
    payload = path.read_bytes()
    if hashlib.sha256(payload).hexdigest() != HASHES[path.name]:
        raise ValueError('Recovery event SDK source hash mismatch: ' + path.name)
    text = payload.decode().replace('\r\n', '\n')
    replace = runpy.run_path(Path(__file__).with_name(
        'materialize-nordic-dfu-validation.py'))['replace_function']
    if path.name == 'nrf_sdh.c':
        text = replace(text, 'static ret_code_t sdh_request_observer_notify(', '''
static ret_code_t sdh_request_observer_notify(nrf_sdh_req_evt_t req)
{
    (void)req;
    return NRF_SUCCESS;
}''')
        text = replace(text, 'static void sdh_state_observer_notify(', '''
static void sdh_state_observer_notify(nrf_sdh_state_evt_t evt)
{
    (void)evt;
}''')
        text = replace(text, 'void nrf_sdh_evts_poll(void)', '''
void nrf_sdh_evts_poll(void)
{
    nrf_sdh_stack_observer_t const *p = (void const *)NRF_SECTION_START_ADDR(sdh_stack_observers);
    nrf_sdh_stack_observer_t const *end = (void const *)NRF_SECTION_END_ADDR(sdh_stack_observers);
    for (; p < end; ++p) p->handler(p->p_context);
}''')
    elif path.name == 'nrf_svc_handler.c':
        text = replace(text, 'void nrf_svc_handler_c(uint32_t* p_svc_args)\n{', '''
void nrf_svc_handler_c(uint32_t* p_svc_args)
{
    /* Same native result for every SVC when the private registry is empty. */
    p_svc_args[0] = NRF_ERROR_SVC_HANDLER_MISSING;
}''')
    else:
        kind = 'ble' if path.name == 'nrf_sdh_ble.c' else 'soc'
        typename = 'nrf_sdh_' + kind + '_evt_observer_t'
        argument = 'p_ble_evt' if kind == 'ble' else 'evt_id'
        text = replace(text, '        for (nrf_section_iter_init(', f'''
        for ({typename} const *p_observer =
                 (void const *)NRF_SECTION_START_ADDR(sdh_{kind}_observers);
             p_observer < ({typename} const *)(void const *)NRF_SECTION_END_ADDR(sdh_{kind}_observers);
             ++p_observer)
        {{
            p_observer->handler({argument}, p_observer->p_context);
        }}''')
        text = text.replace('        nrf_section_iter_t  iter;\n', '')
    text = re.sub(r'NRF_SECTION_SET_DEF\((\w+),\s*(\w+),\s*\w+\);',
                  r'NRF_SECTION_DEF(\1, \2);', text)
    return ('/* Generated fixed nRF52811 Recovery event adapter. */\n'
            '#if !defined(__GNUC__)\n#error "Requires contiguous GCC SDK sections"\n#endif\n' + text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--check-only', action='store_true')
    args = parser.parse_args()
    rendered = materialize(args.source)
    if not args.check_only:
        if args.output is None:
            parser.error('--output is required')
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding='utf-8', newline='\n')
