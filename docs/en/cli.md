# Command-line client

The same Python client provides Windows, Linux and macOS launchers. Python 3.11 or newer is required. On macOS, Bluetooth identifiers may be UUIDs rather than MAC addresses.

## Install and launch

Extract the ZIP and enter `inktag-cli`; a full release checkout uses `cli/` instead. Windows:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\inktag.ps1 --help
```

Linux/macOS:

```sh
bash install.sh
bash inktag.sh --help
```

Launchers create a package-local virtual environment, not a firmware build environment. Linux needs a system Bluetooth service/adapter and Python venv support. Allow terminal/Python Bluetooth access on macOS. To use `inktag` directly, install with `python -m pip install .` in your own activated virtual environment; otherwise replace `inktag` below with the package launcher.

From the release repository, run `inktag webui start --repo .` to serve the bundled `webui/` directory. The CLI ZIP alone does not contain WebUI pages. Use the separate local-server package for Find; other WebUI features remain available without Find installed. Use `inktag webui serve --repo . --lan` for phone access; see the [WebUI guide](webui.md).

## Select a device

```text
inktag device scan
inktag --address ADDRESS device info
inktag --address ADDRESS --no-time-sync device health
inktag ota inspect firmware-ota.zip
inktag --address ADDRESS ota update firmware-ota.zip --yes
inktag --address ADDRESS recovery enter
inktag --address ADDRESS recovery resume-app
```

Use the address or identifier returned by scanning. Command-specific `--help` lists supported options. Ordinary connections and OTA normally synchronize time once; drift observations use `--no-time-sync`.

Alternatively use `--device IKT_NRF_1234`, replacing the example with the exact scanned name. A unique prefix works, but prefer a full name with multiple nearby devices. Disconnect WebUI from the same device first.

## Panel, time and pages

```console
inktag screen profiles
inktag --device IKT_NRF_1234 screen show
inktag --device IKT_NRF_1234 screen set SSD1619_128x250_BWR_HEMA213
inktag --device IKT_NRF_1234 time sync
inktag --device IKT_NRF_1234 display calendar
inktag --device IKT_NRF_1234 display clock
inktag --device IKT_NRF_1234 display device-info
inktag --device IKT_NRF_1234 display clear
inktag --device IKT_NRF_1234 device label "Living room"
```

The profile is only for the matching Hema panel; use `screen profiles` and `screen set --help` for other screens and advanced geometry. Labels allow 24 printable ASCII bytes and do not rename Bluetooth. Display clear preserves slots. Synchronize after battery replacement or a power interruption.

`screen calendar --week-start 1` sets Monday; 0 means Sunday. Use `--countdown-name Trip --countdown-date 2027-01-01` for a target date and `--clear-countdown` to remove it. Countdown labels allow four characters.

## Pictures, slots and slideshow

```console
inktag --device IKT_NRF_1234 image upload photo.jpg --preview preview.png
inktag --device IKT_NRF_1234 image upload photo.jpg --slot next --name landscape --wire-encoding auto
inktag --device IKT_NRF_1234 slot list
inktag --device IKT_NRF_1234 slot show landscape
inktag --device IKT_NRF_1234 slot download 0 --output photo-0.png
inktag --device IKT_NRF_1234 slideshow start --interval 5m
inktag --device IKT_NRF_1234 slideshow stop
```

Without `--slot`, upload displays directly. `next` selects free storage; a specific ID replaces that slot. Add `--store-only` to save without refreshing. IDs may have gaps. Names and content types are set during upload/replacement, not independently edited; names allow 35 UTF-8 bytes. RLE saves transfer bytes, not storage. Interrupted replacement may lose the old target. `--wire-encoding none` sends raw data; the obsolete `--compression` option is not accepted by this client.

`slot delete 0` deletes one image; `slot clear` deletes all stored images, with confirmations. Download important images before changing screen geometry.

## Diagnostics and maintenance

`diagnostic runtime`, `diagnostic health` and `diagnostic clock` retain normal read-only device diagnostics without synchronizing time. Unavailable values are not zero. Use `led locate` to locate a device, `display ghost-clean` for ghost cleaning, `nfc status` for NFC status and `nfc action --help` for supported page-switching settings. Device capabilities remain authoritative. Development waveform experiments, input simulation and OTA fault injection are not included in the user distribution.

`device reset-settings` preserves images; `device clear-user-data` deletes user images and preserves settings. Neither is factory reset. Read confirmations and respect device authentication/fresh old-password checks.

## Updates

Use the actual compatible release file: Nordic, GR, OM and TLSR use ZIP; DA uses `.img`. Never substitute complete flashing. Recovery adds `_OTA` to the factory name; use the exact detected Recovery identity for `resume-app`. Old Recovery may only support updates. See [OTA](ota.md).

CLI and WebUI share protocol, identity, authentication, signature and voltage checks. OTA is not first flashing. Safe Recovery exit requires a supporting installed Recovery. Never blindly repeat a command with an unknown outcome.

## Host-rendered pages and weather

The host fetches data, renders images and schedules delivery. Keep it running and
keep targets in Bluetooth range. In WebUI Create, edit a Weather template and its
source, then Export host task. The CLI imports the editable scene and reuses its
geometry/weather plan. See the [host-page guide](host-pages.md) for the full
workflow and OS scheduling.

```console
inktag host import weather.inktag.json --config weather.toml
inktag host render --scene weather.inktag.json --demo -o preview.png
inktag host fetch weather.toml --output forecast.json
inktag host render --scene weather.inktag.json --snapshot forecast.json -o weather.png
```

Keep the referenced scene file. Browser/host fonts may differ; select a local
`--font-file` or task `font_file` and check the CLI PNG. Imported jobs are disabled
and unbound. The following built-in layouts also remain available:

```console
inktag host templates
inktag host locations Hangzhou
inktag host init weather.toml --latitude 30.27 --longitude 120.15 --label Hangzhou
inktag host render weather-paper --demo --size 400x300 -o preview.png
inktag host fetch weather.toml --output forecast.json
inktag host render weather-paper --snapshot forecast.json -o weather.png
inktag host validate weather.toml
inktag host jobs weather.toml
inktag host run weather.toml --once
inktag host status weather.toml
```

[Open-Meteo](https://open-meteo.com/) provides weather; its location search uses
[GeoNames data](https://open-meteo.com/en/docs/geocoding-api). The free
non-commercial API needs no key; commercial use follows the provider's terms.
Requests include configured location/timezone and weather variables, not device,
password, account or Find data. Fonts are local (`--font-file` if needed), not
redistributed. Synthetic previews are labelled DEMO.

Each TOML `[[jobs]]` references a `scene` file or built-in `template`, and a named `[sources.*]` source, plus
its canvas, palette, target and `interval_minutes`. Jobs can reuse templates and
cached data across screens. They start disabled; inspect the preview before
setting `enabled = true`. A 60-minute interval is recommended.

`run --once` evaluates due jobs once for an OS task scheduler; omit `--once` for
the foreground loop. Default operation writes PNGs without BLE. Use
`--fixture forecast.json --data-dir ./weather-test` for offline validation.
Only explicit `--send` permits device delivery, requiring a host-reported
`address` plus its real 16-hex `expected_device_id` (not its Bluetooth address).
`device` is only a display name. Jobs never reselect by name or fall back to a
same-name device. Windows/Linux use a MAC; macOS uses a host-local CoreBluetooth
UUID, not a MAC or a portable identity. Do not use a browser's `BluetoothDevice.id`.
Renaming preserves the binding; address changes require explicit rebinding.
Use `inktag --device IKT_TL_1234 host target` to read the binding, then copy its
`target` fields into the job. Select duplicate names with `--address` instead.
This command does not synchronize time, enable a job or push a page.
Delivery reuses normal authentication/image transfer and checks the Pro policy
for custom dynamic templates. Missing saved credentials fail without a background
password prompt. Physical delivery is still unverified in this increment.

Unchanged pixels are skipped and missed runs coalesced. Failures keep successful
state and back off without clearing the panel. Preview and delivery states are
separate. Disable a job in TOML or stop the runner with Ctrl+C; a device STOP does
not stop its host's job.

Commands do not add missing hardware/capabilities. This CLI package does not contain WebUI pages; download the [local server](webui.md) separately.
