"""Explicit UTC references; never changes the computer clock or a device."""
from __future__ import annotations

import asyncio
import math
import secrets
import time
from dataclasses import dataclass
from email.utils import parsedate_to_datetime
from urllib.parse import urlsplit
from urllib.request import Request, urlopen

MAX_OFFSET_SECONDS = 86400.0
TIME_SOURCES = (
    "https://www.apple.com/",
    "https://www.google.com/generate_204",
)


def validate_offset(value: float) -> None:
    if (isinstance(value, bool) or not isinstance(value, (int, float))
            or not math.isfinite(value) or abs(value) > MAX_OFFSET_SECONDS):
        raise ValueError("offset seconds must be finite and within -86400..86400")


@dataclass(frozen=True)
class NetworkTime:
    utc_seconds: float
    monotonic_seconds: float
    uncertainty_seconds: float
    sources: tuple[dict, ...]

    def now(self) -> float:
        elapsed = time.monotonic() - self.monotonic_seconds
        if not 0 <= elapsed <= 10:
            raise ValueError("network time reference expired; no SET_TIME sent")
        return self.utc_seconds + elapsed


def _sample(url: str) -> dict:
    request = Request(url + "?inktag_time=" + secrets.token_hex(8), method="HEAD",
                      headers={"Cache-Control": "no-cache", "Pragma": "no-cache"})
    started = time.monotonic()
    # Default urllib TLS verification is deliberately retained. No user-supplied
    # URL, credential, private identity or location is sent to the time sources.
    with urlopen(request, timeout=3) as response:
        ended = time.monotonic()
        final = urlsplit(response.url)
        if final.scheme != "https" or final.hostname != urlsplit(url).hostname:
            raise ValueError("unexpected HTTPS time-source redirect")
        dates = response.headers.get_all("Date", [])
        if len(dates) != 1 or response.headers.get("Age", "0") != "0":
            raise ValueError("HTTPS time source returned missing/ambiguous/cached Date")
        date = parsedate_to_datetime(dates[0])
        if date.tzinfo is None or not 946684800 <= date.timestamp() < 4102444800:
            raise ValueError("HTTPS time source returned an invalid UTC date")
    rtt = ended - started
    if not 0 <= rtt <= 2:
        raise ValueError("HTTPS time-source round trip exceeds two seconds")
    # HTTP Date has one-second resolution, not NTP precision. Anchor at receipt
    # and extrapolate with monotonic time, not the possibly incorrect wall clock.
    return {"source": url, "dateUtc": date.isoformat(), "roundTripSeconds": rtt,
            "utcAtReceipt": date.timestamp() + .5 + rtt / 2,
            "monotonicAtReceipt": ended, "uncertaintySeconds": .5 + rtt / 2}


async def network_time() -> NetworkTime:
    samples = tuple([await asyncio.to_thread(_sample, url) for url in TIME_SOURCES])
    captured = time.monotonic()
    estimates = [s["utcAtReceipt"] + captured - s["monotonicAtReceipt"] for s in samples]
    spread = max(estimates) - min(estimates)
    if spread > 2:
        raise ValueError("independent HTTPS time sources disagree by more than two seconds")
    return NetworkTime(sum(estimates) / len(estimates), captured,
                       max(s["uncertaintySeconds"] for s in samples) + spread / 2,
                       samples)
