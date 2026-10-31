#!/usr/bin/env python3
"""Run the reviewed pvvx backend with early SWS activation detection."""

from __future__ import annotations

import ctypes
from contextlib import nullcontext
import importlib.util
import hashlib
import json
import sys
import time
from pathlib import Path
from types import ModuleType


class ActivationNoContact(SystemExit):
    """Typed zero-response activation failure, before any Flash access."""


def contact_retry(activate, *, enabled=False, timeout_seconds=0, interval_seconds=1):
    """Retry activation only. Never rerun backend.main or any Flash operation."""
    def run(serial_port, activation_ms):
        started = time.monotonic()
        attempt = 0
        while True:
            attempt += 1
            remaining = timeout_seconds - (time.monotonic() - started) if timeout_seconds else None
            if remaining is not None and remaining <= 0:
                raise SystemExit("Contact wait deadline reached; no Flash access.")
            limit = min(activation_ms, max(1, int(remaining * 1000))) if remaining is not None else activation_ms
            try:
                return activate(serial_port, limit)
            except ActivationNoContact:
                if not enabled:
                    raise
                remaining = timeout_seconds - (time.monotonic() - started) if timeout_seconds else None
                if remaining is not None and remaining <= 0:
                    raise SystemExit("Contact wait deadline reached; no Flash access.")
                print(f"-------------------- DETECTION RETRY {attempt + 1} --------------------", flush=True)
                print(f"CONTACT WAIT: no SWS response on activation attempt {attempt}; retrying activation only, before Flash access.")
                time.sleep(min(interval_seconds, remaining) if remaining is not None else interval_seconds)
    return run


def load_backend(path: Path) -> ModuleType:
    spec = importlib.util.spec_from_file_location("inktag_tlsr825x_backend", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load TLSR825x backend: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def install_windows_serial_partial_write(backend: ModuleType) -> None:
    """Complete valid partial overlapped writes from Windows CH340 drivers."""

    if sys.platform != "win32":
        return
    serial_class = backend.serial.Serial
    serialwin32 = __import__(serial_class.__module__, fromlist=["*"])
    win32 = serialwin32.win32
    to_bytes = serialwin32.to_bytes
    port_not_open = serialwin32.PortNotOpenError
    serial_exception = serialwin32.SerialException
    serial_timeout = serialwin32.SerialTimeoutException

    def write_all(serial_port, data) -> int:
        if not serial_port.is_open:
            raise port_not_open()
        pending = to_bytes(data)
        total = 0
        no_progress = 0
        while total < len(pending):
            transferred = win32.DWORD()
            chunk = pending[total:]
            success = win32.WriteFile(
                serial_port._port_handle,
                chunk,
                len(chunk),
                ctypes.byref(transferred),
                serial_port._overlapped_write,
            )
            if serial_port._write_timeout == 0:
                error = win32.ERROR_SUCCESS if success else win32.GetLastError()
                if error in (win32.ERROR_SUCCESS, win32.ERROR_IO_PENDING):
                    return len(pending)
                if error in (
                    win32.ERROR_INVALID_USER_BUFFER,
                    win32.ERROR_NOT_ENOUGH_MEMORY,
                    win32.ERROR_OPERATION_ABORTED,
                ):
                    return total
                raise serial_exception(
                    "WriteFile failed ({!r})".format(ctypes.WinError())
                )
            if not success and win32.GetLastError() not in (
                win32.ERROR_SUCCESS,
                win32.ERROR_IO_PENDING,
            ):
                raise serial_exception(
                    "WriteFile failed ({!r})".format(ctypes.WinError())
                )
            win32.GetOverlappedResult(
                serial_port._port_handle,
                serial_port._overlapped_write,
                ctypes.byref(transferred),
                True,
            )
            if win32.GetLastError() == win32.ERROR_OPERATION_ABORTED:
                return total + transferred.value
            if transferred.value == 0:
                no_progress += 1
                if no_progress >= 20:
                    raise serial_timeout("Write made no progress after 20 retries")
                time.sleep(0.005)
                continue
            no_progress = 0
            total += transferred.value
        return total

    serial_class.write = write_all


def confirm_sws_first_bit_sample(backend, serial_port, divider: int) -> bool:
    """Qualify a later UART sample against three known B2 register readbacks.

    The first response symbol overlaps the master's start pulse. Some CH340
    links decode only its most significant data bit incorrectly at the vendor's
    0x20 sample. Keep that default unless a complete captured B2 frame supports
    0x40, then require two distinct divider values and restoration. No decoded
    data bit is masked, no stop/length check is weakened and no Flash is touched.
    The selected mask belongs only to this newly loaded backend/process.
    """
    if getattr(backend, "bit8mask", None) != 0x20 or not 1 <= divider <= 127:
        return False
    frame = getattr(backend, "_inktag_last_b2_frame", None)
    if frame is None or len(frame) != 9:
        return False
    original_mask = backend.bit8mask
    qualified = False
    backend.bit8mask = 0x40
    try:
        if backend.sws_decode_blk(frame) != divider:
            return False
        adjacent = divider + 1 if divider < 127 else divider - 1
        for expected in (divider, adjacent, divider):
            if not backend.rd_sws_wr_addr_usbcom(serial_port, 0x00B2, bytearray([expected])):
                return False
            observed = backend.sws_read_data(serial_port, 0x00B2)
            if observed != [expected]:
                # Restore the requested divider, never the vendor default 5.
                backend.rd_sws_wr_addr_usbcom(serial_port, 0x00B2, bytearray([divider]))
                return False
        qualified = True
        print("SWS first-bit sample 0x40 qualified by three exact B2 readbacks "
              "at two divider values; frame/identity/Flash checks remain required.", flush=True)
        return True
    finally:
        if not qualified:
            backend.bit8mask = original_mask


def install_early_activation(
    backend: ModuleType,
    hold_on_timeout: bool = False,
    operation_baud: int | None = None,
    *, retry_detect: bool = False, detect_timeout_seconds: int = 0,
    detect_interval_seconds: int = 1,
    clock_hz: int = 0,
    reset_pulse_ms: int = 500,
) -> None:
    if type(reset_pulse_ms) is not int or not 50 <= reset_pulse_ms <= 2000:
        raise ValueError('RESET pulse must be 50..2000 ms')
    original_auto_speed = backend.set_sws_auto_speed
    original_set_speed = backend.set_sws_speed
    serial_exception = getattr(
        getattr(backend, "serial", None), "SerialException", OSError
    )
    speed_is_ready = False

    # Keep each write burst below the CH340/Windows buffering range.  A long
    # unthrottled burst can leave stop frames queued behind the validation
    # request at 460800 baud, so the readback is then decoded out of phase.
    capture_groups = 64
    stop_frames_per_group = 8
    # The ROM/early-boot window runs close to 24 MHz, while the InkTag
    # application switches the TLSR8359 system clock to 32 MHz before it can
    # reclaim PA7 for the blue LED.  Probe both states: assuming only the boot
    # clock makes a perfectly attached application-state target look silent.
    validation_clock_hz_candidates = (
        23_500_000,
        32_000_000,
        24_000_000,
        16_000_000,
        48_000_000,
    )
    if clock_hz:
        if clock_hz < 1 or clock_hz > 192_000_000:
            raise ValueError("Invalid requested SWS clock assumption")
        validation_clock_hz_candidates = (clock_hz,)
    activation_serial_timeout = 0.10
    software_reset_interval = 3
    reconnect_poll_seconds = 0.10

    def flush_output(serial_port) -> None:
        flush = getattr(serial_port, "flush", None)
        if flush is not None:
            flush()

    def synchronize(serial_port, stop_block) -> None:
        flush_output(serial_port)
        serial_port.reset_input_buffer()
        backend.rd_wr_usbcom_blk(serial_port, backend.sws_code_end())
        backend.rd_wr_usbcom_blk(serial_port, stop_block)
        flush_output(serial_port)
        serial_port.reset_input_buffer()

    def capture_stop_burst(serial_port, stop_block) -> None:
        """Continuously stop the CPU without building a stale UART backlog."""

        for _ in range(capture_groups):
            for _ in range(stop_frames_per_group):
                backend.wr_usbcom_blk(serial_port, stop_block)
            flush_output(serial_port)
            # RX is joined to TX on the one-wire adapter, so discard the echo
            # after it has physically left the UART before sending a readback.
            serial_port.reset_input_buffer()

    def issue_software_reset(serial_port, reset_block) -> None:
        """Request a whole-chip reset over SWS, then capture the fresh window."""

        backend.wr_usbcom_blk(serial_port, reset_block)
        flush_output(serial_port)
        print("RESET SWS: whole-chip reset request sent (0x20); waiting for SWS readback, not reset attestation.")
        serial_port.reset_input_buffer()

    def try_lock_speed(
        serial_port,
    ) -> tuple[int | None, int | None, int | None]:
        """Set and verify one known-good divider without a destructive fallback.

        The upstream set_sws_speed() writes a default divider after any failed
        readback.  A transient CH340 decode failure can therefore destroy a
        correctly captured reset window.  Rewriting and rereading the same
        observed-good divider is safe and lets a later read recover from noise.
        """

        observed = None
        for clock_hz in validation_clock_hz_candidates:
            divider = round(clock_hz * 2 / serial_port.baudrate)
            if divider <= 0 or divider > 0x7F:
                continue
            serial_port.reset_input_buffer()
            backend.rd_sws_wr_addr_usbcom(
                serial_port, 0x00B2, bytearray([divider])
            )
            data = backend.sws_read_data(serial_port, 0x00B2)
            if getattr(backend, "debug", False):
                readback = "none" if data is None else bytes(data).hex().upper()
                print(
                    f"SWS timing check: clock assumption {clock_hz / 1_000_000:g} MHz, "
                    f"expected divider 0x{divider:02X}, decoded readback {readback}.",
                    flush=True,
                )
            if data is not None and len(data) == 1:
                observed = data[0]
                if observed == divider:
                    return divider, observed, clock_hz
                if confirm_sws_first_bit_sample(backend, serial_port, divider):
                    return divider, divider, clock_hz
        return None, observed, None

    def hand_over_operation_baud(
        serial_port, detected_clock_hz: int
    ) -> tuple[int, int]:
        """Capture at a reliable baud, then retime the stopped target for I/O.

        Capture a running InkTag application at 921600, then select a slower,
        board-qualified Flash-operation rate only when the stopped target's
        7-bit SWS response divider can represent it.
        """

        if operation_baud is None or operation_baud == serial_port.baudrate:
            return detected_clock_hz, round(
                detected_clock_hz * 2 / serial_port.baudrate
            )
        if operation_baud < 340000:
            raise SystemExit("InkTag operation baud must be >= 340000")

        activation_baud = serial_port.baudrate
        operation_divider = round(detected_clock_hz * 2 / operation_baud)
        if operation_divider <= 0 or operation_divider > 0x7F:
            print(
                f"The detected ~{detected_clock_hz / 1_000_000:.1f} MHz SWS "
                f"clock requires divider {operation_divider} at "
                f"{operation_baud} baud, outside the 1..127 hardware range; "
                "refusing Flash access."
            )
            raise SystemExit(1)

        flush_output(serial_port)
        serial_port.reset_input_buffer()
        reset_output = getattr(serial_port, "reset_output_buffer", None)
        if reset_output is not None:
            reset_output()
        try:
            serial_port.baudrate = operation_baud
        except (serial_exception, OSError) as exc:
            print(f"Could not switch UART to operation baud: {exc}")
            raise SystemExit(1) from None

        serial_port.reset_input_buffer()
        backend.rd_sws_wr_addr_usbcom(
            serial_port, 0x00B2, bytearray([operation_divider])
        )
        observed = backend.sws_read_data(serial_port, 0x00B2)
        if observed is None or len(observed) != 1 or observed[0] != operation_divider:
            print(
                f"SWS operation-baud handover to {operation_baud} failed; "
                "refusing Flash access."
            )
            raise SystemExit(1)
        print(
            f"SWS operation link ready at {operation_baud} baud, divider "
            f"{operation_divider} (~{detected_clock_hz / 1_000_000:.1f} MHz)."
        )
        return detected_clock_hz, operation_divider

    def recover_cpu_after_timeout(serial_port) -> None:
        """Undo a partially accepted activation before returning failure.

        USB-UART SWS is write-first: the target can accept the CPU-stop frame
        even when the following readback is too noisy to validate.  Leaving
        that failure path untouched strands the application in debug-stop and
        makes it look as though a physical reset is required.  These bounded,
        write-only frames are the same CPU-go/whole-chip-reset sequence used
        by the backend's successful ``-r`` exit; they do not access Flash.
        """

        for _ in range(3):
            backend.sws_wr_addr_usbcom(
                serial_port, 0x0602, bytearray([0x88])
            )
            backend.sws_wr_addr_usbcom(
                serial_port, 0x006F, bytearray([0x20])
            )
            time.sleep(0.01)

    def reopen_after_reenumeration(serial_port, deadline: float) -> bool:
        """Reopen the same COM port after a target-power USB transient.

        This runs only inside activation, before the backend can dispatch a
        Flash operation.  Some CH340 modules briefly re-enumerate when their
        3.3 V output is loaded; keep the original activation deadline rather
        than turning that event into an unbounded retry.
        """

        port_name = getattr(serial_port, "port", "the selected COM port")
        print(
            f"USB-UART re-enumerated during activation; waiting to reopen "
            f"{port_name} (no Flash access)..."
        )
        try:
            serial_port.close()
        except (serial_exception, OSError):
            pass

        while time.monotonic() < deadline:
            try:
                serial_port.open()
                serial_port.timeout = activation_serial_timeout
                serial_port.setDTR(False)
                serial_port.setRTS(False)
                serial_port.reset_input_buffer()
                reset_output = getattr(serial_port, "reset_output_buffer", None)
                if reset_output is not None:
                    reset_output()
                print(f"Reopened {port_name}; continuing the activation window.")
                return True
            except (serial_exception, OSError):
                try:
                    serial_port.close()
                except (serial_exception, OSError):
                    pass
                time.sleep(reconnect_poll_seconds)
        return False

    def activate(serial_port, timeout_ms: int) -> None:
        nonlocal speed_is_ready
        print(f"RESET RTS: requesting a {reset_pulse_ms} ms RESET pulse; physical reset requires RTS-to-RESET wiring.")
        serial_port.setDTR(True)
        serial_port.setRTS(True)
        time.sleep(reset_pulse_ms / 1000.0)
        serial_port.setDTR(False)
        serial_port.setRTS(False)
        print("RESET RTS: pulse released; capturing SWS activation window.")

        print(f"Activate ({timeout_ms} ms, early detection)...")
        reset_block = backend.sws_wr_addr(0x006F, bytearray([0x20]))
        stop_block = backend.sws_wr_addr(0x0602, bytearray([0x05]))
        deadline = time.monotonic() + max(timeout_ms, 0) / 1000.0
        started = time.monotonic()
        validation_count = 0
        decoded_readback_count = 0
        software_reset_count = 0
        reconnect_count = 0
        original_timeout = getattr(serial_port, "timeout", None)
        timeout_adjusted = hasattr(serial_port, "timeout")
        activation_link_failed = False
        if timeout_adjusted:
            serial_port.timeout = activation_serial_timeout

        try:
            while time.monotonic() < deadline:
                try:
                    # A preceding read/write process deliberately leaves the CPU
                    # stopped so the next guarded phase can attach without
                    # restarting an old application that may reclaim PA7/SWS for
                    # its blue LED.  Validate that state first.  Request a
                    # whole-chip reset only after several direct stop/readback
                    # attempts have failed.
                    if validation_count and validation_count % software_reset_interval == 0:
                        issue_software_reset(serial_port, reset_block)
                        software_reset_count += 1
                    capture_stop_burst(serial_port, stop_block)
                    synchronize(serial_port, stop_block)
                    validation_count += 1
                    divider, observed, detected_clock_hz = try_lock_speed(
                        serial_port
                    )
                    if observed is not None:
                        decoded_readback_count += 1
                    if divider is not None:
                        detected_clock_hz, divider = hand_over_operation_baud(
                            serial_port, detected_clock_hz
                        )
                        speed_is_ready = True
                        elapsed_ms = round((time.monotonic() - started) * 1000)
                        print(
                            f"Activation detected after {elapsed_ms} ms "
                            f"and {validation_count} validation attempt(s), "
                            f"{software_reset_count} software reset attempt(s), "
                            f"{reconnect_count} USB reconnect(s), "
                            f"divider {divider} "
                            f"(~{detected_clock_hz / 1_000_000:.1f} MHz)."
                        )
                        return
                except (serial_exception, OSError) as exc:
                    if reopen_after_reenumeration(serial_port, deadline):
                        reconnect_count += 1
                        continue
                    activation_link_failed = True
                    print(
                        "USB-UART link did not return before the activation "
                        f"deadline: {exc} (no Flash access)."
                    )
                    raise SystemExit(1) from None
        finally:
            if timeout_adjusted:
                try:
                    serial_port.timeout = original_timeout
                except serial_exception as exc:
                    # Preserve the useful first error when Windows invalidated
                    # the CH340 handle during a target-power transient.
                    if not activation_link_failed:
                        print(
                            "USB-UART link failed during activation before "
                            f"Flash access: {exc}"
                        )
                        raise SystemExit(1) from None

        if timeout_ms > 0:
            timeout_action = (
                "leaving the final CPU-stop capture state intact"
                if hold_on_timeout
                else "issuing best-effort CPU-go/reset cleanup"
            )
            print(
                f"Activation timed out after {validation_count} validation "
                f"attempt(s), {decoded_readback_count} decoded readback(s); "
                f"{software_reset_count} software reset attempt(s); "
                f"{timeout_action} (no Flash access)."
            )
            if not hold_on_timeout:
                recover_cpu_after_timeout(serial_port)
            if decoded_readback_count == 0:
                raise ActivationNoContact(1)
            raise SystemExit(1)

    def set_sws_auto_speed(serial_port) -> bool:
        nonlocal speed_is_ready
        if speed_is_ready:
            speed_is_ready = False
            return True
        return original_auto_speed(serial_port)

    def set_sws_speed(serial_port, clock_hz) -> bool:
        nonlocal speed_is_ready
        if speed_is_ready:
            speed_is_ready = False
            return True
        return original_set_speed(serial_port, clock_hz)

    backend.activate = contact_retry(activate, enabled=retry_detect,
        timeout_seconds=detect_timeout_seconds if retry_detect else 0,
        interval_seconds=detect_interval_seconds)
    backend.set_sws_auto_speed = set_sws_auto_speed
    backend.set_sws_speed = set_sws_speed


def install_exact_sws_frames(backend: ModuleType) -> None:
    """Bounded read-only adapter correction: consume exactly nine UART symbols.

    The vendor's short-read completion requests 10-len rather than 9-len.
    Do not change its on-wire grammar or decoder; reject absent/invalid bytes.
    This is a host regression fix, not proof of the current fixture's cause.
    """
    def read(serial_port, addr, size=1):
        if addr == 0x00B2:
            backend._inktag_last_b2_frame = None
        time.sleep(0.05)
        serial_port.reset_input_buffer()
        if not backend.rd_wr_usbcom_blk(serial_port, backend.sws_rd_addr(addr)):
            print(f"SWS receive failed: address echo incomplete at 0x{addr:06X}.")
            backend.rd_wr_usbcom_blk(serial_port, backend.sws_code_end())
            return None
        out = []
        try:
            for index in range(size):
                serial_port.write([0xFE])
                frame = serial_port.read(9)
                # At most nine reads per byte, independent of timeout setting.
                for _ in range(8):
                    if len(frame) >= 9:
                        break
                    part = serial_port.read(9 - len(frame))
                    if not part:
                        break
                    frame += part
                decoded = backend.sws_decode_blk(frame)
                if addr == 0x00B2 and size == 1:
                    backend._inktag_last_b2_frame = bytes(frame)
                if addr in (0x00B2, 0x06BC) and getattr(backend, "debug", False):
                    value = "invalid" if decoded is None else f"0x{decoded:02X}"
                    print(f"SWS diagnostic frame 0x{addr:06X} byte {index + 1}: "
                          f"{frame.hex().upper()}; decoded={value}.", flush=True)
                if decoded is None:
                    print(f"SWS receive failed: register 0x{addr:06X}, byte {index + 1}/{size}, UART symbols {len(frame)}/9; invalid/short frame.")
                    return None
                out.append(decoded)
            return out
        finally:
            backend.rd_wr_usbcom_blk(serial_port, backend.sws_code_end())
    backend.sws_read_data = read


def establish_read_only_flash_idle(backend, serial_port) -> bool:
    """Start a new read command with CS high, as B85 flash_send_cmd does.

    SWS capture may halt an application during an existing MSPI transaction.
    Starting with CS low would append RDID to that old transaction. This uses
    the existing mapped MSPI control register, not Flash WREN/program/erase.
    UART echo completion does not attest the target's register value.
    """
    if not backend.rd_sws_wr_addr_usbcom(serial_port, 0x0D, bytearray([0x01])):
        print('SWS setup failed: UART echo incomplete while requesting Flash CS idle (register 0x000D); no read command sent.')
        return False
    return True


def install_flash_status_retry(
    backend: ModuleType, expected_flash_jedec: bytes | None = None,
    *, establish_read_bus_idle: bool = False,
) -> None:
    """Keep polling after a transient SWS decode failure.

    The reviewed backend aborts immediately when one status-register read
    returns None. USB-UART SWS can lose an individual response while the Flash
    is busy, so use the backend's existing bounded poll count without
    reissuing the write, erase or unlock operation.
    """

    def read_flash_status(serial_port, opcode: int = 0x05):
        if establish_read_bus_idle and not establish_read_only_flash_idle(backend, serial_port):
            return None
        backend.rd_sws_wr_addr_usbcom(serial_port, 0x0D, bytearray([0x00]))
        backend.rd_sws_wr_addr_usbcom(serial_port, 0x0C, bytearray([opcode]))
        # Telink's mspi_read() transmits one dummy byte to generate the eight
        # clocks that shift status-register data into the MSPI data register.
        backend.rd_sws_wr_addr_usbcom(serial_port, 0x0C, bytearray([0x00]))
        data = backend.sws_read_data(serial_port, 0x0C)
        backend.rd_sws_wr_addr_usbcom(serial_port, 0x0D, bytearray([0x01]))
        return None if data is None else data[0]

    def read_flash_jedec(serial_port, attempts: int = 6):
        decode_failures = 0
        for _ in range(attempts):
            if establish_read_bus_idle and not establish_read_only_flash_idle(backend, serial_port):
                return None
            backend.rd_sws_wr_addr_usbcom(serial_port, 0x0B3, bytearray([0x80]))
            backend.rd_sws_wr_addr_usbcom(serial_port, 0x0D, bytearray([0x00]))
            backend.rd_sws_wr_addr_usbcom(
                serial_port, 0x0C, bytearray([0x9F, 0x00])
            )
            backend.rd_sws_wr_addr_usbcom(serial_port, 0x0D, bytearray([0x0A]))
            data = backend.sws_read_data(serial_port, 0x0C, 3)
            backend.rd_sws_wr_addr_usbcom(serial_port, 0x0D, bytearray([0x01]))
            backend.rd_sws_wr_addr_usbcom(serial_port, 0x0B3, bytearray([0x00]))
            if data is not None and len(data) == 3:
                if decode_failures:
                    print(
                        "\rRecovered transient SWS Flash-JEDEC read "
                        f"after {decode_failures} retry/retries."
                    )
                return bytes(data)
            decode_failures += 1
        print(
            "\rError Read Flash JEDEC after "
            f"{decode_failures} transient SWS decode failures."
        )
        return None

    def read_flash_status_bounded(serial_port, opcode: int, attempts: int = 6):
        decode_failures = 0
        for _ in range(attempts):
            status = read_flash_status(serial_port, opcode)
            if status is not None:
                if decode_failures:
                    print(
                        "\rRecovered transient SWS Flash-status read "
                        f"after {decode_failures} retry/retries."
                    )
                return status
            decode_failures += 1
        return None

    def flash_ready(serial_port, count: int = 33) -> bool:
        last_status = None
        decode_failures = 0
        for _ in range(count):
            status = read_flash_status(serial_port)
            if status is None:
                decode_failures += 1
                continue
            last_status = status
            if (last_status & 0x01) == 0:
                if decode_failures:
                    print(
                        "\rRecovered transient SWS Flash-status read "
                        f"after {decode_failures} retry/retries."
                    )
                return True
        if last_status is None:
            print(
                "\rError Read Flash Status after "
                f"{decode_failures} transient SWS decode failures."
            )
        else:
            print(
                f"\rTimeout! Flash status 0x{last_status:02x}; "
                f"transient read failures={decode_failures}."
            )
        return False

    def flash_unlock(serial_port) -> bool:
        if expected_flash_jedec is None:
            print("\rNo board-qualified Flash JEDEC ID; refusing destructive access.")
            return False
        observed_jedec = read_flash_jedec(serial_port)
        if observed_jedec is None:
            print("\rCannot read Flash JEDEC ID; refusing destructive access.")
            return False
        if observed_jedec != expected_flash_jedec:
            print(
                f"\rFlash JEDEC {observed_jedec.hex().upper()} does not match "
                f"board contract {expected_flash_jedec.hex().upper()}; refusing."
            )
            return False
        qualified_recipes = {
            bytes.fromhex("EB6014"): "TH25Q-80UA",
            bytes.fromhex("EB6013"): "TH25Q-40UA exact-status pilot",
            bytes.fromhex("856013"): "P25Q40SH",
        }
        if observed_jedec not in qualified_recipes:
            print(
                f"\rFlash JEDEC {observed_jedec.hex().upper()} has no qualified "
                "InkTag status-register recipe; refusing."
            )
            return False

        status1 = read_flash_status_bounded(serial_port, 0x05)
        status2 = read_flash_status_bounded(serial_port, 0x35)
        if status1 is None or status2 is None:
            print("\rCannot read both Flash status bytes; refusing destructive access.")
            return False
        flash_name = qualified_recipes[observed_jedec]
        if (status1 & 0x80) or (status2 & 0x01):
            print(
                f"\r{flash_name} status 0x{status2:02X}{status1:02X} has SRP "
                "locking enabled; refusing to alter it."
            )
            return False

        # Preserve the original 00:2C/00:00 pilot. Exact HSEL3 additionally
        # proves38:2C; its stock94F8 writer sends06,01,ONE status byte then CS
        # high (9514..9524). For that exact state use only this one-byte form,
        # never submit/interpret/clear SR2=38. Unknown states still refuse;
        # this is not adoption of the 8-Mbit part's whole status map.
        if observed_jedec == bytes.fromhex("EB6013") and (
            status2 not in (0x00, 0x38) or status1 not in (0x2C, 0x00)
        ):
            print(
                f"\rTH25Q-40UA unqualified status "
                f"0x{status2:02X}{status1:02X}; refusing."
            )
            return False

        if observed_jedec == bytes.fromhex("856013") or (
            observed_jedec == bytes.fromhex("EB6013") and status2 == 0x38
        ):
            protection = status1 & 0x7C
            print(
                f"Flash JEDEC {observed_jedec.hex().upper()} ({flash_name}), "
                f"status=0x{status2:02X}{status1:02X}."
            )
            if protection:
                # One-byte WRSR leaves the second register untouched. For
                # HSEL3 this transaction is independently stock-proven; admit
                # only2C->00 with the complete second byte38 preserved.
                new_status1 = status1 & ~0x7C
                backend.FlashWriteEnable(serial_port)
                backend.rd_sws_wr_addr_usbcom(
                    serial_port, 0x0D, bytearray([0x00])
                )
                backend.rd_sws_wr_addr_usbcom(
                    serial_port, 0x0C, bytearray([0x01])
                )
                backend.rd_sws_wr_addr_usbcom(
                    serial_port, 0x0C, bytearray([new_status1])
                )
                backend.rd_sws_wr_addr_usbcom(
                    serial_port, 0x0D, bytearray([0x01])
                )
                if not flash_ready(serial_port):
                    print(f"\r{flash_name} status-register write did not complete.")
                    return False
                verify1 = read_flash_status_bounded(serial_port, 0x05)
                verify2 = read_flash_status_bounded(serial_port, 0x35)
                if verify1 is None or verify2 is None:
                    print(f"\rCannot verify {flash_name} status after unlock.")
                    return False
                if observed_jedec == bytes.fromhex("EB6013") and verify1 != 0x00:
                    print("\rTH25Q-40UA first status did not become exactly00; refusing.")
                    return False
                if verify1 & 0x7C:
                    print(
                        f"\r{flash_name} remains protected: "
                        f"status=0x{verify2:02X}{verify1:02X}."
                    )
                    return False
                if (verify1 & 0x80) != (status1 & 0x80) or verify2 != status2:
                    print(
                        f"\r{flash_name} non-protection status changed unexpectedly: "
                        f"0x{status2:02X}{status1:02X} -> "
                        f"0x{verify2:02X}{verify1:02X}."
                    )
                    return False
                print(
                    f"{flash_name} array protection cleared and verified: "
                    f"status=0x{verify2:02X}{verify1:02X}."
                )
                return True

            print(
                f"{flash_name} status 0x{status2:02X}{status1:02X}: "
                "array protection is clear; "
                "skipping status-register write."
            )
            return True

        protection = (status1 & 0x7C) or (status2 & 0x40)
        print(f"Flash JEDEC {observed_jedec.hex().upper()} "
              f"({flash_name}), status=0x{status2:02X}{status1:02X}.")
        if protection:
            # TH25Q-80UA defines BP4..BP0 at S6..S2 and CMP at S14.
            # Preserve QE and the OTP security-lock bits while clearing only
            # the array-protection fields. The exact JEDEC ID is checked above.
            new_status1 = status1 & ~0x7C
            new_status2 = status2 & ~0x40
            backend.FlashWriteEnable(serial_port)
            backend.rd_sws_wr_addr_usbcom(
                serial_port, 0x0D, bytearray([0x00])
            )
            backend.rd_sws_wr_addr_usbcom(
                serial_port, 0x0C, bytearray([0x01])
            )
            backend.rd_sws_wr_addr_usbcom(
                serial_port, 0x0C, bytearray([new_status1])
            )
            backend.rd_sws_wr_addr_usbcom(
                serial_port, 0x0C, bytearray([new_status2])
            )
            backend.rd_sws_wr_addr_usbcom(
                serial_port, 0x0D, bytearray([0x01])
            )
            if not flash_ready(serial_port):
                print(f"\r{flash_name} status-register write did not complete.")
                return False
            verify1 = read_flash_status_bounded(serial_port, 0x05)
            verify2 = read_flash_status_bounded(serial_port, 0x35)
            if verify1 is None or verify2 is None:
                print(f"\rCannot verify {flash_name} status after unlock.")
                return False
            if (verify1 & 0x7C) or (verify2 & 0x40):
                print(
                    f"\r{flash_name} remains protected: "
                    f"status=0x{verify2:02X}{verify1:02X}."
                )
                return False
            if (verify1 & 0x80) != (status1 & 0x80) or (
                verify2 & 0x3B
            ) != (status2 & 0x3B):
                print(
                    f"\r{flash_name} non-protection status changed unexpectedly: "
                    f"0x{status2:02X}{status1:02X} -> "
                    f"0x{verify2:02X}{verify1:02X}."
                )
                return False
            print(
                f"{flash_name} array protection cleared and verified: "
                f"status=0x{verify2:02X}{verify1:02X}."
            )
            return True

        print(
            f"{flash_name} status 0x{status2:02X}{status1:02X}: "
            "array protection is clear; "
            "skipping status-register write."
        )
        return True

    backend.InkTagReadFlashStatus = read_flash_status
    backend.InkTagReadFlashStatusBounded = read_flash_status_bounded
    backend.InkTagReadFlashJedec = read_flash_jedec
    backend.FlashReady = flash_ready
    backend.FlashUnlock = flash_unlock


def install_flash_read_retry(
    backend: ModuleType, expected_readback: bytes | None = None,
    single_pass_unknown: bool = False,
    verify_stride: int = 1,
    *, establish_read_bus_idle: bool = False,
) -> None:
    """Retry Flash reads; verify against source or two matching live reads."""

    if verify_stride < 1:
        raise ValueError("verify_stride must be >= 1")
    if verify_stride != 1 and expected_readback is None:
        raise ValueError("sampled verification requires an expected image")

    def read_chunk(serial_port, offset: int, size: int):
        def setup(address, data):
            # UART loopback completion is necessary, not a device ACK. Stop
            # on missing echo rather than reading stale MSPI status as data.
            ok = backend.rd_sws_wr_addr_usbcom(serial_port, address, data)
            if not ok:
                print(f"SWS setup failed: UART echo incomplete, register 0x{address:04X}, Flash offset 0x{offset:06X}.")
            return ok
        if establish_read_bus_idle and not establish_read_only_flash_idle(backend, serial_port):
            return None
        if not setup(0x0B3, bytearray([0x80])):
            return None
        if not setup(0x0D, bytearray([0x00])):
            backend.rd_sws_wr_addr_usbcom(serial_port, 0x0B3, bytearray([0]))
            return None
        ok = setup(
            0x0C,
            bytearray(
                [
                    0x03,
                    (offset >> 16) & 0xFF,
                    (offset >> 8) & 0xFF,
                    offset & 0xFF,
                    0x00,
                ]
            ),
        )
        ok = ok and setup(0x0D, bytearray([0x0A]))
        data = backend.sws_read_data(serial_port, 0x0C, size) if ok else None
        backend.rd_sws_wr_addr_usbcom(
            serial_port, 0x0D, bytearray([0x01])
        )
        backend.rd_sws_wr_addr_usbcom(
            serial_port, 0x0B3, bytearray([0x00])
        )
        return None if data is None or len(data) != size else bytes(data)

    def flash_read_block(
        serial_port, stream, offset: int = 0, size: int = 0x80000
    ) -> bool:
        if expected_readback is not None and len(expected_readback) != size:
            print(
                "\rExpected readback image size does not match requested Flash range."
            )
            return False
        offset &= 0x00FFFFFF
        consumed = 0
        completed_bytes = 0
        page_index = 0
        page_count = (size + 0xFF) // 0x100
        selected_bytes = sum(min(0x100, size - page * 0x100) for page in range(page_count)
            if expected_readback is None or page % verify_stride == 0 or page == page_count - 1)
        action = "Reading" if expected_readback is None else "Read/verify"
        print(f"{action} 0.0% (0.00/{selected_bytes / 1024:.2f} KiB)", flush=True)
        first_completed_at = None
        last_report_at = time.monotonic()
        while consumed < size:
            # Start every read at the backend's normal 256-byte frame size.
            # Unknown live data is accepted only after two matching reads; if
            # a long SWS frame is unstable, retry that frame as independently
            # confirmed 32-byte pieces instead of slowing the entire dump.
            read_size = min(0x100, size - consumed)
            if (
                expected_readback is not None
                and page_index % verify_stride != 0
                and page_index != page_count - 1
            ):
                consumed += read_size
                offset += read_size
                page_index += 1
                continue
            expected_chunk = (
                None
                if expected_readback is None
                else expected_readback[consumed : consumed + read_size]
            )
            action = "Read/confirm" if expected_chunk is None else "Read/verify"
            print(f"\r{action} from 0x{offset:06x}...", end="")
            if expected_chunk is None:
                if single_pass_unknown:
                    # Retry only absent/invalid frames at this same offset;
                    # no reset, full-capture replay, or decoded-data voting.
                    for attempt in range(6):
                        data = read_chunk(serial_port, offset, read_size)
                        if data is not None:
                            break
                    if data is None:
                        print(
                            "\rError: could not read Flash data at "
                            f"0x{offset:06x}!"
                        )
                        return False
                else:
                    previous_chunk = None
                    data = None
                    for attempt in range(3):
                        candidate = read_chunk(serial_port, offset, read_size)
                        if candidate is not None and candidate == previous_chunk:
                            data = candidate
                            break
                        if candidate is not None:
                            previous_chunk = candidate

                if data is None and not single_pass_unknown:
                    print(
                        "\rUnstable 256-byte SWS frame at "
                        f"0x{offset:06x}; confirming 32-byte pieces."
                    )
                    confirmed = bytearray()
                    part_consumed = 0
                    while part_consumed < read_size:
                        part_size = min(0x20, read_size - part_consumed)
                        part_offset = offset + part_consumed
                        previous_part = None
                        for _ in range(6):
                            candidate = read_chunk(
                                serial_port, part_offset, part_size
                            )
                            if (
                                candidate is not None
                                and candidate == previous_part
                            ):
                                confirmed.extend(candidate)
                                break
                            if candidate is not None:
                                previous_part = candidate
                        else:
                            print(
                                "\rError: could not confirm Flash data at "
                                f"0x{part_offset:06x} after 6 reads!"
                            )
                            return False
                        part_consumed += part_size
                    data = bytes(confirmed)
                stream.write(data)
            else:
                missing = 0
                mismatched = 0
                candidates = []
                failed_blocks = {}
                for attempt in range(6):
                    data = read_chunk(serial_port, offset, read_size)
                    if data == expected_chunk:
                        if attempt:
                            print(
                                "\rConfirmed SWS Flash data at "
                                f"0x{offset:06x} after {attempt + 1} read(s)."
                            )
                        stream.write(data)
                        break
                    if data is None:
                        missing += 1
                    else:
                        mismatched += 1
                        differences = [i for i, (left, right) in enumerate(zip(data, expected_chunk)) if left != right]
                        first_difference = differences[0] if differences else None
                        evidence = dict(attempt=attempt + 1, bytes=len(data), sha256=hashlib.sha256(data).hexdigest().upper(),
                            firstDifference=first_difference, differentBytes=len(differences),
                            zeroBytes=data.count(0), ffBytes=data.count(255),
                            status2CBytes=data.count(0x2C), uniform=len(set(data)) == 1)
                        failed_blocks.setdefault(evidence['sha256'], (attempt + 1, bytes(data)))
                        # Only the non-secret image header gets a hex preview;
                        # later Flash/credential content stays out of logs.
                        if offset == 0:
                            evidence['headerPrefixHex'] = data[:32].hex().upper()
                        candidates.append(evidence)
                else:
                    print(f"\nVERIFY DIAGNOSTIC: offset=0x{offset:06X}; absent/invalid={missing}; decoded mismatch={mismatched}; distinct candidate hashes={len({item['sha256'] for item in candidates})}.")
                    if candidates and offset == 0:
                        print("VERIFY DIAGNOSTIC: expected header=" + expected_chunk[:32].hex().upper() + "; first received header=" + candidates[0]['headerPrefixHex'] + ".")
                    name = getattr(stream, 'name', None)
                    if isinstance(name, (str, Path)):
                        evidence_path = Path(name).with_suffix('.mismatch.json')
                        # At most six distinct 256-byte blocks, stored only
                        # beside ignored failed-run evidence. Never publish as
                        # a capture, print their private contents or overwrite.
                        raw_blocks = []
                        for digest, (attempt, block) in failed_blocks.items():
                            block_path = Path(name).with_suffix(f'.mismatch-{attempt}.bin')
                            with block_path.open('xb') as block_stream:
                                block_stream.write(block)
                            raw_blocks.append(dict(path=block_path.name, bytes=len(block), sha256=digest))
                        with evidence_path.open('x', encoding='utf-8') as evidence_stream:
                            json.dump(dict(schema='inktag-sws-failed-block-v1', offset=offset,
                                expectedBytes=len(expected_chunk), expectedSha256=hashlib.sha256(expected_chunk).hexdigest().upper(),
                                noFrameCount=missing, decodedMismatchCount=mismatched, candidates=candidates,
                                rawDiagnosticBlocks=raw_blocks,
                                captureQualified=False, flashWrites=False), evidence_stream, indent=2)
                        print(f"VERIFY DIAGNOSTIC: saved failed-block metadata to {evidence_path}; not a verified capture.")
                    print(
                        "\rError: could not verify Flash data at "
                        f"0x{offset:06x} after 6 reads! No/invalid frame={missing}; decoded mismatch={mismatched}."
                    )
                    return False
            consumed += read_size
            offset += read_size
            page_index += 1
            completed_bytes += len(data)
            now = time.monotonic()
            if first_completed_at is None:
                first_completed_at = now
            if completed_bytes % 0x1000 == 0 or completed_bytes == selected_bytes or now - last_report_at >= 1.0:
                action = "Reading" if expected_readback is None else "Read/verify"
                percent = 100.0 if completed_bytes == selected_bytes else min(99.9, completed_bytes * 100 / selected_bytes)
                print(f"\r{action} {percent:.1f}% "
                      f"({completed_bytes / 1024:.2f}/{selected_bytes / 1024:.2f} KiB); "
                      f"elapsed {now - first_completed_at:.1f}s", flush=True)
                last_report_at = now
        print("\r                               \r", end="")
        return True

    def set_expected_readback(data):
        nonlocal expected_readback
        if data is not None and type(data) is not bytes:
            raise ValueError('Readback expectation must be exact bytes or None')
        expected_readback=data

    backend.InkTagSetExpectedReadback = set_expected_readback
    backend.FlashReadBlock = flash_read_block


def install_whole_chip_reset(backend: ModuleType) -> None:
    """Use the documented TLSR8359 whole-chip reset bit after programming.

    The reviewed legacy backend writes 0x22 to SWS register 0x006F for its
    ``-r`` action. TLSR8359 defines bit 5 as the whole-chip reset request;
    bit 1 is unrelated and is not needed. Keep the backend untouched on disk
    and narrow the compatibility correction to that one exact write.
    """

    original_write = backend.sws_wr_addr_usbcom

    def sws_write(serial_port, address: int, data):
        if address == 0x006F and bytes(data) == b"\x22":
            print("Run application with TLSR8359 whole-chip reset (0x20).")
            data = bytearray([0x20])
        return original_write(serial_port, address, data)

    backend.sws_wr_addr_usbcom = sws_write


def install_probe_pc_readback(backend: ModuleType) -> None:
    """Bounded read-only PC retries; one non-Flash release on failed probe."""
    original_read = backend.sws_read_data
    def read(serial_port, address, size=1):
        if address != 0x06BC or size != 4:
            return original_read(serial_port, address, size)
        for attempt in range(1, 4):
            data = original_read(serial_port, address, size)
            if data is not None and len(data) == 4:
                return data
            print(f"PC readback incomplete ({attempt}/3); retry is read-only, no Flash access.")
        print("RESET SWS: releasing CPU stop and requesting one whole-chip reset after failed probe; recovery is unconfirmed.")
        try:
            backend.sws_wr_addr_usbcom(serial_port, 0x0602, bytearray([0x88]))
            backend.sws_wr_addr_usbcom(serial_port, 0x006F, bytearray([0x20]))
            if hasattr(serial_port, "flush"):
                serial_port.flush()
        except OSError as exc:
            print(f"Probe CPU release failed: {exc}; no retry or Flash access.")
        return None
    backend.sws_read_data = read


def extract_contact_args(arguments):
    filtered, values = [], {}
    flags = {"--inktag-retry-detect", "--inktag-detect-timeout", "--inktag-detect-interval"}
    index = 0
    while index < len(arguments):
        key = arguments[index]
        if key not in flags:
            filtered.append(key)
            index += 1
            continue
        if key in values:
            raise SystemExit(f"Duplicate contact option: {key}")
        if key == "--inktag-retry-detect":
            values[key] = True
            index += 1
            continue
        if index + 1 >= len(arguments):
            raise SystemExit(f"Missing contact option value: {key}")
        try:
            value = int(arguments[index + 1])
        except ValueError:
            raise SystemExit(f"Contact option must be an integer: {key}") from None
        if value < (1 if key.endswith("interval") else 0):
            raise SystemExit(f"Invalid contact option: {key}")
        values[key] = value
        index += 2
    if values and ("--inktag-retry-detect" not in values or "rf" not in filtered):
        raise SystemExit("Contact retry is limited to read activation; no write/probe operation replay.")
    return filtered, values.get("--inktag-retry-detect", False), values.get("--inktag-detect-timeout", 0), values.get("--inktag-detect-interval", 1)


def extract_inktag_args(
    arguments: list[str],
) -> tuple[list[str], bytes | None, Path | None, bool, bool, int, int | None, bool]:
    filtered: list[str] = []
    expected_flash_jedec: bytes | None = None
    verify_file: Path | None = None
    hold_on_timeout = False
    single_pass_read = False
    verify_stride = 1
    operation_baud: int | None = None
    report_flash_jedec = False
    index = 0
    while index < len(arguments):
        if arguments[index] == "--inktag-hold-on-timeout":
            hold_on_timeout = True
            index += 1
            continue
        if arguments[index] == "--inktag-single-pass-read":
            single_pass_read = True
            index += 1
            continue
        if arguments[index] == "--inktag-report-flash-jedec":
            report_flash_jedec = True
            index += 1
            continue
        if arguments[index] == "--inktag-verify-file":
            if verify_file is not None or index + 1 >= len(arguments):
                raise SystemExit("--inktag-verify-file requires one unique path")
            verify_file = Path(arguments[index + 1]).resolve()
            if not verify_file.is_file():
                raise SystemExit(f"InkTag verify file not found: {verify_file}")
            index += 2
            continue
        if arguments[index] == "--inktag-verify-stride":
            if verify_stride != 1 or index + 1 >= len(arguments):
                raise SystemExit("--inktag-verify-stride requires one unique integer")
            try:
                verify_stride = int(arguments[index + 1], 10)
            except ValueError as exc:
                raise SystemExit("--inktag-verify-stride must be an integer >= 1") from exc
            if verify_stride < 1:
                raise SystemExit("--inktag-verify-stride must be an integer >= 1")
            index += 2
            continue
        if arguments[index] == "--inktag-operation-baud":
            if operation_baud is not None or index + 1 >= len(arguments):
                raise SystemExit("--inktag-operation-baud requires one unique integer")
            try:
                operation_baud = int(arguments[index + 1], 10)
            except ValueError as exc:
                raise SystemExit(
                    "--inktag-operation-baud must be an integer >= 340000"
                ) from exc
            if operation_baud < 340000:
                raise SystemExit("--inktag-operation-baud must be an integer >= 340000")
            index += 2
            continue
        if arguments[index] != "--inktag-flash-jedec":
            filtered.append(arguments[index])
            index += 1
            continue
        if expected_flash_jedec is not None or index + 1 >= len(arguments):
            raise SystemExit("--inktag-flash-jedec requires one unique 6-hex value")
        try:
            expected_flash_jedec = bytes.fromhex(arguments[index + 1])
        except ValueError as exc:
            raise SystemExit("--inktag-flash-jedec must be exactly 6 hex digits") from exc
        if len(expected_flash_jedec) != 3:
            raise SystemExit("--inktag-flash-jedec must be exactly 6 hex digits")
        index += 2
    return (
        filtered, expected_flash_jedec, verify_file, hold_on_timeout,
        single_pass_read, verify_stride, operation_baud, report_flash_jedec,
    )


def extract_reset_pulse(arguments):
    filtered, pulse, seen = [], 500, False
    index = 0
    while index < len(arguments):
        if arguments[index] != '--inktag-reset-pulse-ms':
            filtered.append(arguments[index])
            index += 1
            continue
        if seen or index + 1 >= len(arguments):
            raise SystemExit('RESET pulse requires one unique value in 50..2000 ms')
        try:
            pulse = int(arguments[index + 1])
        except ValueError:
            raise SystemExit('RESET pulse must be an integer in 50..2000 ms') from None
        if not 50 <= pulse <= 2000:
            raise SystemExit('RESET pulse must be 50..2000 ms')
        seen = True
        index += 2
    return filtered, pulse


def extract_full_write_plan(arguments):
    option='--inktag-full-write-plan'
    args=list(arguments)
    if option not in args:return args,None
    if args.count(option)!=1 or args.index(option)+1>=len(args):
        raise SystemExit('Full needs exactly one bound write plan')
    index=args.index(option);path=Path(args[index+1]).resolve(strict=True)
    return args[:index]+args[index+2:],path


def extract_read_unit_uid(arguments):
    option='--inktag-read-unit-uid'
    args=list(arguments)
    if option not in args:return args,None
    if args.count(option)!=1 or args.index(option)+1>=len(args):
        raise SystemExit('Bounded capture requires one exact Flash UID')
    index=args.index(option)
    try:uid=bytes.fromhex(args[index+1])
    except ValueError:raise SystemExit('Invalid capture UID') from None
    if len(uid)!=16 or uid in (bytes(16),b'\xff'*16,b'\x51\x01'*8):raise SystemExit('Invalid capture UID')
    return args[:index]+args[index+2:],uid


def install_read_unit_guard(backend,uid,read_uid,confirmed_uid):
    original=backend.FlashReadBlock
    def same_unit_read(port,stream,offset=0,size=0x80000):
        if confirmed_uid([read_uid(backend,port) for _ in range(3)])!=uid:
            raise ValueError('Bounded capture is not the identified unit; no array read')
        return original(port,stream,offset,size)
    backend.FlashReadBlock=same_unit_read


def load_flash_identify():
    spec=importlib.util.spec_from_file_location('inktag_full_uid',Path(__file__).with_name('tlsr825x-flash-identify.py'))
    identify=importlib.util.module_from_spec(spec);spec.loader.exec_module(identify)
    return identify


def extract_post_write_verify(arguments, *, sparse=False):
    """One write, then complete readback on the same halted serial session."""
    option = '--inktag-post-write-verify-file'
    if option not in arguments:
        return list(arguments), None
    if arguments.count(option) != 1:
        raise SystemExit('Duplicate post-write verification option.')
    index = arguments.index(option)
    if index + 1 >= len(arguments):
        raise SystemExit('Missing post-write verification output.')
    output = Path(arguments[index + 1]).resolve()
    filtered = arguments[:index] + arguments[index + 2:]
    if 'wf' not in filtered or any(op in filtered for op in ('rf', 'we', 'es', 'ea')):
        raise SystemExit('Post-write verification requires one wf operation.')
    position = filtered.index('wf')
    if position + 3 != len(filtered) or int(filtered[position + 1], 0) != 0:
        raise SystemExit('Post-write verification is restricted to a zero-based Full.')
    source = Path(filtered[position + 2]).resolve(strict=True)
    expected = source.read_bytes()
    sizes=(524288,1048576) if sparse else (524288,)
    if len(expected) not in sizes or output.exists() or output == source:
        raise SystemExit('Full verification requires a bounded image and a new distinct output.')
    return filtered, (output, expected)


class _FlashWriteProgress:
    """Presentation-only stream proxy for the pinned backend's page loop.

    It reads one page, completes erase/program (or skips an erased FF page),
    and only then reads the next page. That next read confirms the previous
    prefix. The last page counts only after FlashWriteBlock returns True.
    No address-start line, stream prefetch or elapsed heartbeat is completion.
    """

    def __init__(self, stream, total):
        self.stream = stream
        self.total = total
        self.pending = self.completed = 0
        self.first_completed_at = None
        self.last_report_at = time.monotonic()
        print(f"Programming 0.0% (0.00/{total / 1024:.2f} KiB)", flush=True)

    def __getattr__(self, name):
        return getattr(self.stream, name)

    def _confirm_previous(self, final=False):
        self.completed += self.pending
        self.pending = 0
        now = time.monotonic()
        if self.completed and self.first_completed_at is None:
            self.first_completed_at = now
        if self.completed and (final or self.completed % 0x1000 == 0 or now - self.last_report_at >= 1.0):
            # Never round an unfinished prefix up to a displayed 100%.
            percent = 100.0 if final and self.completed == self.total else min(99.9, self.completed * 100 / self.total)
            print(f"\nProgramming {percent:.1f}% "
                  f"({self.completed / 1024:.2f}/{self.total / 1024:.2f} KiB); "
                  f"elapsed {now - self.first_completed_at:.1f}s", flush=True)
            self.last_report_at = now

    def read(self, *args, **kwargs):
        self._confirm_previous()
        data = self.stream.read(*args, **kwargs)
        self.pending = len(data)
        return data

    def finish(self):
        self._confirm_previous(final=True)


def extract_skip_readback(arguments):
    """Explicit Full-only exception; never an automatic failure fallback."""
    option = '--inktag-skip-post-write-readback'
    count = arguments.count(option)
    if count > 1:
        raise SystemExit('Duplicate skip-readback option.')
    if count and '--inktag-post-write-verify-file' not in arguments:
        raise SystemExit('Skip-readback requires the bounded Full operation.')
    return [arg for arg in arguments if arg != option], bool(count)


def install_post_write_verify(backend, output, expected, *, skip_readback=False):
    original_write = backend.FlashWriteBlock

    def write_and_verify(serial_port, stream, offset, size, erase=True):
        if offset != 0 or size != 524288 or not erase:
            raise SystemExit('Invalid same-session Full range; no write.')
        position = stream.tell()
        if position != 0 or stream.read() != expected:
            raise SystemExit('Full input changed before write; no write.')
        stream.seek(position)
        # Reserve evidence before the first erase, never overwrite a prior run.
        with (nullcontext(None) if skip_readback else output.open('xb')) as readback:
            print('PROGRAMMING: keep power/SWS stable; target LEDs may stay on or off while the CPU is halted. Progress is processed range, not readback verification.', flush=True)
            progress = _FlashWriteProgress(stream, size)
            if not original_write(serial_port, progress, offset, size, erase):
                print('FULL WRITE FAILED/UNKNOWN: no retry, reset or readback.')
                return False
            progress.finish()
            if skip_readback:
                print('FULL WRITE COMPLETE / READBACK SKIPPED: explicit request; NOT VERIFIED. No readback file or matched receipt.', flush=True)
                return True
            print('FULL READBACK: verifying every byte on the same halted SWS session; please wait. No intervening reset.', flush=True)
            if not backend.FlashReadBlock(serial_port, readback, offset, size):
                print('FULL READBACK FAILED: no retry of the write or CPU run.')
                return False
        actual = output.read_bytes()
        if actual != expected:
            print('FULL READBACK MISMATCH: CPU remains halted; installation unconfirmed.')
            return False
        print('FULL READBACK MATCHED: 524288 bytes; SHA256=' + hashlib.sha256(actual).hexdigest().upper())
        return True

    backend.FlashWriteBlock = write_and_verify


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit("usage: tlsr825x-early-activate.py BACKEND [backend arguments]")
    backend_path = Path(sys.argv[1]).resolve()
    if not backend_path.is_file():
        raise SystemExit(f"TLSR825x backend not found: {backend_path}")
    unit_args,read_unit_uid=extract_read_unit_uid(sys.argv[2:])
    plan_args,full_plan=extract_full_write_plan(unit_args)
    requested_args, skip_readback = extract_skip_readback(plan_args)
    full_args, post_verify = extract_post_write_verify(requested_args,sparse=full_plan is not None)
    if full_plan is not None and post_verify is None:
        raise SystemExit('Bound Full plan requires the same-session post-write operation')
    reset_args, reset_pulse_ms = extract_reset_pulse(full_args)
    contact_args, retry_detect, detect_timeout, detect_interval = extract_contact_args(reset_args)
    (backend_args, expected_flash_jedec, verify_file,
     hold_on_timeout, single_pass_read, verify_stride,
     operation_baud, report_flash_jedec) = extract_inktag_args(contact_args)
    if read_unit_uid is not None and ('rf' not in backend_args or post_verify is not None or single_pass_read):
        raise SystemExit('Unit-bound capture requires a confirmed read-only operation')
    if verify_stride != 1 and verify_file is None:
        raise SystemExit("--inktag-verify-stride requires --inktag-verify-file")
    backend = load_backend(backend_path)
    install_windows_serial_partial_write(backend)
    # The probe PC/chip-register reads need the same exact frame boundary as
    # backups. The legacy reader requested a tenth symbol for a nine-symbol
    # byte and could turn a responding target into an incomplete PC result.
    # Destructive/native operations remain unchanged; no Flash write retry.
    probe_command = not any(operation in backend_args for operation in ("rf", "wf", "we", "es", "ea"))
    if "rf" in backend_args or probe_command or post_verify:
        install_exact_sws_frames(backend)
    clock_hz = 0
    for clock_option in ("-c", "--clk"):
        if clock_option in backend_args:
            clock_hz = int(backend_args[backend_args.index(clock_option) + 1]) * 1_000_000
    install_early_activation(backend, hold_on_timeout, operation_baud,
        retry_detect=retry_detect, detect_timeout_seconds=detect_timeout,
        detect_interval_seconds=detect_interval, clock_hz=clock_hz, reset_pulse_ms=reset_pulse_ms)
    install_whole_chip_reset(backend)
    if probe_command:
        install_probe_pc_readback(backend)
    install_flash_status_retry(backend, expected_flash_jedec,
        establish_read_bus_idle="rf" in backend_args)
    install_flash_read_retry(
        backend, post_verify[1] if post_verify and full_plan is None else (None if verify_file is None else verify_file.read_bytes()),
        single_pass_read, verify_stride,
        establish_read_bus_idle="rf" in backend_args or bool(post_verify),
    )
    if post_verify:
        if expected_flash_jedec is None or verify_file or single_pass_read or verify_stride != 1:
            raise SystemExit('Full same-session verification requires exact JEDEC and complete readback.')
        if full_plan is None:
            raise SystemExit('Normal Full requires a current bound write plan; legacy whole-window Full is retired')
        sys.path.insert(0,str(Path(__file__).resolve().parents[6]/'tools/hardware'))
        from tlsr_full_flash_transport import install
        identify=load_flash_identify()
        output,expected=post_verify
        install(backend,Path(backend_args[backend_args.index('wf')+2]),output,full_plan,expected,
            expected_flash_jedec,read_uid=identify.read_uid,confirmed_uid=identify.confirmed_uid,
            progress=_FlashWriteProgress,skip_readback=skip_readback)
    if report_flash_jedec:
        if "rf" not in backend_args:
            raise SystemExit("--inktag-report-flash-jedec requires a read operation")
        original_flash_read = backend.FlashReadBlock

        def read_with_flash_jedec(serial_port, stream, offset=0, size=0x80000):
            print('FLASH BUS: requesting CS idle before the new RDID/read transaction; UART echo is not a target ACK.')
            observed = backend.InkTagReadFlashJedec(serial_port)
            if observed is None:
                return False
            print(f"Observed Flash JEDEC: {observed.hex().upper()}")
            if expected_flash_jedec is not None and observed != expected_flash_jedec:
                print("Flash JEDEC differs from the capture/board contract; no array read.")
                return False
            status1 = backend.InkTagReadFlashStatusBounded(serial_port, 0x05)
            status2 = backend.InkTagReadFlashStatusBounded(serial_port, 0x35)
            if status1 is None or status2 is None:
                print("Cannot read both Flash status bytes.")
                return False
            print(f"Observed Flash status: SR2:SR1={status2:02X}:{status1:02X}")
            return original_flash_read(serial_port, stream, offset, size)

        backend.FlashReadBlock = read_with_flash_jedec
    if read_unit_uid is not None:
        identify=load_flash_identify()
        install_read_unit_guard(backend,read_unit_uid,identify.read_uid,identify.confirmed_uid)
    sys.argv = [str(backend_path), *backend_args]
    backend.main()


if __name__ == "__main__":
    main()
