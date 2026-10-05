"""TLSR v5 host package contract. Full migration precedes ordinary v5 OTA."""

TLSR_APP_LIMIT = 0x38000
TLSR_LAYOUT = "single-with-ble-recovery-v5"
TLSR_LAYOUT_SIGNATURE = 0x35524C54
TLSR_GUI_ASSET_BANKS = [
    {"start": 0x38000, "size": 0x4000},
    {"start": 0x3C000, "size": 0x4000},
]
TLSR_IMAGE_EXTENTS = [{"start": 0x54000, "size": 0x2B000}]
TLSR_CONDITIONAL_IMAGE_EXTENTS = {
    "hs_nebular420": [{"start": 0x80000, "size": 0x80000}],
}


def validate_tlsr_data_layout(application: dict) -> None:
    """Reject absent/mixed layouts; never use host metadata to migrate a device."""
    if (
        application.get("marker_offset") != 8
        or application.get("image_store_reserved_logical_sectors") != 0
        or application.get("gui_asset_banks") != TLSR_GUI_ASSET_BANKS
        or application.get("image_store_extents") != TLSR_IMAGE_EXTENTS
        or application.get("conditional_image_store_extents") != TLSR_CONDITIONAL_IMAGE_EXTENTS
    ):
        raise ValueError("Telink v5 data layout does not match the manifest; Full migration is required")
