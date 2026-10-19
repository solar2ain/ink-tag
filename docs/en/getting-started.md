# Getting started

## Meet e-paper

E-paper offers a paper-like reading experience without a continuously illuminated backlight. It can be comfortable under suitable lighting, but “eye-friendly” is not a medical guarantee. Use suitable lighting, viewing distance and breaks. Read [Good Display's e-paper FAQ](https://www.good-display.com/faq/1.html) for more information.

E-paper uses ambient light and needs external lighting in the dark. A full refresh commonly takes several seconds to around fifteen seconds; some colour panels or low temperatures take longer. Flashing is usually normal: keep power stable and wait rather than repeatedly starting operations.

The panel needs no sustaining power to retain the finished image, even after power is removed. Infrequent refreshes and low-power standby can give a coin cell a long service life. Bluetooth, the MCU and timekeeping still consume energy, however; actual battery life depends on capacity, refresh frequency, temperature, Bluetooth and Find activity. Image retention does not mean zero power for the complete device.

The Web console's three-page introduction includes actual interface screenshots with numbered boxes and arrows. Click a screenshot to enlarge it. You can reopen the introduction from Settings → Client settings → About.

## 1 Prepare

Use stable power and a Web Bluetooth-capable browser, or Python 3.11 or newer for the CLI. Open the Web console over HTTPS or a local origin and grant Bluetooth permission when requested.

Identify the actual MCU and board marking. A product name or HS9118 package marking does not uniquely identify the silicon; panels may also have been replaced. The client shows the MCU, while physical flashing uses a board configuration to select its backend. No firmware or first-flash package is supplied in this preview.

## 2 Connect

Select Connect and the device's Bluetooth name. Factory application names follow `IKT_PLATFORM_ID4`; Recovery adds `_OTA`. A user-facing device label is separate from the Bluetooth name.

Check Settings → Device settings → Overview, then Settings → Device settings → Display for panel size, resolution, colour and orientation. Do not guess settings when they have not been read from the device.

## 3 Send content

Process a picture or choose a Create template. Preview the direction and colours, send it and wait for final display completion. Transfer at 100% is not display completion. Supported devices can store pictures and use slideshows. Image clearing and settings reset are different operations.

## 4 Update and get help

Use Settings → Device settings → Firmware for ordinary [OTA updates](ota.md). Wired complete flashing is needed for first installation, Recovery upgrades or recovery that cannot be completed wirelessly. Settings → Client settings → About provides project and console links and can reopen the introduction. Keep credentials and private location data out of reports.
