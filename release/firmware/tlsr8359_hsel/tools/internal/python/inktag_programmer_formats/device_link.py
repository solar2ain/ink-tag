from __future__ import annotations

from urllib.parse import quote, urlsplit


INKTAG_WEB_URL = "https://solar2ain.github.io/ink-tag/"


def build_device_link(bluetooth_name: str | None = None) -> str:
    """Build the managed public WebUI link for one advertised BLE name."""
    if not bluetooth_name:
        return INKTAG_WEB_URL
    return f"{INKTAG_WEB_URL}?ble={quote(bluetooth_name, safe='')}"


def is_managed_device_link(value: str) -> bool:
    """Return true for the root InkTag link with at most one ``ble`` hint."""
    try:
        candidate = urlsplit(value.strip())
        base = urlsplit(INKTAG_WEB_URL)
    except ValueError:
        return False
    if (
        candidate.scheme.lower() != base.scheme
        or candidate.netloc.lower() != base.netloc
        or candidate.path.rstrip("/") != base.path.rstrip("/")
        or candidate.fragment
    ):
        return False
    if not candidate.query:
        return True
    fields = candidate.query.split("&")
    return len(fields) == 1 and fields[0].startswith("ble=")
