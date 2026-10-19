# Host-rendered pages: weather and date placards

[中文](../zh-CN/host-pages.md) | English

The computer fetches weather, renders an image and delivers it over Bluetooth.
No executable page or script is installed on the device. Keep the computer awake
and targets in Bluetooth range; the browser need not remain open.

## Design and export

In WebUI Create, choose a Weather template or a host-dynamic blank page. Weather
fields, current conditions, original code-drawn icons, hourly temperature bars
with a humidity curve (black temperature bars and a red humidity line by default),
optional rain charts, forecasts, Gregorian dates, weekdays
and lunar dates are editable controls. Default templates show the weather
snapshot's local calendar date rather than the current time. Update time remains
an optional control. Ordinary
text, tables, lists, pictures, QR codes and strokes can be added around them.
The square Weather button appears last in the control library, only on host-dynamic
pages. Its dropdown adds a weather group or individual fields. Dates use an accent
color; temperatures stay black. Gray e-paper backgrounds are preview-only: exported
images keep white pixels.
QR controls resize in pixel steps, not just integer module multiples, retaining
four quiet modules and hard black/white edges. Portrait timetable presets are
removed; landscape/square timetables and saved user pages remain available.
Date numerals use red while calendar units remain black. Text controls default
to transparent backgrounds, with transparent or palette-colored fills in the
inspector. Photo presets below 400×300 (or the corresponding portrait size) are
omitted; picture frames remain available on blank canvases.
Picture controls retain their algorithm, contrast, dithering strength and heavy-ink
settings in the scene, reused by previews and exports.

Configure the label, coordinates and timezone in Weather source & preview, then
apply them. Explicitly fetch a preview or import a snapshot from `host fetch`.
The initial preview uses synthetic data, identified in the editor sidebar rather
than on the screen. Fetch real weather before enabling a job. Default templates
and CLI exports do not overlay demo text or a provider label; source credit stays
in the data-source panel and snapshot metadata. Snapshots without hourly humidity
show temperature bars only, never a curve inferred from current humidity.
Small screens prioritize large current readings; larger screens add forecasts
beside the current reading and a larger chart below. Square and narrow portrait
displays use their own compositions. Export host
task downloads the editable `*.inktag.json`, without automatically adding device
addresses, credentials or accounts. Native device widgets/fonts/variables and
slot carousels are not host-weather features and are explicitly refused on export.

## CLI workflow

```console
inktag host import weather.inktag.json --config weather.toml
inktag host validate weather.toml
inktag host render --scene weather.inktag.json --demo -o preview.png
inktag host fetch weather.toml --output forecast.json
inktag host render --scene weather.inktag.json --snapshot forecast.json -o weather.png
inktag --device IKT_TL_1234 host target
```

Replace filenames/names with actual values. Import creates a disabled TOML job;
existing files are never overwritten. Keep the referenced scene file, including
when moving computers. Its canvas and palette are authoritative; do not override
them with `render --size` or `--palette`. Edit the scene and check the job instead.

Fonts are local host files, not redistributed with scenes. Chinese scenes use a
local CJK font by default; use `--font-file /path/to/font.ttf` for a particular
font, or set `font_file` in the job. Browser/host font metrics can differ slightly.
Check the CLI PNG: rich text retains size, color, alignment, gaps and baseline
offsets, and content is clipped at its frame rather than silently shrunk.

Copy the real `address` and `expected_device_id` from `host target` into the job,
then explicitly enable it. Names are labels only. Windows/Linux use MACs; macOS
uses a host-local CoreBluetooth UUID, not a portable MAC. Browser Bluetooth IDs
are not host addresses. Rebind after an address change; never fall back to a
same-name device. Configuration contains local device identities: do not publish it.

```toml
[[jobs]]
id = "desk"
scene = "weather.inktag.json"
source = "home"
width = 400
height = 300
locale = "zh-CN"
interval_minutes = 60
enabled = true
address = "AA:BB:CC:DD:12:34" # replace with the actual host address
expected_device_id = "0123456789ABCDEF" # replace with the actual device identity
```

```console
inktag host run weather.toml --once
inktag host run weather.toml --once --send
inktag host run weather.toml --send
inktag host status weather.toml
```

Without `--send`, only local PNGs are written. `--once` evaluates due jobs once;
otherwise the runner stays in the foreground, stopped with Ctrl+C. Delivery uses
the normal shared authentication/image transport and the Pro policy. Missing
saved credentials fail rather than prompting in the background. Identity, canvas
or palette mismatches refuse delivery without modifying device settings/slots.
Physical delivery still requires qualification on the actual target.

## Scheduling and data

A 60-minute interval is recommended. Windows Task Scheduler can run the installed
`inktag.exe` every five minutes with arguments
`host run "D:/InkTag/weather.toml" --once --send`. Use absolute paths and the normal
user with Bluetooth/saved-credential access. Linux/macOS user tasks invoke the
same command with absolute paths; macOS permission must be verified in the actual
launch environment. Do not run independent state databases for the same job.

State defaults to `host-pages/` below the user's InkTag data directory. Override
with `--data-dir /absolute/path/InkTag` when needed. Preview/delivery states are
separate. Unchanged pixels are skipped, missed intervals coalesced, and failures
preserve successful state with backoff, never clearing the panel. Scene edits
trigger a new render; keep TOML dimensions in sync after resizing. Scheduled
weather uses the named TOML source, so update its coordinates/timezone when moving.
Disable the OS task or set `enabled = false` to pause. Device STOP does not stop it.

Weather uses [Open-Meteo](https://open-meteo.com/en/docs). Only configured
coordinates/timezone/weather variables are sent, not device/account/Find data.
The free API is for non-commercial use; commercial use follows provider terms.
Source credit stays in snapshot metadata. `--fixture forecast.json` permits offline
preview checks with an isolated `--data-dir`, but cannot be combined with `--send`.
Synthetic snapshots are identified in metadata and must not be used for scheduled delivery.

## Date placard

Choose Create → Date placard and select the display size. A large
accent-colored day number, weekday, lunar date and weather are editable controls.
Landscape layouts use columns; portrait layouts stack them. Small screens
prioritize the date and current weather. Temperatures remain black.
The large day reuses the device clock's drawn digits rather than an extra font
asset, with editable size and color.

The date follows the weather snapshot's local date, not a ticking clock. Weather
source, target binding and delivery use the same workflow as above. For daily
updates, choose a 1440-minute interval when importing.

```console
inktag host import date-card.inktag.json --config date-card.toml --interval-minutes 1440
inktag host render --scene date-card.inktag.json --demo -o date-card.png
inktag host validate date-card.toml
inktag host run date-card.toml --once
```

Import creates a disabled job. Use `--demo` for synthetic previews, or fetch real
weather with `host fetch` and render with `--snapshot`. Explicit enablement and
`--send` follow the workflow above. Local previews never connect to devices.
