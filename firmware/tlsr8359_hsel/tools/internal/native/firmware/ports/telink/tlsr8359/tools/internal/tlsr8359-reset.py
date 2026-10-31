#!/usr/bin/env python3
"""Pulse only a qualified CH340 RTS-to-target-RESETB connection."""

from __future__ import annotations

import argparse
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", required=True, type=int)
    parser.add_argument("--pulse-ms", required=True, type=int)
    args = parser.parse_args()

    port = serial.Serial(
        args.port,
        args.baud,
        timeout=0.1,
        write_timeout=0.5,
    )
    try:
        # pyserial maps asserted RTS to the CH340's active-low electrical
        # output. DTR stays inactive because it is not part of this fixture.
        port.setDTR(False)
        port.setRTS(False)
        time.sleep(0.1)
        port.setRTS(True)
        time.sleep(args.pulse_ms / 1000.0)
        port.setRTS(False)
        time.sleep(0.1)
    finally:
        try:
            port.setRTS(False)
            port.setDTR(False)
        finally:
            port.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
