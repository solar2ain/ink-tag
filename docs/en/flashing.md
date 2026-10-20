# Initial installation and complete flashing

[中文](../zh-CN/flashing.md) | English

This release provides clients and guides only, with no firmware, wired flashing packages or flashing scripts. A factory device cannot be installed using WebUI or CLI alone; wait for a package and instructions matching the exact device.

For devices already running compatible InkTag firmware, follow [getting started](getting-started.md). Once a matching OTA package is available, use [wireless updates](ota.md) for ordinary application updates; routine settings do not require complete flashing.

## Before preparing hardware

- The same label model or size does not prove identical MCU, board wiring or panel. Check [adapted devices](supported-devices.md) and [devices and panels](devices.md).
- Complete flashing can overwrite factory firmware and user data. Use qualified tools, exact wiring and stable power, and preserve a factory backup.
- Do not substitute similarly named images, guess pads or use generic erase commands instead of missing device-specific instructions.
- Stop on unknown silicon, abnormal power or readback failure; do not repeatedly erase everything. Debug probes may affect operation and power use.

## If a device will not start or connect

Check the battery, power, wiring and browser permissions first. If InkTag Recovery is reachable, follow the wireless-update guide using a matching trusted package; never substitute another board's file when none is available. Client installation does not require Keil, an MCU compiler or a firmware SDK.
