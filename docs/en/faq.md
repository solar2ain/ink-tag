# Frequently asked questions

## Connection and identification

### 1. Why is the device missing from the scan?

Confirm that the device is powered, Bluetooth and scan permissions are enabled, and no other page or application is connected to it. Wait a few seconds and scan again after a disconnect, an ordinary/Find advertising transition, or an OTA/Recovery reboot.

### 2. Why does connection take a long time?

After establishing GATT, the WebUI reads identity, protocol, screen configuration, and capabilities. Windows Bluetooth caching, radio interference, and an active display refresh can add delay. Do not operate one device from multiple WebUI or CLI instances.

### 3. Why does the browser not show a previously authorized device automatically?

Bluetooth permission is jointly managed by the browser and operating system. A device picker may reappear because of privacy policy, an address or Recovery-role change, or a different site origin. When the grant remains restorable, WebUI shows the last device and offers direct reconnect. Use the same HTTPS or `localhost` origin consistently.

### 4. Does WebUI disconnect also disconnect the device?

The disconnect button requests closure of the current GATT connection. After an abnormal browser, Bluetooth-stack, or device interruption, releasing the connection may take a short time. Avoid immediately reconnecting from another page.

## Display and images

### 5. Why is an e-paper refresh slow?

Refresh duration depends on the controller, color type, temperature, and refresh mode. Tri-color, four-color, and cold panels are usually slower than monochrome panels. Data-transfer completion is not display completion; wait for the device's final completion result.

### 6. Can the display refresh at low voltage?

Yes. InkTag does not block display refresh solely because of a valid low-voltage reading. Below 2.40 V it warns about low battery and rejects new Flash writes; below 2.60 V it rejects a new OTA. Mutating operations fail safely when the voltage sample is invalid.

### 7. Why does the image-slot count differ from raw device capacity?

The usable count depends on resolution, color format, qualified Flash capacity, and Free/Pro policy. Free permits up to five slots when the screen's long edge is below 300 pixels and two on normal or larger screens. Actual availability never exceeds safe transactional storage capacity.

### 8. Why can a Free device not remove the InkTag Free mark?

The device enforces `InkTag Free` on every application page, including calendar, clock, device information, and images. WebUI, CLI, and image content cannot remove it. Pro may hide the default `InkTag Pro` mark through settings.

## Find

### 9. The device is advertising. Why is there no new location?

Find locations are crowd reports from nearby Apple devices; InkTag does not determine its own position. Valid advertising only proves that the tag is transmitting a Find frame. No participating Apple device nearby, unavailable networking, or Apple processing delay can postpone a report. WebUI does not fabricate or duplicate points.

### 10. Why can Find pause after a reboot?

Rotating public keys require valid UTC time. A platform that cannot retain trusted time while unpowered uses its registered fallback key after reboot until WebUI or CLI synchronizes the clock. Rotation then resumes for the current time slot, while a new crowd report may still arrive later.

### 11. Why does “query all keys” report an incompatible Apple response?

This feature depends on a private Apple interface. An expired session, network or proxy failure, throttling, or a transient response change can fail a query. Verify Apple login and networking, then retry later. This error does not by itself mean advertising stopped or the local keyring is corrupt.

### 12. Why did a successful query add no track point?

WebUI deduplicates reports by identity, time, and position, and determines the latest position from all saved track data. An old or already saved Apple report can make the query succeed without adding a unique point.

### 13. Why is Find hidden or unavailable?

Find requires Pro plus support from both the platform firmware and exact board. A locally remembered Pro device may expose the navigation entry, but each connected device still enforces its own activation and capability. DA14585 and nRF52811 are Free-only and do not provide Find.

## OTA and recovery

### 14. Why can OTA not start?

Common causes are voltage below 2.60 V, an invalid voltage sample, a package/board mismatch, a busy device, or selection of the wrong Recovery device. Do not bypass board, signature, version, or voltage checks. Completion includes application reconnect, version verification, and clock synchronization.

### 15. When is a full wired flash required?

Prefer OTA for ordinary application updates. Use an exact-board full image only for first installation, a Recovery change, an unbootable OTA/application path, or an explicit release instruction. Back up stock firmware and verify power and wiring first.

## Problem reports

### 16. What should a problem report include?

Include the advertised name, board ID, firmware version, screen configuration, operation time, WebUI/CLI version, full error text, and reproducible steps. Never publish Apple credentials, Find private keys, activation secrets, or stock-firmware backups.
