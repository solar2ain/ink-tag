# Web console user guide

InkTag Web has three runtime modes: the Python local server, an HTTPS static site such as GitHub Pages, and a standalone single HTML file. None requires Node.js at runtime. Use a current Chrome or Edge build. Bluetooth access requires HTTPS, `localhost`, or a browser environment that explicitly permits local-file Bluetooth access.

## Connect a device

1. Power the tag normally and make sure first-flash or Recovery wiring is no longer asserted.
2. Select **Connect device**, then choose the InkTag advertisement whose suffix matches the physical tag.
3. Wait for the board ID, firmware version, capabilities, and display profile. Do not send display or OTA commands if the board cannot be identified.
4. Select **Connect device** again to choose another tag. Unsupported controls are hidden according to device capabilities.

The browser remembers authorized Bluetooth devices, although the operating system may still ask for a selection. The normal application and `_OTA`/Recovery advertisements are different execution roles of the same tag; complete any authorization requested during an update.

## Display, calendar, and clock

- Before first use, verify panel size, colors, controller, and orientation. Save the profile and reconnect once to confirm persistence.
- Clear, calendar, clock, and image transfer are mutually exclusive. Do not remove power while refresh is active. Use Stop and wait for the device acknowledgement before starting a different operation.
- A completed BUSY phase means the controller reported refresh completion; it is not merely the end of BLE transfer. Tri-color, four-color, and cold panels can take substantially longer.
- Stop and recheck the profile after mirrored output, clipping, color bars, an all-red/all-black result, or a BUSY timeout. Avoid repeated refresh attempts with an uncertain profile.

## Images and slots

Crop the source for the panel, then inspect the generated black, white, and color planes. The final transfer uses the physical resolution reported by the device. Canvas rotation is an editing aid and should not require a second manual rotation before sending. Displays without grayscale capability do not expose grayscale-darkening controls.

Devices with image storage can keep multiple device-ready frames and run a slideshow. Changing to a profile with a different resolution may make stored images incompatible; export them or accept the cleanup prompt before writing replacements.

## OTA

The OTA section at the bottom of the page is enabled only after a recognized device reports a supported transport. Select an `ota.zip` for the exact board ID and wait for upload, validation, role transition, reconnect, and version confirmation. See [OTA updates](ota.md) for recovery details.

## Privacy and troubleshooting

The static WebUI processes images and Bluetooth traffic locally in the browser. Network access occurs only for features the user explicitly enables; read the related privacy notice before using them.

- No device appears: check system Bluetooth and scan permissions, target power, filters, and other pages/apps that may own the connection.
- GATT disconnects: wait for display refresh, power-cycle the tag, and reconnect from one control page only.
- Holiday data fails: use HTTP/HTTPS and verify that all static assets were deployed. Failure must not block the basic calendar page.
- The page reports an old version: refresh site caches or download the standalone HTML again from the same Release.
- Unexpected hardware behavior: retain the page log, board ID, profile, firmware version, and reproduction steps, but do not publish device secrets or factory firmware.
