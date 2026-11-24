from __future__ import annotations

from dataclasses import replace

from .models import ActivationStatus, ImageStoreInfo, ImageStoreItem, ScreenConfig


def constrain_image_store_for_client(
    info: ImageStoreInfo | None,
    items: list[ImageStoreItem],
    activation: ActivationStatus,
    screen: ScreenConfig | None,
) -> tuple[ImageStoreInfo | None, list[ImageStoreItem]]:
    """Preserve physical capacity and every valid record, independent of edition."""
    stable = sorted(items, key=lambda item: item.id)
    if info is not None:
        info = replace(info, occupied_ids=info.occupied_ids if info.fixed_slot_count else tuple(item.id for item in stable))
    return info, stable
