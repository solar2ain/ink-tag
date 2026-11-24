"""Maintained identity mapping for the existing nRF52832 single backend.

Pure host data, no device access and no new selector/record. Keep compatible
with exported host tools: never require private firmware headers at runtime.
The owner tests bind this row to the concentrated family header; root owns
the corresponding aggregate registry row.
"""
from board_config import ConfigurationError

FIRMWARE_FAMILY = 'nrf52832_priceetag'
BOARD_ID = 'nrf52832_priceetag'  # Original native string, NOT numeric zero.
BOARD_MARKING = 'PriceETag V1.7'  # Exact operator label corrected by the owner, 2026-10-23.
PRODUCT_NAME = 'PriceETag'


def resolve_board_id(board_id):
    if type(board_id) is not str or board_id != BOARD_ID:
        raise ConfigurationError('Unknown/foreign Nordic board_id; no backend selected')
    return dict(firmware_family=FIRMWARE_FAMILY, board_id=BOARD_ID,
                board_id_kind='native-single-backend-identity',
                product_name=PRODUCT_NAME)


def resolve_marking(board_marking):
    if type(board_marking) is not str or board_marking != BOARD_MARKING:
        raise ConfigurationError('Unknown board_marking; no guessed Nordic backend')
    return dict(**resolve_board_id(BOARD_ID), board_marking=BOARD_MARKING,
                board_marking_verified=True,
                board_marking_status='owner-confirmed-pcb-label-2026-10-23')


def require_base_family(configured_family, base_family):
    if configured_family != FIRMWARE_FAMILY or base_family != configured_family:
        raise ConfigurationError('Configuration/base firmware-family mismatch; no device accessed')
