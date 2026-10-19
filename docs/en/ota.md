# OTA and Recovery

OTA updates the application. Use [complete flashing](flashing.md) for first installation, a Recovery upgrade or a device that cannot be recovered wirelessly. Do not modify packages or upload a complete-flashing image as OTA.

Select the package for the exact MCU and board, verify its checksum and use stable power. Settings → Device settings → Firmware validates the file, enters Recovery, transfers the payload and waits for device validation. A verified return to the application and running image completes the update; normal OTA synchronizes time once. File validation or 100% transfer alone is not success.

Recovery normally advertises the corresponding `_OTA` identity. WebUI and CLI offer a guarded return to the application only when the installed Recovery supports it and a valid application is available. CLI uses `recovery enter` and `recovery resume-app`; entry still requires ordinary device authentication.

For errors or an unknown outcome, retain the message and checksum. Do not blindly replay exit, reset or write commands. Equal Bluetooth addresses do not make cached application and Recovery services interchangeable; clients must verify the actual role and identity.
