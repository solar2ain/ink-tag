from __future__ import annotations


# Runtime executable identities, not PCB/product names or peripheral profiles.
# Values are (shared transport driver, existing signed package target).
FIRMWARE_FAMILIES = {
    "tlsr8359_hanshow": ("telink", "tlsr8359_hanshow"),
    "gr5331_zkel": ("goodix", "gr5331_zkc21v"),
    "gr551x_zkel": ("goodix", "gr551x_zkc42v"),
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
