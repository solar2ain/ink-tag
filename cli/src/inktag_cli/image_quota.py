from __future__ import annotations

from dataclasses import replace

from .models import ActivationStatus, ImageStoreInfo, ImageStoreItem, ScreenConfig

FREE_COMPACT_MAX_EDGE_EXCLUSIVE = 300
FREE_COMPACT_IMAGE_LIMIT = 10
FREE_NORMAL_IMAGE_LIMIT = 4


def free_image_record_limit(screen: ScreenConfig | None) -> int:
    if screen is None or screen.visible_width <= 0 or screen.visible_height <= 0:
        return FREE_NORMAL_IMAGE_LIMIT
    if max(screen.visible_width, screen.visible_height) >= FREE_COMPACT_MAX_EDGE_EXCLUSIVE:
        return FREE_NORMAL_IMAGE_LIMIT
    return FREE_COMPACT_IMAGE_LIMIT


def constrain_image_store_for_client(
    info: ImageStoreInfo | None,
    items: list[ImageStoreItem],
    activation: ActivationStatus,
    screen: ScreenConfig | None,
    *,
    tiered_capacity: bool = True,
) -> tuple[ImageStoreInfo | None, list[ImageStoreItem]]:
    """Defend clients against stale firmware; firmware remains authoritative."""
    stable = sorted(items, key=lambda item: item.id)
    if info is not None:
        info = replace(info, occupied_ids=info.occupied_ids if info.fixed_slot_count else tuple(item.id for item in stable))
    if activation.active or not tiered_capacity:
        return info, stable
    limit = free_image_record_limit(screen)
    visible = stable[:limit]
    if info is None:
        return None, visible  # Read-only listing works; capacity stays unknown.
    saved = len(visible)
    remaining = max(0, limit - saved)
    compatible = sum(item.compatible for item in visible)
    playable = sum(item.compatible and item.content_type == "image" for item in visible)
    return replace(
        info,
        fit=min(info.fit, remaining),
        contiguous_fit=min(info.contiguous_fit, remaining),
        saved=saved,
        compatible=compatible,
        playable=playable,
        incompatible=saved - compatible,
    ), visible
