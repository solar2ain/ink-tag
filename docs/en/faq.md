# Frequently asked questions

## Device not found

Check power, Bluetooth permission and the exact name. Close other programs using the same device and allow a short role-transition delay. A timeout alone does not prove damaged firmware or connection contention.

## Slow display updates

Refresh duration depends on colour, temperature, controller and mode. Transfer completion is not screen completion. For errors or abnormal BUSY behavior, check the panel configuration before repeating a refresh.

## Unavailable features and readings

Capabilities depend on hardware, firmware and entitlement. nRF52811 and DA14585 are Free-only and use actual picture capacity without activation. On Pro-capable targets, activation unlocks all available slots. Unavailable measurements display `--`, not zero. Capacity remains the device-reported limit for the current panel. The Free `InkTag` mark cannot be hidden; authorized Pro devices can hide `InkTag Pro`. See [Pro features and activation](pro.md) for the Apple Find My tracker, NFC interaction and code process.

## Time after power loss

A device cannot account for time while unpowered. A saved estimate may be inaccurate; ordinary client connections normally synchronize it. Use CLI `--no-time-sync` during drift observations.

## Wired tools

Use them for first installation, fixed-Recovery updates or a device that cannot be recovered wirelessly. Ordinary application updates use OTA. Reports should include the exact MCU/PCB, firmware, panel and reproduction, never credentials or factory dumps.

## The page opens on a phone but Bluetooth does not connect

Use Chrome or Edge on desktop. On iPhone/iPad, use [Bluefy](https://apps.apple.com/app/id1492822055). On Android, prefer Chrome. Open the site in that browser rather than an embedded WeChat/in-app window, and grant Bluetooth permission.

Use HTTPS or a loopback origin. For phone access to a computer, follow the local-service trusted HTTPS setup. A page loading in Safari or an embedded browser does not mean Bluetooth is available.
