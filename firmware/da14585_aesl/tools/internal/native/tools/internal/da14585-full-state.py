"""Pure DA Full initialization codec. No old-journal reader or device access.

Only physical Full preparation uses this module. OTA continues to use the
unchanged on-device highest-confirmed-epoch admission. Verification of the
selected App signature and Recovery integrity belongs to the caller.
"""
import struct
import re
import zlib

PAGE_BYTES = 4096
RESET_TRACE_ADDRESS = 0x07FD7398
RESET_TRACE_BYTES = 32
RESET_TRACE_SEAL = 0x44525431
LAYOUT = int.from_bytes(b'DAV1', 'little')
PIN_CAPABILITY = b'INKTAG-DA-PIN-DEFAULT-V1\0'
PIN_COMMON_CAPABILITY = b'INKTAG-DA-PIN-COMMON-V1\0'
GPIO_PADS = frozenset((port << 4) | pin for port, maximum in enumerate((7, 5, 9, 7))
                      for pin in range(maximum + 1))
PROTECTED_PADS = frozenset((0x00, 0x03, 0x05, 0x06, 0x14, 0x15, 0x22))


def diagnostic_cleanup_profile(application):
    """Existing signed DA SDK6 UNINIT32 profile, never a user-supplied address.

    Caller must authenticate App first. These existing native constants and
    DAV1 memory ownership identify the supported profile, not a new signature.
    Unknown Apps can be inspected but cannot enter the physical Full writer.
    """
    size = struct.unpack_from('<I', application, 4)[0]
    raw = application[64:64 + size - 96] if 96 < size <= len(application) - 64 else b''
    supported = all(struct.pack('<I', value) in raw for value in (RESET_TRACE_ADDRESS, RESET_TRACE_SEAL))
    return {'profile': 'DA-SDK6-UNINIT32-v1', 'supported': supported,
            'address': RESET_TRACE_ADDRESS, 'bytes': RESET_TRACE_BYTES,
            'hostMustClearBeforeStart': True, 'hardwareQualified': False}


def cleanup_scope(capacity=None, verified=False):
    if capacity is not None and (type(capacity) is not int or capacity < 0x40000):
        raise ValueError('Unknown/short physical Flash capacity')
    return {'configuredFlashWindowBytes': 0x40000,
            'writeRanges': [[0, 0x39000], [0x3B000, 0x40000]],
            'protectedFactoryRanges': [[0x39000, 0x3B000]],
            'userSettingsTimeLabelReplayAndPinOverridesReinitialized': verified,
            'imageStoreBytes': 0x11000, 'imageStoreReadbackVerified': verified,
            'diagnosticHistory': {'persistentFlashLogImplemented': False,
                'healthZeroIsClearanceEvidence': False,
                'ramTraceAddress': RESET_TRACE_ADDRESS, 'ramTraceBytes': RESET_TRACE_BYTES,
                'ramTraceHeldZeroReadbackRequired': True, 'ramTraceClearVerified': verified},
            'outsideConfiguredFlash': {'start': 0x40000, 'end': capacity,
                'bytes': None if capacity is None else capacity - 0x40000,
                'action': 'preserved, not erased or independently read back'},
            'allPhysicalFlashErased': False, 'otpOrSecurityCountersWritten': False,
            'physicalWriterQualified': False}


def pin_capabilities(application):
    """Caller must authenticate the exact native App first; never inspect a manifest."""
    size = struct.unpack_from('<I', application, 4)[0]
    if size <= 96 or 64 + size > len(application):
        raise ValueError('Pin capability App raw bounds')
    raw = application[64:64 + size - 96]
    counts = (raw.count(PIN_CAPABILITY), raw.count(PIN_COMMON_CAPABILITY))
    if any(count > 1 for count in counts):
        raise ValueError('Ambiguous App pin capability marker')
    return counts


def pin_defaults_capable(application):
    return any(pin_capabilities(application))


def require_pin_support(application, wire):
    legacy, common = pin_capabilities(application)
    if not valid_pin_defaults(wire) or not (legacy or common):
        raise ValueError('App lacks configured pin defaults or pin block is invalid')
    if (wire[2] != 0 or wire[4] == 255) and not common:
        raise ValueError('App lacks COMMON pin semantics for EN polarity/display disable')


def valid_pin_defaults(wire):
    if len(wire) != 16 or wire[:2] != b'\x01\0' or wire[3] or \
            wire[2] & ~0x40 or wire[11:] != b'\xff'*5:
        return False
    if not (all(pin != 255 for pin in wire[4:10]) or wire[4:11] == b'\xff'*7):
        return False
    if wire[10] == 255 and wire[2]:
        return False
    active = [pin for pin in wire[4:11] if pin != 255]
    return all(pin in GPIO_PADS - PROTECTED_PADS for pin in active) and len(set(active)) == len(active)


def recovery_pages(epoch):
    """Initialize both native pages: one CONFIRMED record, erased spare.

    This is exactly the existing recovery_record_t, not a new ABI. Unused
    bytes retain the FF initialization used by the shipping C append path.
    No previous epoch, generation, settings or journal bytes are consulted.
    """
    if type(epoch) is not int or not 0 <= epoch <= 0xFFFF:
        raise ValueError('DA Full epoch outside existing Recovery policy')
    prefix = struct.pack('<IIIIBBHII', 0x314A5249, LAYOUT, 1, 0,
                         3, 1, 0xFFFF, epoch, 0xFFFFFFFF)
    record = prefix + struct.pack('<I', zlib.crc32(prefix))
    return record + b'\xff' * (PAGE_BYTES - len(record)), b'\xff' * PAGE_BYTES


def verify_recovery_pages(data, epoch):
    expected_a, expected_b = recovery_pages(epoch)
    if data[0x37000:0x38000] != expected_a or data[0x3B000:0x3C000] != expected_b:
        raise ValueError('Full Recovery initialization does not match selected App epoch')
    return {'state': 'confirmed', 'compatibilityEpoch': epoch,
            'controlA': 0x37000, 'controlB': 0x3B000,
            'bothPagesInitialized': True, 'oldFloorCompared': False}


def fnv1a(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def verify_settings(data):
    """Admit only freshly seeded v1 defaults, never retained user records."""
    settings = data[0x3C000:0x3E000]
    if settings[:16] != struct.pack('<IIII', 0x31505349, 1, 0xFFFFFFFE, 0xFFFFFFFF):
        raise ValueError('Full Settings initialization header')
    cursor, records = 16, {}
    while cursor < PAGE_BYTES and settings[cursor:cursor+4] != b'\xff' * 4:
        magic, kind, length, checksum = struct.unpack_from('<IHHI', settings, cursor)
        end = cursor + 12 + ((length + 3) & ~3)
        if magic != 0x31525349 or kind not in (1, 3, 6, 7) or kind in records or end + 4 > PAGE_BYTES:
            raise ValueError('Full contains old/unknown user Settings records')
        payload = settings[cursor+12:cursor+12+length]
        if fnv1a(struct.pack('<HH', kind, length) + payload) != checksum or \
                settings[cursor+12+length:end] != b'\xff' * (-length % 4) or \
                settings[end:end+4] != struct.pack('<I', 0x31435349):
            raise ValueError('Full Settings checksum/commit/padding')
        records[kind] = payload
        cursor = end + 4
    if settings[cursor:] != b'\xff' * (len(settings) - cursor):
        raise ValueError('Full Settings cleanup is incomplete')
    capable = pin_defaults_capable(data[0x4000:0x1C000])
    order = [1, 3, 7] if capable else [3, 7]
    if list(records) not in (order, order + [6]):
        raise ValueError('Full Settings default record order')
    pins = records.get(1)
    if pins is not None and not valid_pin_defaults(pins):
        raise ValueError('Full pin default is not a qualified common pin16')
    if pins is not None:
        require_pin_support(data[0x4000:0x1C000], pins)
    screen = records.get(3, b'')
    if len(screen) != 28 or screen[:5] != b'SP\x01\x11\x01' or \
            screen[21:24] != b'\0' * 3 or struct.unpack_from('<I', screen, 24)[0] != fnv1a(screen[:24]):
        raise ValueError('Full screen default is not exact SP/version1')
    if records.get(7) != bytes((2, 1, 1, 0, 0, 0, 0, 0, 0)):
        raise ValueError('Full ID7 preference defaults are missing or retained')
    label = records.get(6)
    if label is not None and (len(label) != 36 or label[:8] != b'DIT1\x01\x0b\0\0' or
            not re.fullmatch(b'InkTag [0-9A-F]{4}', label[8:19]) or
            label[19:32] != b'\0' * 13 or struct.unpack_from('<I', label, 32)[0] != fnv1a(label[:32])):
        raise ValueError('Full contains retained/custom label instead of derived default')
    return {'preferencesSeeded': True, 'screenSeeded': True,
            'defaultLabelSeeded': label is not None, 'oldUserRecordsRetained': False,
            'screenWireHex': screen[4:21].hex(), 'screenRecordHex': screen.hex(),
            'pinWireHex': None if pins is None else pins.hex(), 'pinDefaultsCapable': capable}


def verify_assets_and_first_display(data):
    assets = data[0x36000:0x37000] + data[0x3E000:0x40000]
    magic, schema, size, crc = struct.unpack_from('<IIII', assets)
    if (magic, schema, size) != (0x44464B49, 1, 10500) or \
            zlib.crc32(assets[16:10516]) != crc or assets[10516:] != b'\xff' * (12288-10516):
        raise ValueError('Full packaged read-only font/resource initialization')
    first = struct.pack('<II', 0x31444649, 0xCEBBB9B6)
    if data[0x35000:0x36000] != first + b'\xff' * (4096-8):
        raise ValueError('Full first-display/request initialization')
    return {'fixedAssetsVersion': 1, 'userFontsSupported': False,
            'firstDisplayMarker': 'IFD1', 'recoveryRequestCleared': True}
