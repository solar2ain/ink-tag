"""Bounded App return and one verified clock write; no control replay."""
from __future__ import annotations

import asyncio
from datetime import datetime
import time

from bleak.exc import BleakError

from .board_ids import board_ids_equivalent
from .generated_commands_v2 import PROTOCOL_V2_VERSION
from .transport import ApplicationBootstrapError, InkTagError


def application_identity(snapshot):
    return {
        "address": snapshot.address,
        "bleAddress": snapshot.ble_address,
        "board": snapshot.board_id,
        "appBytes": snapshot.application_size,
        "appVersion": snapshot.application_version,
        "checksum": snapshot.application_checksum,
        "activationId": snapshot.activation.activation_id,
    }


def verify_application(client, expected):
    current = application_identity(client.snapshot)
    if (not client._client or not client._client.is_connected
            or client.snapshot.protocol_version != PROTOCOL_V2_VERSION
            or not current["address"]
            or current["address"].casefold() != expected["address"].casefold()
            or not current["board"] or not current["appBytes"]
            or current["checksum"] is None):
        raise InkTagError("Application return identity/role was not verified; no clock write sent")
    for key in ("bleAddress", "board", "appBytes", "appVersion", "checksum", "activationId"):
        value = expected.get(key)
        if value is None or value == "":
            continue
        equal = (board_ids_equivalent(current[key], value) if key == "board"
                 else str(current[key]).casefold() == str(value).casefold())
        if not equal:
            raise InkTagError(f"Application return {key} mismatch; no clock write sent")
    return current


async def sync_returned_clock(client):
    try:
        # Existing ordinary SET_TIME + GET_TIME preserves the negotiated week
        # start and uses the normal local timezone. Neither write is retried.
        return await asyncio.wait_for(
            client.set_time(datetime.now().astimezone(), timeout=15.0), 30.0)
    except Exception as error:
        raise InkTagError(
            f"App has returned, but clock synchronization was not confirmed: {error}. "
            "No reset, Recovery return or SET_TIME was repeated.") from error


async def connect_returned_application(client, deadline, on_phase):
    last_error = None
    while time.monotonic() < deadline:
        try:
            await asyncio.wait_for(client.connect(), deadline - time.monotonic())
            return
        except (ApplicationBootstrapError, BleakError, OSError, TimeoutError) as error:
            # Only the transport's explicit pre-protocol bootstrap wrapper is
            # retryable. Ordinary InkTagError (identity/App/AUTH/refusal) exits.
            last_error = error
            await client.disconnect()
            if time.monotonic() < deadline:
                on_phase("Application connection not ready; retrying within the remaining return window")
                await asyncio.sleep(min(0.5, max(0.0, deadline - time.monotonic())))
    raise InkTagError(f"Application return connection timed out: {last_error}. "
                      "No RESET, Recovery return or SET_TIME was repeated.")


async def restart_application(client, *, reconnect_timeout=90.0,
                              synchronize=True, on_phase=lambda message: None):
    expected = application_identity(client.snapshot)
    verify_application(client, expected)
    session = client._client
    old_address, old_name = client.address, client.name
    old_auto, old_expected = client.auto_sync_time, client.expected_device_id
    try:
        client.auto_sync_time = False
        on_phase("Requesting restart")
        try:
            reply = await client.reset()
        except Exception as error:
            raise InkTagError(f"Restart request result is unconfirmed: {error}. "
                              "RESET was not repeated; reconnect read-only to inspect the device.") from error
        on_phase("Waiting for device disconnect")
        deadline = time.monotonic() + reconnect_timeout
        drop_deadline = min(deadline, time.monotonic() + 20.0)
        while session is not None and session.is_connected:
            if time.monotonic() >= drop_deadline:
                raise InkTagError("Restart acknowledged, but no disconnect was observed; "
                                  "restart result is unconfirmed. RESET was not repeated.")
            await asyncio.sleep(min(0.1, max(0.0, drop_deadline - time.monotonic())))
        await client.disconnect()
        client.address, client.name = expected["address"], None
        if expected.get("activationId") and int(expected["activationId"], 16) != 0:
            client.expected_device_id = expected["activationId"]
        on_phase("Waiting for the same application")
        await connect_returned_application(client, deadline, on_phase)
        on_phase("Verifying returned application")
        identity = verify_application(client, expected)
        effective = None
        if synchronize:
            on_phase("Synchronizing returned application clock")
            effective = await sync_returned_clock(client)
        on_phase("Restart completed" if synchronize else "Restart completed; time synchronization disabled")
        return {"ok": True, "action": "restart", "deviceReply": reply,
                "applicationVerified": True, "address": identity["address"],
                "clockSynced": synchronize, "time": effective}
    finally:
        client.address, client.name = old_address, old_name
        client.auto_sync_time, client.expected_device_id = old_auto, old_expected


async def finish_recovery_return(options, result, *, address=None, on_phase=lambda message: None):
    if result.get("ok") is not True:
        raise InkTagError("Recovery did not confirm application return; no clock write sent")
    if options.preserve_device_time:
        return {**result, "clockSynced": False}
    exact_address = result.get("address") or address
    if not exact_address:
        raise InkTagError("App return reported without an exact address; no clock write sent")
    image = result.get("applicationImage") or {}
    expected = {"address": exact_address, "board": result.get("board"),
                "appBytes": image.get("appBytes", result.get("applicationBytes")),
                "appVersion": result.get("applicationBuild"), "checksum": image.get("checksum")}
    client = options.client(auto_sync_time=False)
    client.address, client.name = exact_address, None
    try:
        on_phase("Verifying returned application")
        await connect_returned_application(client, time.monotonic() + 45.0, on_phase)
        verify_application(client, expected)
        on_phase("Synchronizing returned application clock")
        effective = await sync_returned_clock(client)
        return {**result, "applicationVerified": True, "clockSynced": True, "time": effective}
    except InkTagError:
        raise
    except Exception as error:
        raise InkTagError(f"App return was reported, but post-return verification failed: {error}. "
                          "Recovery return was not repeated.") from error
    finally:
        await client.disconnect()
