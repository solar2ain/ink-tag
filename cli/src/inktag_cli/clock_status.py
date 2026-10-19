"""Single read-only clock sample; mirrors WebUI clockDiagnostic, not a drift test."""
from datetime import datetime, timedelta, timezone
import math


def clock_status(device: dict[str, int], before: datetime, after: datetime,
                     before_ns: int, after_ns: int) -> dict:
    utc, minutes, week = device['utcTimestamp'], device['timezoneMinutes'], device['weekStart']
    rtt_ms = (after_ns - before_ns) / 1_000_000
    if not 0 <= utc <= 0xffffffff or not -840 <= minutes <= 840 or not 0 <= week <= 6 or rtt_ms < 0:
        raise ValueError('Invalid clock diagnostic sample')
    midpoint = (before.timestamp() + after.timestamp()) / 2
    if not all(map(math.isfinite, (midpoint, rtt_ms))):
        raise ValueError('Invalid host clock sample')
    stable = abs((after - before).total_seconds() * 1000 - rtt_ms) <= 250
    valid = 946684800 <= utc < 4102444800
    at = datetime.fromtimestamp(midpoint, timezone.utc)
    return {
        'readOnly': True, 'deviceUtcTimestamp': utc,
        'deviceLocalTime': datetime.fromtimestamp(utc, timezone(timedelta(minutes=minutes))).isoformat(timespec='milliseconds'),
        'timezoneMinutes': minutes, 'weekStart': week,
        'hostLocalTime': at.astimezone().isoformat(timespec='milliseconds'),
        'hostUtcBefore': before.isoformat(timespec='milliseconds'),
        'hostUtcAfter': after.isoformat(timespec='milliseconds'),
        'hostUtcMidpoint': at.isoformat(timespec='milliseconds'),
        'roundTripMilliseconds': round(rtt_ms, 3),
        'offsetFromHostSeconds': round(utc - midpoint, 3) if stable and valid else None,
        'uncertaintySeconds': round(1 + rtt_ms / 2000, 3),
        'timeResolutionSeconds': 1, 'hostClockStable': stable, 'deviceTimeValid': valid,
    }
