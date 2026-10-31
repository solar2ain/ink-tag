from __future__ import annotations


# Runtime executable identities, not PCB/product names or peripheral profiles.
# Values are (shared transport driver, existing signed package target).
FIRMWARE_FAMILIES = {
    "tlsr8359_hanshow": ("telink", "tlsr8359_hanshow"),
    "gr5331_zkel": ("goodix", "gr5331_zkc21v"),
    "gr551x_zkel": ("goodix", "gr551x_zkc42v"),
    "gr5513_zkel_internal": ("goodix", "gr5513_zkel_internal"),
    "om6626_zkcxxb": ("onmicro", "om6626_zkcxxb"),
    "om6626_hsela": ("onmicro", "om6626_hsela"),
}


def firmware_family_target(identity: str) -> str | None:
    family = FIRMWARE_FAMILIES.get(identity.strip().lower())
    return family[1] if family else None


def canonical_board_id(value: str) -> str:
    return value.strip().lower()


def board_ids_equivalent(left: str, right: str) -> bool:
    return canonical_board_id(left) == canonical_board_id(right)


def display_build_target(value: str) -> str:
    """User-facing name only; never use this projection for wire admission."""
    aliases = {
        'gr5331_zkc21v': 'gr5331_zkel', 'gr551x_zkc42v': 'gr551x_zkel',
        'gr551x_zkc42vm': 'gr551x_zkel', 'gr5513_zkel_internal': 'gr551x_zkel_internal',
        'om6626_zkc': 'om6626_zkel', 'om6626_zkcxxb': 'om6626_zkel',
        'om6626_zkc21b': 'om6626_zkel', 'om6626_zkc21v': 'om6626_zkel',
        'om6626_zkc42b': 'om6626_zkel', 'om6626_hsela': 'om6626_hsel',
        'tlsr8359_hanshow': 'tlsr8359_hsel', 'aesl22_nrf': 'nrf52811_aesl',
        'aesl29_da': 'da14585_aesl', 'aesl42_da': 'da14585_aesl',
    }
    return aliases.get(canonical_board_id(value), value)
