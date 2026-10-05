from __future__ import annotations

from typing import Any, Mapping


INDICATOR_PROTOCOL_VERSION = 2
INDICATOR_POLICIES = {0: "off", 1: "all", 3: "critical"}
INDICATOR_POLICY_IDS = {name: value for value, name in INDICATOR_POLICIES.items()}
INDICATOR_WIRE_SIZE = 2


def decode_indicator_config(payload: bytes) -> dict[str, Any]:
    if len(payload) != INDICATOR_WIRE_SIZE or payload[0] != INDICATOR_PROTOCOL_VERSION:
        raise ValueError("Device returned an invalid indicator configuration")
    policy = INDICATOR_POLICIES.get(payload[1])
    if policy is None:
        raise ValueError("Device returned an invalid indicator configuration")
    return {"policy": policy}


def encode_indicator_config(config: Mapping[str, Any]) -> bytes:
    policy = INDICATOR_POLICY_IDS.get(str(config.get("policy", "")))
    if policy is None:
        raise ValueError("Invalid indicator configuration")
    return bytes((INDICATOR_PROTOCOL_VERSION, policy))
