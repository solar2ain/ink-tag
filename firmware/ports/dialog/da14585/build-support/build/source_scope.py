"""DA source admission by implementation module, never by protocol contents."""
from pathlib import Path, PurePosixPath
import re

SHARED_COMMON_FILES = frozenset({
    'compat/common/protocol/inktag_packbits.c',
    'compat/common/protocol/inktag_packbits.h',
})


def source_path(root: Path, name: str) -> Path:
    """Use the InkTag checkout's shared input or the standalone export copy."""
    checkout = root.parents[3] if len(root.parents) > 3 else None
    if checkout is not None and root == checkout / 'firmware/ports/dialog/da14585':
        if name in SHARED_COMMON_FILES:
            return checkout / 'firmware/common' / Path(name).relative_to('compat/common')
        if name in {'boards/boards.json', 'boards/da14585_aesl.h'}:
            return checkout / 'firmware' / name
    return root / name


# Fixed NFC closure in main b82079f4. Includes headers and flat materialized
# copies; Type2/Type4/GT names cannot be inferred from an NFC substring.
NFC_MODULE_FAMILIES = {
    'ndef': ('inktag_ndef',),
    'phone-and-binding': ('inktag_nfc_binding', 'inktag_nfc_command',
                          'inktag_nfc_link', 'inktag_nfc_volatile_command'),
    'type2': ('inktag_type2_uri_store',),
    'type4': ('inktag_type4_apdu', 'inktag_type4_transport', 'inktag_type4_ndef'),
    'fmsc': ('inktag_nfc_fmsc',),
    'gt': ('inktag_gt_mirror', 'inktag_gt_pipe', 'inktag_gt_stream'),
}
NFC_IMPLEMENTATION_MODULES = frozenset(
    module for modules in NFC_MODULE_FAMILIES.values() for module in modules)

PRO_INPUT_DEFINITIONS = (
    'inktag_input_action_allowed', 'inktag_button_bindings_migrate_default',
    'inktag_button_binding', 'inktag_button_simulate',
    'epd_device_settings_valid', 'inktag_device_settings_valid_pro',
    'inktag_input_simulation_clear', 'inktag_input_simulation_begin',
    'inktag_input_simulation_cancel', 'inktag_input_simulation_finish',
    'inktag_input_simulation_payload',
)
PRIVATE_BENCH_DEFINITIONS = (
    'user_app_sleep_probe_enter', 'user_app_sleep_probe_exit', 'inktag_da_default_qr',
)


def free_implementation_error(name, data):
    """Reject positive custom-input bodies, not stock runtime or wire IDs."""
    path = PurePosixPath(name)
    if name == 'build-support/sdk/materialize-dialog-port.py' and re.search(
            rb'add_argument\(\s*[\'\"]--(?:packbits-candidate|extended-sleep-candidate|a33-panel-candidate|connection-temperature-candidate|named-device-qr-candidate|gui-cache-lines)[\'\"]', data):
        return 'Private DA candidate build option'
    if not path.parts or path.parts[0] not in {'app', 'boards', 'compat'} or path.suffix not in {'.c', '.h', '.inc'}:
        return None
    for symbol in PRO_INPUT_DEFINITIONS:
        pattern = rb'\b' + symbol.encode('ascii') + rb'\s*\([^;{}]*\)\s*\{'
        if re.search(pattern, data):
            return 'Pro custom-input implementation: ' + symbol
    for symbol in PRIVATE_BENCH_DEFINITIONS:
        pattern = rb'\b' + symbol.encode('ascii') + rb'\s*\([^;{}]*\)\s*\{'
        if re.search(pattern, data):
            return 'Private DA bench implementation: ' + symbol
    return None


def implementation_kind(name):
    path = PurePosixPath(name)
    if 'common_pro' in {p.lower() for p in path.parts} or path.stem.lower().endswith('_pro'):
        return 'Pro'
    if path.parts and path.parts[0].lower() in {'app', 'boards', 'compat'} and path.stem.lower() == 'inktag_input_simulation':
        return 'Pro custom-input'
    if path.parts and path.parts[0].lower() in {'app', 'boards', 'compat'} and path.stem.lower() == 'inktag_da_ota_public_key':
        return 'Retired publisher verification'
    if path.parts and path.parts[0].lower() in {'app', 'boards', 'compat'} and (
            path.stem.lower() in NFC_IMPLEMENTATION_MODULES or
            re.search(r'(^|_)(nfc|ndef|fmsc)(_|\.)', path.name, re.I)):
        return 'NFC'
    return None
