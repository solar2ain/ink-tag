# OTA and Recovery

OTA updates the application. Use [complete flashing](flash.md) for first installation, a Recovery upgrade or a device that cannot be recovered wirelessly. Do not modify packages or upload a complete-flashing image as OTA.

Use stable power, connect the device and open Firmware update. When an update is available, choose “Load online firmware” to download and validate the matching package. You can also select a local OTA file for the exact MCU and board. Review the package details, then click “Start OTA”; loading a file does not start an update. WebUI enters Recovery, transfers the payload and waits for device validation. A verified return to the application and running image completes the update; normal OTA synchronizes time once. File validation or 100% transfer alone is not success.

Recovery normally advertises the corresponding `_OTA` identity. WebUI and CLI offer a guarded return to the application only when the installed Recovery supports it and a valid application is available. CLI uses `inktag-cli device -a ADDRESS recovery enter` and `inktag-cli device -a ADDRESS recovery resume-app`; entry still requires ordinary device authentication.

For errors or an unknown outcome, retain the message and checksum. Do not blindly replay exit, reset or write commands. Equal Bluetooth addresses do not make cached application and Recovery services interchangeable; clients must verify the actual role and identity.
