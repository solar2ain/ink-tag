"""Resolve a selected TLSR preset; historical part records are advisory.

This is not connected-device admission. Fresh supported JEDEC/status/UID,
ordinary consent, preservation and signed-package checks still precede erase.
"""
from tlsr_flash_jedec import FLASH_BYTES

DEFAULT_UART = dict(writeBaud=460800, readBaud=460800, activationMs=30000, clockMHz=0)


def resolve_contract(entry: dict, selector: int) -> tuple[dict, list[str]]:
    if entry.get('port') != 'telink/tlsr8359' or type(selector) is not int or not 1 <= selector <= 255:
        raise ValueError('Config must select a compatible TLSR8359 backend')
    historical = entry.get('firstFlash')
    if not isinstance(historical, dict):
        historical = {}
    contract = dict(format='bounded-flash-window-bin', startAddress=0, imageSizeBytes=524288,
                    selectorAddress=0x7F100, selectorId=selector)
    warnings = []
    if not historical or any(historical.get(k) != v for k, v in contract.items() if k != 'format') or historical.get('format') not in ('full-flash-bin', 'bounded-flash-window-bin'):
        warnings.append('Historical Flash/layout registration is absent or differs. Using the selected Config and signed lower512-KiB layout; this is not exact-board qualification.')
    jedec = historical.get('flashJedecId')
    contract['flashJedecId'] = jedec.upper() if isinstance(jedec, str) and jedec.upper() in FLASH_BYTES else None
    if contract['flashJedecId'] is None:
        warnings.append('No usable historical Flash model. A fresh supported part and explicit model y/N confirmation are required; no capacity or protection recipe is guessed.')
    transport = historical.get('uartSws')
    valid = (isinstance(transport, dict) and set(transport) == set(DEFAULT_UART)
             and all(type(v) is int for v in transport.values())
             and transport['writeBaud'] in (460800, 576000, 921600)
             and transport['readBaud'] in (460800, 921600)
             and 1000 <= transport['activationMs'] <= 120000 and transport['clockMHz'] in (0, 24, 29))
    contract['uartSws'] = dict(transport) if valid else dict(DEFAULT_UART)
    if not valid:
        warnings.append('No usable historical UART/SWS settings. Using family defaults: write/read460800, automatic clock,30s activation; not a measured fixture qualification.')
    capacity = entry.get('physicalFlashSizeBytes') or entry.get('verifiedFlashWindowSizeBytes')
    if type(capacity) is not int or capacity < 524288:
        warnings.append('Historical writable512-KiB evidence is absent. Fresh supported Flash capacity must cover the complete prepared image before erase.')
    return contract, warnings
