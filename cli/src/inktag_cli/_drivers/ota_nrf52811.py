#!/usr/bin/env python3
"""Canonical InkTag CLI driver for Nordic Secure DFU over BLE."""

from __future__ import annotations

import argparse
import asyncio
import json
import re
import struct
import sys
import time
import zipfile
import zlib
from dataclasses import dataclass
from pathlib import Path

from bleak import BleakClient, BleakScanner


DFU_SERVICE_UUID = "0000fe59-0000-1000-8000-00805f9b34fb"
DFU_CONTROL_UUID = "8ec90001-f315-4f60-9fb8-838830daea50"
DFU_PACKET_UUID = "8ec90002-f315-4f60-9fb8-838830daea50"
BUTTONLESS_UUID = "8ec90003-f315-4f60-9fb8-838830daea50"
RECOVERY_NAME_PATTERN = re.compile(r"^(?:IKT|InkTag)_NRF_([0-9A-F]{4})_OTA$", re.IGNORECASE)
APPLICATION_NAME_PATTERN = re.compile(r"^(?:IKT|InkTag)_NRF_([0-9A-F]{4})$", re.IGNORECASE)
BUTTONLESS_ENTER = 0x01
BUTTONLESS_RESPONSE = 0x20
BUTTONLESS_SUCCESS = 0x01

OP_CREATE = 0x01
OP_SET_PRN = 0x02
OP_CALCULATE_CRC = 0x03
OP_EXECUTE = 0x04
OP_SELECT = 0x06
OP_ABORT = 0x0C
OP_RESPONSE = 0x60
RESULT_SUCCESS = 0x01
OBJECT_COMMAND = 0x01
OBJECT_DATA = 0x02
DEFAULT_PRN = 4
MAX_OBJECT_ATTEMPTS = 3


@dataclass(frozen=True)
class DfuPackage:
    init_packet: bytes
    firmware: bytes


def load_package(path: Path) -> DfuPackage:
    with zipfile.ZipFile(path, "r") as archive:
        manifest = json.loads(archive.read("manifest.json"))
        root = manifest.get("manifest", manifest)
        application = root.get("application")
        if not application:
            raise ValueError("Only an application-only Nordic Secure DFU package is supported")
        if any(root.get(name) for name in ("bootloader", "softdevice", "softdevice_bootloader")):
            raise ValueError("Package must not contain a SoftDevice or bootloader")
        return DfuPackage(
            init_packet=archive.read(application["dat_file"]),
            firmware=archive.read(application["bin_file"]),
        )


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def progress(done: int, total: int, started: float) -> None:
    width = 34
    ratio = min(1.0, done / total) if total else 1.0
    filled = int(width * ratio)
    elapsed = max(time.monotonic() - started, 0.001)
    rate = done / elapsed / 1024
    bar = "#" * filled + "." * (width - filled)
    print(f"\r[{bar}] {ratio * 100:6.2f}%  {done}/{total} bytes  {rate:6.1f} KiB/s", end="", flush=True)
    if done >= total:
        print()


def characteristic_uuids(client: BleakClient) -> set[str]:
    return {
        characteristic.uuid.lower()
        for service in client.services
        for characteristic in service.characteristics
    }


def stable_id4(name: str | None, address: str | None = None) -> str | None:
    normalized = (name or "").strip()
    match = APPLICATION_NAME_PATTERN.fullmatch(normalized) or RECOVERY_NAME_PATTERN.fullmatch(normalized)
    if match:
        return match.group(1).upper()

    parts = (address or "").split(":")
    if len(parts) == 6 and all(len(part) == 2 and all(char in "0123456789abcdefABCDEF" for char in part) for part in parts):
        return f"{parts[-2]}{parts[-1]}".upper()
    return None


def recovery_name_from_identity(application_name: str | None, application_address: str | None = None) -> str:
    identity_suffix = stable_id4(application_name, application_address)
    if identity_suffix is None:
        raise ValueError(
            "Cannot determine the stable Nordic ID4 from the application name or BLE address"
        )
    return f"IKT_NRF_{identity_suffix}_OTA"


def application_name_from_bootloader(bootloader_name: str, requested_name: str | None) -> str | None:
    if requested_name and not RECOVERY_NAME_PATTERN.fullmatch(requested_name.strip()):
        return requested_name
    identity_suffix = stable_id4(bootloader_name)
    return f"IKT_NRF_{identity_suffix}" if identity_suffix else None


async def discover_device(name: str | None, address: str | None, timeout: float, bootloader: bool = False):
    target_label = address or name or ("IKT_NRF_<ID4>_OTA" if bootloader else "InkTag Nordic")
    print(f"Scanning for {target_label}...")
    found = await BleakScanner.discover(timeout=timeout, return_adv=True)
    candidates = []
    for device, advertisement in found.values():
        advertised_name = advertisement.local_name or device.name or ""
        services = {uuid.lower() for uuid in advertisement.service_uuids}
        if address and device.address.lower() != address.lower():
            continue
        if name and not advertised_name.lower().startswith(name.lower()):
            continue
        if not bootloader and not address and not name and not advertised_name.startswith(("IKT_NRF", "InkTag_NRF")):
            continue
        if bootloader:
            if DFU_SERVICE_UUID not in services or RECOVERY_NAME_PATTERN.fullmatch(advertised_name) is None:
                continue
        candidates.append((device, advertised_name, DFU_SERVICE_UUID in services))

    if not candidates:
        raise RuntimeError(f"No matching BLE device found: {target_label}")

    candidates.sort(key=lambda item: (not item[2], item[1]))
    device, advertised_name, _ = candidates[0]
    print(f"Found: {advertised_name or '(unnamed)'} [{device.address}]")
    return device


async def wait_for_application(
    name: str | None, address: str | None, timeout: float, connect_timeout: float
) -> None:
    """Require the updated application to advertise and expose Buttonless DFU.

    Nordic's final EXECUTE response is sent before dual-bank activation, reset
    and Windows' address cache have fully settled. Treat transfer completion
    and application recovery as separate gates so a silent post-OTA device is
    never reported as success.
    """
    started = time.monotonic()
    deadline = started + timeout
    label = address or name or "InkTag application"
    print(f"Waiting for application BLE reconnect ({label})", end="", flush=True)

    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        print(".", end="", flush=True)
        found = await BleakScanner.discover(timeout=min(3.0, remaining), return_adv=True)
        candidate = None
        candidate_name = ""
        for device, advertisement in found.values():
            advertised_name = advertisement.local_name or device.name or ""
            if address and device.address.lower() != address.lower():
                continue
            if name and not advertised_name.lower().startswith(name.lower()):
                continue
            if not address and not name and not advertised_name.startswith(("IKT_NRF", "InkTag_NRF")):
                continue
            candidate = device
            candidate_name = advertised_name
            break
        if candidate is None:
            continue

        try:
            async with BleakClient(candidate, timeout=min(connect_timeout, 10.0)) as client:
                if BUTTONLESS_UUID not in characteristic_uuids(client):
                    continue
        except Exception:
            # Windows may return a cached advertisement while the address is
            # still moving from Bootloader back to application. Keep polling.
            continue

        elapsed = time.monotonic() - started
        print(f"\nApplication BLE OK: {candidate_name or '(unnamed)'} [{candidate.address}] after {elapsed:.1f}s")
        return

    print()
    raise RuntimeError(f"OTA payload completed but application BLE did not recover within {timeout:.0f}s: {label}")


class SecureDfu:
    def __init__(self, client: BleakClient, packet_size: int = 20, prn: int = DEFAULT_PRN):
        self.client = client
        self.packet_size = packet_size
        self.prn = prn
        self.responses: asyncio.Queue[bytes] = asyncio.Queue()

    def _notification(self, _sender, data: bytearray) -> None:
        self.responses.put_nowait(bytes(data))

    async def start(self) -> None:
        await self.client.start_notify(DFU_CONTROL_UUID, self._notification)
        # Windows can accept write-without-response calls after the peripheral
        # queue is full. Four packets per receipt keeps nRF52811 reliable while
        # retaining substantially more throughput than per-packet receipts.
        await self.request(OP_SET_PRN, struct.pack("<H", self.prn))

    async def stop(self) -> None:
        if self.client.is_connected:
            try:
                await self.client.stop_notify(DFU_CONTROL_UUID)
            except Exception:
                pass

    async def request(self, opcode: int, payload: bytes = b"", timeout: float = 15.0) -> bytes:
        await self.client.write_gatt_char(DFU_CONTROL_UUID, bytes([opcode]) + payload, response=True)
        return await self.wait_response(opcode, timeout)

    async def wait_response(self, opcode: int, timeout: float = 15.0) -> bytes:
        while True:
            response = await asyncio.wait_for(self.responses.get(), timeout=timeout)
            if len(response) < 3 or response[0] != OP_RESPONSE:
                continue
            if response[1] != opcode:
                continue
            if response[2] != RESULT_SUCCESS:
                extended = response[3] if len(response) > 3 else None
                suffix = f", extended=0x{extended:02X}" if extended is not None else ""
                raise RuntimeError(f"Secure DFU opcode 0x{opcode:02X} failed: result=0x{response[2]:02X}{suffix}")
            return response[3:]

    async def select(self, object_type: int) -> tuple[int, int, int]:
        response = await self.request(OP_SELECT, bytes([object_type]))
        if len(response) != 12:
            raise RuntimeError(f"Malformed Secure DFU SELECT response ({len(response)} bytes)")
        return struct.unpack("<III", response)

    async def calculate_crc(self) -> tuple[int, int]:
        response = await self.request(OP_CALCULATE_CRC)
        if len(response) != 8:
            raise RuntimeError(f"Malformed Secure DFU CRC response ({len(response)} bytes)")
        return struct.unpack("<II", response)

    async def write_packets(
        self,
        data: bytes,
        reference: bytes | None = None,
        base_offset: int = 0,
    ) -> None:
        reference = data if reference is None else reference
        packet_count = 0
        for offset in range(0, len(data), self.packet_size):
            packet = data[offset : offset + self.packet_size]
            await self.client.write_gatt_char(DFU_PACKET_UUID, packet, response=False)
            packet_count += 1
            if self.prn and packet_count % self.prn == 0:
                payload = await self.wait_response(OP_CALCULATE_CRC)
                if len(payload) != 8:
                    raise RuntimeError(f"Malformed Secure DFU PRN response ({len(payload)} bytes)")
                remote_offset, remote_crc = struct.unpack("<II", payload)
                expected_offset = base_offset + offset + len(packet)
                expected_crc = crc32(reference[:expected_offset])
                if remote_offset != expected_offset or remote_crc != expected_crc:
                    raise RuntimeError(
                        f"Secure DFU PRN verification failed at {expected_offset}: "
                        f"device offset={remote_offset}, device CRC=0x{remote_crc:08X}"
                    )

    async def send_command(self, init_packet: bytes) -> None:
        max_size, offset, remote_crc = await self.select(OBJECT_COMMAND)
        expected_crc = crc32(init_packet[:offset]) if offset <= len(init_packet) else -1
        if offset == len(init_packet) and remote_crc == crc32(init_packet):
            print("Init packet already present; executing it.")
            await self.request(OP_EXECUTE)
            return
        if offset:
            reason = "mismatched CRC" if remote_crc != expected_crc else "incomplete object"
            print(f"Discarding the previous init packet ({reason}).")
            await self.request(OP_ABORT)
            max_size, offset, _ = await self.select(OBJECT_COMMAND)
            if offset:
                raise RuntimeError("Secure DFU did not reset the command object after ABORT")
        if len(init_packet) > max_size:
            raise RuntimeError(f"Init packet is {len(init_packet)} bytes; device limit is {max_size}")
        await self.request(OP_CREATE, bytes([OBJECT_COMMAND]) + struct.pack("<I", len(init_packet)))
        await self.write_packets(init_packet, init_packet, 0)
        checked_offset, checked_crc = await self.calculate_crc()
        if checked_offset != len(init_packet) or checked_crc != crc32(init_packet):
            raise RuntimeError("Init packet CRC verification failed")
        await self.request(OP_EXECUTE)

    async def prepare_firmware(self, firmware: bytes) -> tuple[int, int]:
        max_size, offset, remote_crc = await self.select(OBJECT_DATA)
        if not max_size:
            raise RuntimeError("Secure DFU returned a zero data-object size")
        if offset > len(firmware):
            raise RuntimeError(f"Remote DFU offset {offset} exceeds this firmware image")
        committed_offset = offset - (offset % max_size)
        crc_matches = remote_crc == crc32(firmware[:offset])
        if not crc_matches and offset == committed_offset and offset:
            raise RuntimeError("Remote DFU resume state belongs to a different firmware image")
        if offset != committed_offset:
            print(f"Discarding incomplete data object at byte {offset}; resuming from {committed_offset}.")
        elif offset:
            print(f"Resuming at byte {offset}.")
        return max_size, committed_offset

    async def send_firmware(
        self,
        firmware: bytes,
        state: tuple[int, int],
        interrupt_after_bytes: int | None = None,
    ) -> None:
        max_size, offset = state
        started = time.monotonic()
        progress(offset, len(firmware), started)
        while offset < len(firmware):
            object_size = min(max_size, len(firmware) - offset)
            chunk = firmware[offset : offset + object_size]
            next_offset = offset + len(chunk)
            last_error: Exception | None = None
            for attempt in range(1, MAX_OBJECT_ATTEMPTS + 1):
                try:
                    # CREATE rewinds an unfinished object to the last executed
                    # boundary, so a dropped ATT packet cannot poison retries.
                    await self.request(OP_CREATE, bytes([OBJECT_DATA]) + struct.pack("<I", object_size))
                    await self.write_packets(chunk, firmware, offset)
                    checked_offset, checked_crc = await self.calculate_crc()
                    if checked_offset != next_offset or checked_crc != crc32(firmware[:next_offset]):
                        raise RuntimeError(
                            f"Firmware CRC verification failed at {next_offset}: "
                            f"device offset={checked_offset}, device CRC=0x{checked_crc:08X}"
                        )
                    last_error = None
                    break
                except Exception as exc:
                    last_error = exc
                    if attempt < MAX_OBJECT_ATTEMPTS:
                        print(f"Retrying data object {offset}-{next_offset} ({attempt}/{MAX_OBJECT_ATTEMPTS}): {exc}")
                        await asyncio.sleep(0.2)
            if last_error is not None:
                raise RuntimeError(
                    f"Data object {offset}-{next_offset} failed {MAX_OBJECT_ATTEMPTS} times: {last_error}"
                )
            await self.request(OP_EXECUTE, timeout=30.0)
            offset = next_offset
            progress(offset, len(firmware), started)
            if interrupt_after_bytes is not None and offset >= interrupt_after_bytes:
                raise RuntimeError(
                    f"Injected OTA interruption after {offset}/{len(firmware)} bytes; "
                    "the device should remain in Secure DFU for retransmission"
                )


async def enter_buttonless_dfu(client: BleakClient, expected_name: str) -> str:
    response_event = asyncio.Event()
    result = bytearray()

    def notification(_sender, data: bytearray) -> None:
        result[:] = data
        response_event.set()

    await client.start_notify(BUTTONLESS_UUID, notification)

    async def request(opcode: int, payload: bytes = b"") -> None:
        response_event.clear()
        result.clear()
        await client.write_gatt_char(BUTTONLESS_UUID, bytes((opcode,)) + payload, response=True)
        try:
            await asyncio.wait_for(response_event.wait(), timeout=8.0)
        except asyncio.TimeoutError:
            if client.is_connected:
                raise RuntimeError(f"No response to buttonless DFU opcode 0x{opcode:02X}")
            return
        if len(result) < 3 or result[0] != BUTTONLESS_RESPONSE or result[1] != opcode:
            raise RuntimeError(f"Malformed buttonless DFU response: {result.hex()}")
        if result[2] != BUTTONLESS_SUCCESS:
            raise RuntimeError(f"Buttonless DFU opcode 0x{opcode:02X} rejected: {result.hex()}")

    print("Requesting buttonless DFU mode...")
    await request(BUTTONLESS_ENTER)
    for _ in range(30):
        if not client.is_connected:
            break
        await asyncio.sleep(0.1)
    return expected_name


async def run(args) -> None:
    package = load_package(args.package)
    print(f"Package: {args.package}")
    print(f"  Init packet: {len(package.init_packet)} bytes")
    print(f"  Application: {len(package.firmware)} bytes, CRC32=0x{crc32(package.firmware):08X}")

    application_name = None
    application_address = None
    device = await discover_device(args.name, args.address, args.scan_timeout)
    async with BleakClient(device, timeout=args.connect_timeout) as client:
        uuids = characteristic_uuids(client)
        if DFU_CONTROL_UUID in uuids and DFU_PACKET_UUID in uuids:
            bootloader_device = device
            bootloader_name = device.name or ""
            if RECOVERY_NAME_PATTERN.fullmatch(bootloader_name) is None:
                raise RuntimeError(
                    "Nordic Recovery must advertise as IKT_NRF_<ID4>_OTA"
                )
            application_name = application_name_from_bootloader(bootloader_name, args.name)
        elif BUTTONLESS_UUID in uuids:
            application_name = device.name or args.name or "IKT_NRF"
            application_address = device.address
            candidate_name = recovery_name_from_identity(application_name, application_address)
            bootloader_name = await enter_buttonless_dfu(client, candidate_name)
            bootloader_device = None
        else:
            raise RuntimeError("Device exposes neither InkTag buttonless DFU nor Nordic Secure DFU")

    if bootloader_device is None:
        await asyncio.sleep(1.0)
        bootloader_device = await discover_device(bootloader_name, None, args.scan_timeout, bootloader=True)

    print(f"Connecting to {bootloader_name}...")
    async with BleakClient(bootloader_device, timeout=args.connect_timeout) as client:
        uuids = characteristic_uuids(client)
        if DFU_CONTROL_UUID not in uuids or DFU_PACKET_UUID not in uuids:
            raise RuntimeError("Selected device does not expose Nordic Secure DFU control and packet characteristics")
        dfu = SecureDfu(client, packet_size=args.packet_size)
        await dfu.start()
        try:
            print("Sending signed init packet...")
            await dfu.send_command(package.init_packet)
            firmware_state = await dfu.prepare_firmware(package.firmware)
            print("Sending application...")
            interrupt_after_bytes = None
            if args.interrupt_after_percent is not None:
                interrupt_after_bytes = max(
                    1,
                    (len(package.firmware) * args.interrupt_after_percent + 99) // 100,
                )
            await dfu.send_firmware(package.firmware, firmware_state, interrupt_after_bytes)
        finally:
            await dfu.stop()
    print("OTA complete. The device is validating and restarting the application.")
    if not args.skip_reconnect_check and (application_address or application_name):
        await wait_for_application(
            application_name, application_address, args.reconnect_timeout, args.connect_timeout
        )
    elif not args.skip_reconnect_check:
        print("Application reconnect check skipped: OTA was started directly in Bootloader mode.")


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path, help="signed application-only Nordic DFU ZIP")
    parser.add_argument("--name", help="application BLE name/prefix, for example IKT_NRF_1A2B")
    parser.add_argument("--address", help="application or bootloader BLE address")
    parser.add_argument("--scan-timeout", type=float, default=12.0)
    parser.add_argument("--connect-timeout", type=float, default=15.0)
    parser.add_argument("--reconnect-timeout", type=float, default=45.0)
    parser.add_argument("--skip-reconnect-check", action="store_true")
    parser.add_argument("--packet-size", type=int, default=20, choices=range(20, 245))
    parser.add_argument(
        "--interrupt-after-percent",
        type=int,
        choices=range(1, 100),
        help="hardware-test only: disconnect after this percentage and leave Secure DFU recoverable",
    )
    return parser.parse_args()


def main() -> int:
    try:
        asyncio.run(run(parse_args()))
        return 0
    except KeyboardInterrupt:
        print("\nCancelled.", file=sys.stderr)
        return 130
    except Exception as exc:
        print(f"\nOTA failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
