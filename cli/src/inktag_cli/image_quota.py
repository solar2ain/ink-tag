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
    info: ImageStoreInfo,
    items: list[ImageStoreItem],
    activation: ActivationStatus,
    screen: ScreenConfig | None,
    *,
    tiered_capacity: bool = True,
) -> tuple[ImageStoreInfo, list[ImageStoreItem]]:
    """Defend clients against stale firmware; firmware remains authoritative."""
    stable = sorted(items, key=lambda item: item.id)
    if activation.active or not tiered_capacity:
        return info, stable
    limit = free_image_record_limit(screen)
    visible = stable[:limit]
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
