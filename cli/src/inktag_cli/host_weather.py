"""Public weather data adapter. No device identity or account data leaves the host."""
from __future__ import annotations

from datetime import datetime, timedelta
import json
import math
from typing import Any
from urllib.parse import urlencode
from urllib.request import Request, build_opener, HTTPRedirectHandler, HTTPSHandler

PROVIDER_URL = "https://api.open-meteo.com/v1/forecast"
MAX_RESPONSE = 2 * 1024 * 1024


def find_locations(name: str, language: str = "en") -> list[dict]:
    if not isinstance(name, str) or not 2 <= len(name.strip()) <= 80 or any(ord(c) < 32 for c in name):
        raise ValueError("Location search needs 2..80 printable characters")
    if language not in {"en", "zh"}: raise ValueError("Location language must be en or zh")
    request = Request("https://geocoding-api.open-meteo.com/v1/search?"+urlencode({"name": name.strip(), "count": 5, "language": language, "format": "json"}),
                      headers={"Accept": "application/json", "User-Agent": "InkTag-Weather/1"})
    with build_opener(_NoRedirect(), HTTPSHandler()).open(request, timeout=12) as response:
        payload = response.read(MAX_RESPONSE+1)
    if len(payload) > MAX_RESPONSE: raise ValueError("Location response is too large")
    try:
        values = json.loads(payload).get("results", [])
        return [{"name": str(item["name"])[:80], "region": str(item.get("admin1", ""))[:80], "country": str(item.get("country", ""))[:80],
                 "latitude": number(item["latitude"], "latitude", -90, 90), "longitude": number(item["longitude"], "longitude", -180, 180),
                 "timezone": str(item.get("timezone", "auto"))[:64]} for item in values[:5]]
    except (KeyError, TypeError, AttributeError, ValueError) as error:
        raise ValueError("Invalid location search response") from error


def number(value: Any, label: str, low: float = -100, high: float = 100) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not low <= value <= high:
        raise ValueError(f"Invalid weather {label}")
    return float(value)


def source_options(source: dict) -> dict:
    if not isinstance(source, dict):
        raise ValueError("Weather source must be a table")
    if set(source) - {"provider", "latitude", "longitude", "label", "timezone"}:
        raise ValueError("Unknown weather source option")
    if source.get("provider") != "open-meteo":
        raise ValueError("Only the open-meteo weather provider is supported")
    label = source.get("label", "Weather")
    zone = source.get("timezone", "auto")
    if not isinstance(label, str) or not 1 <= len(label) <= 32 or any(ord(c) < 32 for c in label):
        raise ValueError("Weather location label must be 1..32 printable characters")
    if not isinstance(zone, str) or not 1 <= len(zone) <= 64 or any(ord(c) < 32 for c in zone):
        raise ValueError("Invalid weather timezone")
    return {"provider": "open-meteo", "latitude": number(source.get("latitude"), "latitude", -90, 90),
            "longitude": number(source.get("longitude"), "longitude", -180, 180), "label": label, "timezone": zone}


class _NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise ValueError("Weather provider redirected; request refused")


def fetch_weather(source: dict) -> dict:
    source = source_options(source)
    query = {key: source[key] for key in ("latitude", "longitude", "timezone")}
    query.update(current="temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m",
                 hourly="temperature_2m,relative_humidity_2m,precipitation_probability",
                 daily="weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max",
                 forecast_days=5, temperature_unit="celsius", wind_speed_unit="kmh", precipitation_unit="mm")
    request = Request(PROVIDER_URL + "?" + urlencode(query), headers={"Accept": "application/json", "User-Agent": "InkTag-Weather/1"})
    # HTTPS certificate verification stays enabled; arbitrary/redirected endpoints
    # and user/device credentials are deliberately not accepted by this adapter.
    with build_opener(_NoRedirect(), HTTPSHandler()).open(request, timeout=12) as response:
        payload = response.read(MAX_RESPONSE + 1)
    if len(payload) > MAX_RESPONSE:
        raise ValueError("Weather response is too large")
    try:
        return normalize_weather(json.loads(payload), source["label"])
    except (KeyError, TypeError, IndexError, json.JSONDecodeError, UnicodeDecodeError) as exc:
        raise ValueError("Incomplete or invalid weather response") from exc


def normalize_weather(raw: dict, label: str) -> dict:
    """Keep API-local ISO times, avoiding a dependency on host timezone settings."""
    if not isinstance(raw, dict) or any(not isinstance(raw.get(key), dict) for key in ("current", "hourly", "daily", "current_units")):
        raise ValueError("Incomplete or invalid weather response")
    current, hourly, daily = raw["current"], raw["hourly"], raw["daily"]
    units = raw["current_units"]
    if units.get("temperature_2m") != "°C" or units.get("wind_speed_10m") != "km/h":
        raise ValueError("Unexpected weather units")
    stamp = datetime.fromisoformat(current["time"])
    start = stamp.replace(minute=0, second=0, microsecond=0)
    hours = []
    for index, value in enumerate(hourly["time"]):
        dt = datetime.fromisoformat(value)
        if start <= dt < start + timedelta(hours=24):
            hours.append({"time": value, "temperature": number(hourly["temperature_2m"][index], "temperature"),
                          "rain": number(hourly["precipitation_probability"][index], "rain probability", 0, 100)})
            if "relative_humidity_2m" in hourly:
                hours[-1]["humidity"] = number(hourly["relative_humidity_2m"][index], "hourly humidity", 0, 100)
    if len(hours) < 6 or len(hours) > 25:
        raise ValueError("Weather forecast needs at least six hourly readings")
    days = []
    for i, value in enumerate(daily["time"][:5]):
        datetime.fromisoformat(value)
        low = number(daily["temperature_2m_min"][i], "daily minimum")
        high = number(daily["temperature_2m_max"][i], "daily maximum")
        if low > high:
            raise ValueError("Daily minimum exceeds maximum")
        days.append({"date": value, "low": low, "high": high,
                     "code": int(number(daily["weather_code"][i], "code", 0, 99))})
    if not days:
        raise ValueError("Missing daily weather forecast")
    result = {"schema": 1, "label": label, "timezone": raw.get("timezone", "local"), "observed_at": current["time"],
              "temperature": number(current["temperature_2m"], "temperature"),
              "feels_like": number(current["apparent_temperature"], "apparent temperature"),
              "humidity": number(current["relative_humidity_2m"], "humidity", 0, 100),
              "wind": number(current["wind_speed_10m"], "wind speed", 0, 500),
              "code": int(number(current["weather_code"], "code", 0, 99)), "hours": hours, "days": days,
              "attribution": "Open-Meteo · CC BY 4.0", "demo": False}
    return validate_weather(result)


def validate_weather(data: dict) -> dict:
    if not isinstance(data, dict) or type(data.get("schema")) is not int or data.get("schema") != 1:
        raise ValueError("Expected an InkTag weather snapshot (schema 1)")
    if not isinstance(data.get("label"), str) or not 1 <= len(data["label"]) <= 32 or any(ord(c) < 32 for c in data["label"]):
        raise ValueError("Invalid location label")
    datetime.fromisoformat(data["observed_at"])
    for name in ("temperature", "feels_like"):
        number(data[name], name)
    number(data["humidity"], "humidity", 0, 100)
    number(data["wind"], "wind speed", 0, 500)
    number(data["code"], "code", 0, 99)
    if not isinstance(data["hours"], list) or not 6 <= len(data["hours"]) <= 25 or not isinstance(data["days"], list) or not 1 <= len(data["days"]) <= 5:
        raise ValueError("Invalid weather series length")
    last = None
    for hour in data["hours"]:
        dt = datetime.fromisoformat(hour["time"])
        if last is not None and dt <= last:
            raise ValueError("Hourly weather timestamps must increase")
        last = dt
        number(hour["temperature"], "hourly temperature")
        number(hour["rain"], "rain probability", 0, 100)
        if "humidity" in hour:
            number(hour["humidity"], "hourly humidity", 0, 100)
    for day in data["days"]:
        datetime.fromisoformat(day["date"])
        if number(day["low"], "low") > number(day["high"], "high"):
            raise ValueError("Daily minimum exceeds maximum")
        number(day["code"], "code", 0, 99)
    return data


def demo_weather() -> dict:
    """Synthetic preview data, not a real forecast or a location observation."""
    start = datetime(2026, 10, 18, 8)
    temps = [22, 23, 24, 25, 26, 27, 27, 27, 26, 25, 23, 22, 21, 20, 19, 18, 18, 17, 17, 17, 18, 18, 19, 20]
    rain = [10, 10, 5, 5, 5, 10, 15, 25, 45, 65, 80, 70, 50, 35, 25, 20, 15, 10, 10, 10, 10, 15, 20, 15]
    return {"schema": 1, "label": "杭州", "timezone": "Asia/Shanghai", "observed_at": start.isoformat(timespec="minutes"),
            "temperature": 22, "feels_like": 23, "humidity": 68, "wind": 11, "code": 2, "demo": True,
            "attribution": "Open-Meteo · CC BY 4.0",
            "hours": [{"time": (start + timedelta(hours=i)).isoformat(timespec="minutes"), "temperature": t, "rain": rain[i],
                       "humidity": [68,64,60,56,52,48,46,50,54,62,72,78,82,84,86,88,86,84,82,80,78,76,72,70][i]} for i, t in enumerate(temps)],
            "days": [{"date": (start + timedelta(days=i)).date().isoformat(), "low": low, "high": high, "code": code}
                     for i, (low, high, code) in enumerate([(17, 27, 2), (18, 25, 61), (16, 23, 3), (15, 24, 1), (16, 26, 0)])]}
