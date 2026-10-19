from __future__ import annotations

import math
import statistics
from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class ClockSample:
    monotonic_seconds: float
    host_utc_seconds: float
    device_utc_seconds: int
    round_trip_milliseconds: float


def summarize_clock_accuracy(samples: list[ClockSample]) -> dict[str, Any]:
    if len(samples) < 3:
        raise ValueError("clock accuracy requires at least three samples")
    ordered = sorted(samples, key=lambda sample: sample.monotonic_seconds)
    anchor = ordered[0]
    duration = ordered[-1].monotonic_seconds - anchor.monotonic_seconds
    if duration <= 0:
        raise ValueError("clock accuracy samples must span time")

    wall_errors = [
        sample.host_utc_seconds
        - (
            anchor.host_utc_seconds
            + sample.monotonic_seconds
            - anchor.monotonic_seconds
        )
        for sample in ordered
    ]
    edges: list[tuple[float, float, float]] = []
    skipped_transitions = 0
    previous = ordered[0]
    for sample in ordered[1:]:
        delta = sample.device_utc_seconds - previous.device_utc_seconds
        if delta == 1:
            edge_monotonic = (
                previous.monotonic_seconds + sample.monotonic_seconds
            ) / 2.0
            edge_host = (
                anchor.host_utc_seconds
                + edge_monotonic
                - anchor.monotonic_seconds
            )
            edges.append(
                (
                    edge_monotonic - anchor.monotonic_seconds,
                    sample.device_utc_seconds - edge_host,
                    sample.monotonic_seconds - previous.monotonic_seconds,
                )
            )
        elif delta > 1:
            skipped_transitions += delta
        previous = sample

    if len(edges) < 3:
        raise ValueError(
            "Insufficient timing resolution for clock accuracy: at least three "
            "one-second transitions are required. Use --interval 0.23 and check "
            "BLE read latency; this result does not establish a device clock fault."
        )
    x_values = [edge[0] for edge in edges]
    y_values = [edge[1] for edge in edges]
    x_mean = statistics.fmean(x_values)
    y_mean = statistics.fmean(y_values)
    sxx = sum((value - x_mean) ** 2 for value in x_values)
    if sxx <= 0:
        raise ValueError("clock transition samples do not span time")
    slope = sum(
        (x_value - x_mean) * (y_value - y_mean)
        for x_value, y_value in zip(x_values, y_values, strict=True)
    ) / sxx
    intercept = y_mean - slope * x_mean
    residuals = [
        y_value - (intercept + slope * x_value)
        for x_value, y_value in zip(x_values, y_values, strict=True)
    ]
    residual_variance = sum(value * value for value in residuals) / (len(edges) - 2)
    slope_standard_error = math.sqrt(residual_variance / sxx)
    drift_ppm = slope * 1_000_000.0
    confidence_95_ppm = 1.96 * slope_standard_error * 1_000_000.0
    brackets_ms = [edge[2] * 1000.0 for edge in edges]
    round_trips = [sample.round_trip_milliseconds for sample in ordered]
    sample_intervals = [
        current.monotonic_seconds - previous.monotonic_seconds
        for previous, current in zip(ordered, ordered[1:])
    ]

    return {
        "reference": "host monotonic clock anchored to UTC at the first sample",
        "sampleCount": len(ordered),
        "transitionCount": len(edges),
        "skippedTransitions": skipped_transitions,
        "durationSeconds": round(duration, 3),
        "medianSampleIntervalMilliseconds": round(
            statistics.median(sample_intervals) * 1000.0, 3
        ),
        "meanRoundTripMilliseconds": round(statistics.fmean(round_trips), 3),
        "maxRoundTripMilliseconds": round(max(round_trips), 3),
        "meanTransitionBracketMilliseconds": round(
            statistics.fmean(brackets_ms), 3
        ),
        "maxTransitionBracketMilliseconds": round(max(brackets_ms), 3),
        "driftPpm": round(drift_ppm, 3),
        "regression95PercentPpm": round(confidence_95_ppm, 3),
        "estimatedSecondsPerDay": round(slope * 86400.0, 4),
        "offsetAtStartSeconds": round(intercept, 6),
        "offsetAtEndSeconds": round(intercept + slope * duration, 6),
        "hostWallVsMonotonicRangeMilliseconds": round(
            (max(wall_errors) - min(wall_errors)) * 1000.0, 3
        ),
    }
