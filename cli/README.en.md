# InkTag user CLI

[简体中文](README.md)

When password status returns `PERSISTENCE_ERROR`, `device info`, `device health`
and `time show` may retain a basic read-only diagnostic link, explicitly marked
password-state unknown. They do not synchronize time, try a credential or enter
Recovery. Firmware may still refuse health reads. Keep `--no-time-sync` for
clock evidence. This is not proof of password-OFF, data repair or successful OTA;
other commands do not receive this exception.

InkTag CLI is the cross-platform Bluetooth command-line client for end users.
The initial release supports discovery, identity and telemetry, clear/calendar/
clock/device-information actions, task cancellation, screen inspection, image processing
and upload, device image slots, slideshows, bounded hardware diagnostics, and
Nordic, Goodix, OnMicro, and Dialog/Renesas OTA. Image commands execute the
same implementation as WebUI instead of maintaining an approximate Python port.

## Install

Python 3.11 or newer is required. From a source checkout:

    python -m pip install -e cli
    inktag --help

With multiple worktrees, an editable install can still point at a different
worktree. For real-device development, use the repository-root launcher below;
it puts this worktree's `cli/src` first and verifies the imported module path:

    .\tools\dev-inktag-cli.ps1 --device InkTag_GR_ device info

The same CLI orchestrates the local WebUI and Find identity library:
`inktag webui serve` uses Python to start the production frontend, SQLite API
and optional Find
service, while `inktag find identity list/import/select/update/delete` manages identities in
the running local service. Private keys travel only over loopback to the local
backend and never enter browser JavaScript, the tag or logs. Destructive
identity deletion requires `--yes`; private-key export is deliberately absent.
Node.js is not a runtime dependency. GitHub static hosting and the standalone
single-file build use browser IndexedDB/localStorage and do not expose the Find
backend.

GitHub Releases include InkTag-cli-vVERSION.zip. Verify SHA256SUMS.txt, then run:

    python -m pip install InkTag-cli-v0.2.0.zip

Ubuntu and Debian also require the system BlueZ service:

    sudo apt-get install bluez
    sudo systemctl enable --now bluetooth

WSL2 cannot directly use a Bluetooth controller that Windows owns. Real-device testing requires temporarily passing a separate USB Bluetooth adapter through `usbipd`; Windows cannot use that adapter while it is attached to WSL.

## Common commands

    inktag device scan
    inktag --device InkTag_TL_9903 device status
    inktag --device InkTag_OM_ device info
    inktag --address AA:BB:CC:DD:EE:FF device rename InkTag_Desk
    inktag --device InkTag_Desk device sync-links --sync-managed-qr
    inktag --address AA:BB:CC:DD:EE:FF device label "Living room"
    inktag --address AA:BB:CC:DD:EE:FF device qr https://solar2ain.github.io/ink-tag/
    inktag --address AA:BB:CC:DD:EE:FF led status
    inktag --address AA:BB:CC:DD:EE:FF led policy critical
    inktag --address AA:BB:CC:DD:EE:FF display calendar
    inktag --address AA:BB:CC:DD:EE:FF screen calendar --week-start 1 --countdown-name Trip --countdown-date 2026-10-01
    inktag display stop
    inktag screen show
    inktag image algorithms
    inktag image process photo.jpg --size 400x300 --color BWRY --algorithm auto
    inktag --device InkTag_GR_ image upload photo.jpg --algorithm 2 --strength 0.8
    inktag --device InkTag_GR_ image upload photo.jpg --slot next --name holiday
    inktag --device InkTag_GR_ image upload photo.jpg --slot next --store-only
    inktag --device InkTag_GR_ image upload photo.jpg --slot 8 --preview preview.png
    inktag slot list
    inktag slot download 2 --output slot-2.png
    inktag slot download 2 --format both --output slot-2
    inktag slot show 2
    inktag slideshow start --interval 5m
    inktag test diagnostic
    inktag led color blue
    inktag led effect blue fast-flash --repeat 5
    inktag led effect white sequence --repeat 3 --step-ms 240
    inktag led pattern "red:120,off:120,green:120,off:120,blue:240" --repeat 2
    inktag led off
    inktag time accuracy --duration 10m --interval 0.23
    inktag test nfc --mode status
    inktag test nfc --mode dump
    inktag test nfc --mode configure --action calendar
    inktag test nfc --mode simulate --action slot --slot 2
    inktag find status
    inktag find keyring-provision --device-name "My InkTag" --yes
    inktag find enable --allow-experimental
    inktag find disable
    inktag device verify --expect-board gr551x_zkc42vm --require-ota
    inktag device reset --yes
    inktag ota inspect firmware.zip
    inktag ota update firmware.zip

Names are unique-prefix selectors. If several devices match, the CLI refuses to
guess and asks for a more precise `--device` or `--address`. On macOS, the address
is normally a CoreBluetooth UUID rather than the device MAC. An exact-address
selector on Windows bypasses unreliable scanning and tries the cached GATT table
directly, while still validating the live service, characteristics, and protocol
version; a failed validation forces one uncached service refresh. Other platforms
continue after the first exact-address advertisement. A complete factory name
such as `InkTag_TL_9903` also continues on its exact match. Prefix and automatic
selection keep a short aggregation window after the first match so ordinary
advertising and the bounded Find multiplex window can reveal duplicate matches.
Only a completely empty lookup consumes all of `--scan-timeout`; `device scan
--all` still uses the full duration because it must inventory nearby devices.
Ordinary InkTag discovery passes the standard service UUID to the OS Bluetooth
backend to reduce unrelated scan traffic, especially on Windows. Add the global
`--verbose` option when a connection is slow; the CLI reports separate timings
for each scan pass, GATT connection, firmware identity read, V2 handshake, and
connection readiness without changing device state or protocol behavior.

`device status` reads advertising only and does not open a GATT connection. An
ordinary application name also matches its Recovery name with the appended
`_OTA`; the command combines that signal with any advertised OTA/DFU service
UUID and reports `application` or `recovery`.

`device rename` normally synchronizes the NFC NDEF link when supported, but it
does not replace the device-information QR: the CLI cannot reliably determine
whether a stored QR matrix is user-owned. Use `--sync-managed-qr` only when the
QR is managed by InkTag. The name remains active if synchronization fails; use
`device sync-links` to retry NFC, or `device sync-links --sync-managed-qr` to
retry the official QR as well. `--no-link-sync` disables the default NFC sync.

Do not let multiple CLI processes, a browser Web Bluetooth connection, or
another BLE debugger use the same host adapter concurrently. A device connected
to the WebUI normally stops advertising and therefore cannot be discovered by a
second process; competing scanners can also cause dropped reports in Windows or
BlueZ. Hardware automation must serialize adapter ownership and record scan,
connect, GATT discovery, and V2 initialization timings separately.

The Bluetooth broadcast name and persistent device label are separate fields.
Broadcast names must start with `InkTag_` and contain only ASCII letters,
digits, underscores, or hyphens. Labels accept UTF-8; pass a blank string to
clear one. The CLI enforces the byte limits reported by the firmware.

## Image processing and upload

Names and numbers from `image algorithms` are the stable WebUI algorithm list.
`image process` creates an offline device-palette PNG. `image upload` reads the
device's visible dimensions, colors, and transform, then performs crop/stretch,
automatic portrait rotation, zoom, pan, processing preset, contrast, heavy-ink,
and dithering before packing the device transfer format. Controls include
`--fit crop|stretch`, `--rotate auto|0|90|180|270`, `--scale`, `--pan-x`,
`--pan-y`, `--preset`, `--contrast`, `--strength`, and `--heavy-ink`.

Without `--slot`, the image is displayed only. `--slot next` saves the next
available record. Add `--store-only` to commit a slot without refreshing the
current e-paper image; it is valid only together with `--slot`.
A full store is rejected unless `--force` explicitly permits
replacement of the oldest compatible ordinary image. `--slot ID` replaces that
record without requiring another force flag. The CLI uses only the 0.2.0 V2
transaction protocol: it waits for old-work cancellation, every chunk, slot
commit, and the final completion event carrying the same transaction ID. A
failure sends `TRANSFER_ABORT`, so transfer completion is never reported as
physical refresh completion. Firmware whose version characteristic is not
`0x20` is rejected before the first command write; the CLI never falls back to
V1.

`slot download ID` uses `READ_RESOURCE` and strictly checks the slot ID,
offset, and total length of every chunk. By default it applies the current
compatible screen profile's visible crop, transform, and controller padding
and exports PNG; `--format raw` saves the device-ready color planes and
`--format both` saves both. The command reports CRC-32 and SHA-256 digests of
the downloaded bytes. The frozen protocol does not expose the record header's
expected CRC, so this CRC-32 is a download digest rather than a second
comparison with the device-private record header.

## Debug-command migration

Use this CLI for future BLE debugging. The old
`tools/hardware/smoke-test-inktag-ble.py` remains only as a historical
regression reference and receives no new product features. `device verify`
covers board/profile/OTA-service assertions; `device info`, `device watch`,
and `test diagnostic` cover state and raw notifications; `test nfc` covers
NFC status, user data, behavior writes, and complete simulation. Display,
image, slot, slideshow, and OTA work use their corresponding maintained
subcommands. Legacy `model-id`, `write-mode`, `skip-query`, and arbitrary
GATT writes are retired protocol/transport internals, not user APIs.

`test nfc --mode simulate` waits for the tap and final page/slot/stop
result instead of returning at queue acknowledgement; `test button` likewise
waits for the configured action's final display/slideshow result. Reset
immediately drops BLE, so `device reset` requires confirmation and automation
must pass `--yes`.

`find status` reads multiplex windows and persistent configuration.
Current devices support Find v2: they hold master public point P and rolling
key K, never the owner's private scalar or derived private keys. First
`find enable` creates and protects owner material in the canonical service
and verifies the device commit proof before enabling broadcasts.
`find identity import-local-v2` recovers a matching protected local v2 owner
using the exact fingerprint, device ID and BLE name, without replacing the
owner, selecting it or enabling tracking. `list/select/update/delete` manage
the same database, deletion requires `--yes`, and private-key export is not exposed. Boards that
advertise only `EXPERIMENTAL_FIND` require an explicit
`--allow-experimental` acknowledgement when enabling it; `find clear --yes`
clears device Find identity/configuration subject to its capabilities and authentication.

`time accuracy` observes device second transitions over one BLE connection
without synchronizing the clock, then fits drift against the host monotonic
clock. Sub-second reads narrow the integer-second edge bracket; they do not
treat BLE or HFCLK as the device time source. The report includes a 95%
regression range, while release qualification still requires a long no-probe
temperature-aware run.
`--interval` is bounded to 0.1–0.5 seconds (default 0.23); sparse intervals such
as five seconds cannot bracket second edges and are rejected before connecting.
Slow BLE reads can also leave insufficient resolution, which is not evidence
of a device clock fault. Discrete `time read` snapshots are not a ppm measurement.

`device health --duration 30 --interval 5` samples health over one BLE
connection, immediately and again at the end. Duration is in seconds, bounded
to one hour; the interval is at least one second. Without `--duration`, the
existing single-snapshot output is unchanged. To observe the existing clock
and sleep state without a connection-time clock write:

    inktag --device IKT_TL_ACEE --no-time-sync --json device health --duration 30 --interval 5

Each sample retains raw counters, UTC capture time, elapsed time and request
latency. A counter decrease may indicate reset or wrap, not a valid positive
delta. Firmware without `sleepEntries` still returns its other health fields.
Counters do not replace current measurements, and BLE polling itself wakes
the device. A failed read exits and disconnects instead of reconnecting and
claiming a successful single-connection test.

Place global options before the subcommand, for example
`inktag --json device info`. Slot deletion and OTA require `--yes` in
non-interactive use. OTA validates the container, layout, size, and SHA-256
before transfer, then attests the returned application version, byte count,
checksum, confirmed Swap state, and generation. If a hardware batch has no
safe Flash region for image records, `slot` and `slideshow` fail explicitly
instead of writing into the application or OTA regions.

Normal `ota update` explicitly synchronizes the clock once after application
attestation. If installation and attestation succeed but the subsequent connection
or clock sync fails, the command reports partial success and exits with code 1.
JSON returns `ok=false`, `status="partial-success"`, `applicationVerified=true`,
and `driverExitCode=0`, retaining `clockSyncError` and driver output. The image is
already installed: do not automatically repeat OTA. Resolve connection/authentication
and retry `time sync` separately. An explicit `--no-time-sync` drift experiment does
not require synchronization; `--skip-verify` does not attest the application.
Opportunistic synchronization on ordinary connections still does not block read-only work.

## Initial boundaries

For a reviewed fixed TLSR Recovery v3 data-phase `QUEUE_FULL`, the CLI backs
off at most once: disconnect, wait one second, reconnect the same device and
restart the same validated image at block zero with a single-packet window.
Later transport reconnects retain that window. A second overflow fails, and
the existing total limit of eight transfer attempts is unchanged. Unknown
Recovery versions, low voltage, signature/layout failures and other firmware
rejections are not retried by this policy. App/image attestation and the
default single clock synchronization still follow transfer acceptance.

- First flashing, factory backup, and restore remain in the guarded port tools.
- There is no arbitrary GATT-write or screen-GPIO command.
- Activation is enabled only when the exact target advertises the `ACTIVATION`
  capability; other targets fail explicitly without a compatibility write.
