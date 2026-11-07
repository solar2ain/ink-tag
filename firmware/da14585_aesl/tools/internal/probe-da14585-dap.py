"""Bounded DA14585 SWD probe: DAP reads, never native connect/halt/reset.

DP/AP configuration and power requests are diagnostic interventions, not
undisturbed low-power/clock measurements. No CPU, peripheral or Flash writes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import struct
import time


class WrongTarget(RuntimeError):
    pass


def hardware_status(link):
    """Probe-side telemetry only; never a calibrated device voltage gate."""
    try:
        status = link.hardware_status
        return dict(targetVoltageRawMv=int(status.VTarget), voltageQualified=False,
                    pins={name:int(getattr(status,name)) for name in ('tck','tdi','tdo','tms','tres','trst')})
    except Exception as error:
        return dict(targetVoltageRawMv=None, voltageQualified=False,
                    error=type(error).__name__+': '+str(error))


def validate_range(address, size):
    if not 16 <= size <= 4096 or not 0 <= address <= 0xFFFFFFFF:
        raise ValueError("Capture must be 16..4096 bytes at a uint32 address")
    # CPU SRAM alias, physical 96 KiB SRAM, or OTP. Never arbitrary MMIO.
    windows = ((0, 0x18000), (0x07FC0000, 0x07FD8000),
               (0x07F80000, 0x07F90000))
    if not any(lo <= address and address + size <= hi for lo, hi in windows):
        raise ValueError("Capture must stay inside one SRAM/OTP window")


def probe(link, speed, read_otp=False, capture=None):
    out = {"ok": False, "resetCalled": False, "cpuHaltCalled": False,
           "nativeConnectCalled": False, "cpuOrPeripheralWrites": False,
           "powerMeasurement": False, "cleanupErrors": []}
    ctrl = csw = tar = None
    step = 'validate-read-range'
    try:
        if capture is not None:
            validate_range(*capture)
        step = 'set-swd-speed'
        link.set_speed(speed)
        step = 'configure-coresight'
        session = runpy.run_path(str(Path(__file__).with_name('da14585-dap-session.py')))
        session['configure_preserving_debug_state'](link)
        step = 'read-sw-dp'
        try:
            dp_id = link.coresight_read(0, ap=False)
        except Exception as error:
            out['nativeReadError'] = type(error).__name__+': '+str(error)
            # Completed failure of the first read; no DP/AP/CPU writes yet.
            # The outer wrapper also requires all no-write flags and clean
            # close, unlike a timeout or an unstructured native result.
            raise RuntimeError('SW-DP ID unavailable; check target power and SWD') from error
        out["dpId"] = f"0x{dp_id:08X}"
        if dp_id != 0x0BB11477:
            raise WrongTarget("Not the DA14585 SW-DP; no fallback")
        step = 'dap-power-ack'
        link.coresight_write(0, 0x1E, ap=False)  # clear sticky DAP errors only
        ctrl = link.coresight_read(1, ap=False)
        out["powerRequestBefore"] = f"0x{ctrl & 0x50000000:08X}"
        # The SDK's SWD transactions require overrun detection disabled.
        # Preserve power requests during setup, and restore the old mode in
        # cleanup; a full interface initialization must not clear power first.
        link.coresight_write(1, (ctrl & 14) | 0x50000000, ap=False)
        deadline = time.monotonic() + 1.0
        while True:
            ack = link.coresight_read(1, ap=False)
            if ack & 0xA0000000 == 0xA0000000:
                break
            if time.monotonic() >= deadline:
                raise RuntimeError("DAP power ACK unavailable; target may be asleep")
            time.sleep(0.01)
        step = 'read-ahb-ap'
        link.coresight_write(2, 0xF0, ap=False)
        ap_id = link.coresight_read(3, ap=True)
        out["apId"] = f"0x{ap_id:08X}"
        if ap_id != 0x04770021:
            raise WrongTarget("Not the expected DA14585 AHB-AP")
        link.coresight_write(2, 0, ap=False)
        csw = link.coresight_read(0, ap=True)
        tar = link.coresight_read(1, ap=True)

        def unit(address, size):
            link.coresight_write(0, (csw & ~0x37) | {1: 0x10, 2: 0x11, 4: 0x12}[size], ap=True)
            link.coresight_write(1, address, ap=True)
            value = link.coresight_read(3, ap=True)
            return (value >> (8 * (address & 3))) & ((1 << (size * 8)) - 1)

        def blob(address, size):
            aligned = address & ~3
            result = b"".join(unit(pos, 4).to_bytes(4, "little")
                              for pos in range(aligned, (address + size + 3) & ~3, 4))
            return result[address - aligned:address - aligned + size]

        step = 'read-cpu-id'
        cpuid = unit(0xE000ED00, 4)
        out["cpuid"] = f"0x{cpuid:08X}"
        if cpuid != 0x410CC200:
            raise WrongTarget("Not the expected Cortex-M0; refusing other core")
        # SDK6 defines these as separate 8-bit MMIO registers. Read correct
        # lanes, never assume a neighbouring 32-bit register has the same bus.
        step = 'read-da14585-id'
        chip_id = bytes(unit(0x50003200 + i, 1) for i in range(3))
        out["chipId"] = chip_id.decode("ascii", errors="replace")
        if chip_id != b"585":
            raise WrongTarget("Cortex-M0 is not sufficient: DA14585 chip ID differs")
        step = 'read-revision-and-vectors'
        out["chipRevision"] = unit(0x50003204, 1)
        out["vectorHex"] = blob(0, 16).hex()
        out["cpuHalted"] = bool(unit(0xE000EDF0, 4) & 0x20000)
        if read_otp:
            step = 'read-otp-hints'
            out["otpHints"] = {f"0x{address:08X}": blob(address, size).hex()
                               for address, size in ((0x07FC0000, 16),
                                                     (0x07F8FEC0, 16),
                                                     (0x07F8FFA8, 8))}
        if capture is not None:
            step = 'capture-read-range'
            data = blob(*capture)
            out["capture"] = {"address": capture[0], "bytes": len(data),
                              "sha256": hashlib.sha256(data).hexdigest().upper(),
                              "hex": data.hex()}
        out["ok"] = True
    except Exception as error:
        out["error"] = str(error)
        out["failedStep"] = step
        out["nativeErrorType"] = type(error).__name__
        # Unsupported/failed session setup is not a target-readiness miss.
        # In particular, do not let RetryDetect replay a failed line flush.
        out["fatal"] = isinstance(error, (WrongTarget, ValueError)) or step == 'configure-coresight'
    finally:
        # Only debug-register writes. No DRW write (which would write target
        # memory), CDBGRSTREQ, DHCSR, reset pin, reset/halt/go, or native Connect.
        actions = []
        if csw is not None:
            actions += [(2, 0, False), (0, csw, True)]
        if tar is not None:
            actions.append((1, tar, True))
        if ctrl is not None:
            actions.append((1, ctrl & 0x5000000F, False))
        for reg, value, ap in actions:
            try:
                link.coresight_write(reg, value, ap=ap)
            except Exception as error:
                out["cleanupErrors"].append(str(error))
        if out["cleanupErrors"]:
            out.update(ok=False, fatal=True)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jlink-dll", type=Path, required=True)
    parser.add_argument("--serial", type=int)
    parser.add_argument("--speed", type=int, default=50)
    parser.add_argument("--read-otp-hints", action="store_true")
    parser.add_argument("--capture-address", type=lambda value: int(value, 0))
    parser.add_argument("--capture-size", type=int, default=256)
    parser.add_argument("--capture-output", type=Path)
    args = parser.parse_args()
    if not 1 <= args.speed <= 4000:
        parser.error("--speed must be 1..4000 kHz")
    capture = None
    if args.capture_output is not None:
        capture = (args.capture_address or 0, args.capture_size)
        validate_range(*capture)
        if args.capture_output.exists():
            parser.error("Capture output already exists; never overwrite evidence")
    elif args.capture_address is not None:
        parser.error("--capture-address requires --capture-output")
    expected_dll = "JLink_x64.dll" if struct.calcsize("P") == 8 else "JLinkARM.dll"
    if args.jlink_dll.name.lower() != expected_dll.lower():
        parser.error("J-Link DLL architecture does not match Python")
    import pylink
    # Keep library alive beyond any exception traceback and JLink destructor.
    global _LIBRARY
    _LIBRARY = pylink.library.Library(dllpath=str(args.jlink_dll), use_tmpcpy=False)
    link = pylink.JLink(lib=_LIBRARY, unsecure_hook=lambda *unused: 0)
    result = {"ok": False, "cleanupErrors": [], "fatal": False,
              "resetCalled": False, "cpuHaltCalled": False,
              "nativeConnectCalled": False, "cpuOrPeripheralWrites": False}
    if args.serial is not None:
        result['serial'] = args.serial
    try:
        probes = link.connected_emulators()
        serials = [item.SerialNumber for item in probes]
        serial = args.serial
        if serial is None:
            if len(serials) != 1:
                raise RuntimeError("Exactly one probe required, or specify --serial")
            serial = serials[0]
        if serial not in serials:
            raise RuntimeError("Selected probe unavailable; no fallback")
        link.open(serial_no=serial)
        link.set_tif(pylink.enums.JLinkInterfaces.SWD)
        telemetry = hardware_status(link)
        result = probe(link, args.speed, args.read_otp_hints, capture)
        result["probeHardwareStatus"] = telemetry
        result["serial"] = serial
    except Exception as error:
        result.update(ok=False, error=str(error))
    finally:
        try:
            link.close()
        except Exception as error:
            result.update(ok=False, fatal=True)
            result["cleanupErrors"].append("close: " + str(error))
        del link
    if result["ok"] and capture is not None:
        # Generated diagnostic artifact, never a firmware/source edit.
        with args.capture_output.open("xb") as stream:
            stream.write(bytes.fromhex(result["capture"].pop("hex")))
    print(json.dumps(result))
    return 0 if result["ok"] else (3 if result.get("fatal") else 1)


if __name__ == "__main__":
    raise SystemExit(main())
