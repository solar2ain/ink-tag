# OTA updates

An OTA package is valid only for the same board ID. Do not unpack or modify the ZIP, and do not upload a full image as an OTA package.

1. Open InkTag Web and connect the device.
2. Confirm the detected board and current version in the OTA section.
3. Select `InkTag-<board>-v<version>-ota.zip` from the same release.
4. Wait for upload, verification, activation, and BLE advertising to complete.
5. Reconnect and verify the firmware version, time, display profile, and saved settings.

After an interrupted update, avoid repeatedly issuing commands. Wait for the normal or `_OTA` advertisement and follow the platform recovery guide. A stable release must pass normal update, power-loss interruption, corrupt-package rejection, and rollback tests.

## 0.2 transports

| Platform | Update role | Transport and recovery |
|---|---|---|
| Goodix | Original name with `_OTA` suffix | The application requests fixed Recovery; Recovery validates the signed application and returns to it. Reconnect the same device's `_OTA` advertisement after an interruption. |
| OnMicro | Original name with `_OTA` suffix | Fixed Swap Boot/Recovery receives the signed package. Keep power stable until reboot and version readback complete. |
| Telink | Original name with `_OTA` suffix | Fixed Recovery receives the application and attests length/CRC. Experimental boards still require the interruption matrix shown by the support table. |

The CLI can recognize deferred Nordic Secure DFU and Dialog SUOTA transports, but this 0.2 candidate does not attach firmware for those boards.

## Completion criteria

100% host progress means only that transmission ended. Continue waiting for device validation, role transition, restart, and application advertising. Reconnect and read the exact board ID, new version, time, display profile, Bluetooth name, and persistent settings. On devices with image storage, sample the slot directory and slideshow configuration after the update.

Treat any of the following as a failure: a wrong-board package can be sent, an invalid signature/digest is accepted, the application never advertises again, the version does not change, settings disappear, or neither normal nor `_OTA`/Recovery advertising returns after interruption. Stop repeated uploads, retain the log and package SHA-256, and use the recovery path in the full-flashing guide.
