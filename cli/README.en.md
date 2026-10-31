# InkTag command-line client

[中文](README.md) | English

InkTag CLI connects to devices, sends images, manages displays and performs OTA updates. It also runs WebUI, Find queries and host weather jobs. Use `inktag-cli` or the equivalent `inktag` executable.

## Quick start

Install Python 3.11 or newer, download/clone this repository or extract the complete CLI ZIP, and enter the root containing both `cli/` and `webui/`. Use the unified installer:

**Windows:**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\install_inktag.ps1
```

**macOS / Linux:**

```sh
sh install_inktag.sh
```

This installs the CLI into `cli/.venv` and automatically installs the **local Find query service dependencies** in a separate `find/runtime/.venv` under the per-user data directory. It then starts local WebUI and Find in the background (`webui start`); closing the terminal does not stop them. Setup prints copyable start, stop, status and log commands. New macOS installations store data and logs in `~/.inktag/`; an existing legacy directory is retained to preserve sessions and tags. System Python is unchanged; installation does not log into Apple, connect devices or clear existing data.

First setup requires internet access. Windows also requires full 7-Zip and downloads an Apple authentication image of about 292 MB on first setup. macOS uses native system authentication without that image. Location queries on Linux are not yet qualified.

To install without starting, run `powershell -NoProfile -ExecutionPolicy Bypass -File .\install_inktag.ps1 --prepare-only` on Windows or `sh install_inktag.sh --prepare-only` on macOS/Linux. Then choose either method below; command arguments are identical.

### Method 1: run the client script without activation

Run from the repository or complete package root; the scripts use the installed `cli/.venv`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\cli\inktag.ps1 device scan
```

```sh
bash cli/inktag.sh device scan
```

Rerun the unified installer after an update. For CLI-only use without Find queries, enter `cli/` and use the existing `install.ps1` or `bash install.sh` instead.

The unified installers are provided in this repository and the complete **CLI ZIP**. For a wheel/sdist only, run `python -m venv .venv`, activate that environment with `. .\.venv\Scripts\Activate.ps1` on Windows or `source .venv/bin/activate` on macOS/Linux, then run `python -m pip install "path to the downloaded .whl or .tar.gz"`. In an extracted directory containing `pyproject.toml`, `python -m pip install .` also works. These distributions do not include the complete-package root installers or the Find query service.

### Manually create a CLI environment instead of the installer

Install Python3.11+, then run from the repository or complete package root. Windows:

```powershell
python -m venv cli\.venv
.\cli\.venv\Scripts\python.exe -m pip install .\cli
```

macOS/Linux:

```sh
python3 -m venv cli/.venv
cli/.venv/bin/python -m pip install ./cli
```

These commands install only the CLI. Prefer the unified installer when Find queries are needed, so their dependencies are prepared too. Do not recreate a working existing environment.

### Method 2: activate the environment and use `inktag-cli`

In the current Windows PowerShell session:

```powershell
. .\cli\.venv\Scripts\Activate.ps1
```

In the current macOS / Linux terminal:

```sh
source cli/.venv/bin/activate
```

After activation, use `inktag-cli` (or `inktag`) directly, or `python -m inktag_cli`. Each new terminal needs activation again. If you prefer not to activate, or policy blocks the activation script, keep using the wrappers; no system execution-policy change is needed. Run `deactivate` to leave the environment.

```sh
inktag-cli -h
inktag-cli device scan
inktag-cli device -n B892 info
inktag-cli display -n B892 calendar
inktag-cli image -n B892 upload photo.png --slot next --name "Holiday"
```

### Start the local WebUI from the CLI

Return to the extracted `InkTag-client` root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\cli\inktag.ps1 webui start --repo . --enable-tls
```

```sh
bash cli/inktag.sh webui start --repo . --enable-tls
```

For mainly WebUI/Find use, run `powershell -NoProfile -ExecutionPolicy Bypass -File .\install_inktag.ps1` (Windows) or `sh install_inktag.sh` (macOS/Linux) from the extracted package root instead. These prepare the local WebUI/Find environment and start the service; they do not register CLI commands globally. The Find environment is in `find/runtime/.venv` under the per-user data directory, separate from `cli/.venv`. See [WebUI usage](../docs/en/webui.md) for default data locations.

Every command and subcommand supports `-h` and `--help`; consult current help for complete parameters. The examples below use `inktag-cli` in an activated environment. Otherwise replace that prefix with the wrapper invocation.

Scanning, connecting, rendering and querying show a spinner with live `elapsed` time in a terminal. Image uploads also show progress. The indicator clears on completion or cancellation and pauses for password and confirmation prompts. JSON output, redirected output and foreground service logs remain animation-free.

`device scan` and initial discovery for other device commands default to at most 30 seconds. Connection commands continue early once a target is selected. Device management, image, display, OTA and host jobs all use this default. Set `--scan-timeout SECONDS` after `device`, for example `device -n B892 --scan-timeout 45 info`; this does not change the post-switch OTA or Recovery deadlines. Scanning shows the discovered device count live and comma-separated BLE names on the next line. It clears the live status before the result table. Ctrl+C also lists devices already found and exits with code 130. Other commands clear their waiting status when they finish.

Successful connections display the BLE name and address without `--verbose`. Native OTA connections first report that the BLE link is connected; this does not mean the update has completed. With `--json`, connection notices are separate JSON lines on stderr; stdout contains only the command result.

## Select one or more devices

Connection options follow `device`, `image` or `display`, before the subcommand:

```sh
inktag-cli device -n B892 info
inktag-cli device -a AA:BB:CC:DD:EE:FF info
inktag-cli device -n B892,6775 info
inktag-cli device -a AA:BB:CC:DD:EE:01,AA:BB:CC:DD:EE:02 health
inktag-cli --json device -n B892,6775 health
```

`-n` / `--ble-name` accepts a case-insensitive unique substring of the BLE name. Exact names take priority. An ambiguous substring lists candidates instead of operating on them. Chip UIDs are not selectors. `-a` / `--ble-address` selects an exact address; macOS uses the CoreBluetooth identifier shown by scanning.

Separate multiple targets with commas. If both names and addresses are supplied, the lists must have equal lengths: each name/address pair refers to the same device. Duplicate targets are removed. Batch operations run sequentially and report each result; failed writes are never automatically retried. Ctrl+C stops remaining operations. Separate terminals can operate on different devices concurrently.

Connection options include `--scan-timeout`, `--connect-timeout`, `--no-time-sync` and device authentication. Ordinary connections synchronize time by default; use `--no-time-sync` to preserve it. Read-only diagnostic commands follow their documented behavior.

## Command overview

| Command | Purpose |
| --- | --- |
| `device scan / info / status / health / watch / verify / restart` | Discover, inspect, observe, verify and restart devices |
| `device display`, or `display` | Calendar, clock, device page, clear, stop and slideshow |
| `device image`, or `image` | Convert and upload images; list algorithms |
| `device slot` | List, show, download and delete stored images |
| `device setting` | Names, QR, screen, pins, buttons, fonts and password |
| `device led / time` | Indicator policy, lighting, device time and synchronization |
| `device ota / recovery` | Inspect/install OTA packages; enter Recovery or return to App |
| `device activation` | Authorization status, activation requests and activation codes |
| `device diagnostic` | Health, runtime and clock diagnostics where supported |
| `device findmy` | Enable/disable and inspect device-side Find |
| `findmy` | Local account, identities, queries and tracking; `findmy device` also manages the device |
| `host` | Host-rendered pages, weather, templates and scheduled jobs |
| `webui` | Foreground/background WebUI, status, stop, restart and logs |

## Device settings

```sh
inktag-cli device -n B892 setting show
inktag-cli device -n B892 setting ble-name show
inktag-cli device -n B892 setting ble-name set IKT_OM_B892
inktag-cli device -n B892 setting device-name set "My desk"
inktag-cli device -n B892 setting device-url set "https://example.com/"
inktag-cli device -n B892 setting screen show
inktag-cli device -n B892 setting screen profiles
inktag-cli device -n B892 setting pins show
inktag-cli device -n B892 setting pins set --config pins.json --yes
```

`ble-name` changes the broadcast name used for discovery. `device-name` is printed on the device information page and currently accepts up to 24 printable ASCII bytes. `device-url` sets its QR URL/text; `device-url sync` updates managed links from the current BLE name. QR content cannot currently be read back from the device.

`setting pins` combines the JSON sections `screen`, `led`, `buttons` and `buzzer`. Omitted sections retain their current values. This example changes only the two buttons:

```json
{
  "buttons": [
    {"gpio": 26, "activeLow": true, "pull": "up"},
    {"gpio": 255, "activeLow": false, "pull": "none"}
  ]
}
```

GPIO 255 means disconnected. Use the `configuration` returned by `pins show` as the screen/LED format. BUSY/RESET behavior follows the screen driver; board EN and LED polarity remain configurable. TXD/RXD are not exposed. There are at most two user buttons, normally active-low; buzzer mode/pins replace a separate enable switch. Only device-reported editable settings can be changed.

Screen/LED and button/buzzer writes are separate. A failure reports confirmed sections; read current configuration before deciding what to do next. `pins restore-screen-led-defaults` restores only screen/LED routing.

```sh
inktag-cli device -n B892 setting button show
inktag-cli device -n B892 setting button set single next
inktag-cli device -n B892 nfc usage
inktag-cli device nfc commands calendar
```

Both buttons share single/double/long-press actions. NFC has no mode selector or off switch. `device nfc usage` reads device instructions and advertised phone commands; `device nfc commands calendar` generates command text locally. On the phone, use NFC Tools: Write → Add a record → Text. Phone commands require Pro and supporting firmware; reading the device link does not execute a device action.

Use `setting font info / build / install` for fonts, `setting screen mark / calendar` for the InkTag mark and calendar preferences, and `setting buzzer test` for a short output test.

## Passwords and clearing data

Enter passwords through hidden prompts or `--password-stdin`, never command arguments. Successful authentication normally caches a reusable control credential. `--password-stdin` alone does not disable caching; `--no-credential-cache` prevents reading/writing reusable credentials on disk. Device authentication and throttling remain in effect.

Use `setting password status / set / disable`. `setting reset-settings --yes` clears ordinary settings/password while preserving images, Pro, Find and factory identity; an enabled password still requires the original password. `setting clear-user-data --yes` clears user images/playback references while preserving settings, Pro, Find and factory identity. `device restart` only reboots.

Clearing can use `--expected-device-id` to verify the intended device before acting. This verifies identity; it is not a selector. If a result is unconfirmed, reconnect and inspect state rather than repeating the clearing operation.

## Images, displays and OTA

`image process` converts images offline and `image algorithms` lists algorithms. Uploads support `--slot next` or a specific slot, `--name`, and `--store-only`; an occupied slot can be explicitly replaced. Batch uploads process images for each target's actual screen. Static displays and device-executed dynamic layouts are different capabilities.

```sh
inktag-cli device -n B892 display slideshow start --interval 30m
inktag-cli device -n B892 slot list
inktag-cli device ota inspect app.zip
inktag-cli device -n B892 ota update app.zip
inktag-cli device -n B892 recovery enter
inktag-cli device -n B892_OTA recovery exit
```

OTA requires a matching signed package and verifies package integrity, publisher signature and the returned device identity/state. Recovery entry is separate from choosing an OTA package. Use `recovery exit` to return to App; it is the same as `recovery resume-app`. Complete flashing uses separate MCU programmer tools.

A short BLE name also works, for example `device -n 43D2 recovery exit`. Select a full name or address if several devices match. Platforms share connection, return-request and application-ready stages; use `--verbose` for connection diagnostics. `--json` retains native result fields and writes progress events to stderr.

## WebUI, local logs and Find

```sh
inktag-cli webui serve
inktag-cli webui start --enable-tls
inktag-cli webui status
inktag-cli webui log -f
inktag-cli webui log --service find -f
inktag-cli webui restart --enable-tls
inktag-cli webui stop
inktag-cli findmy identity list
```

`serve` runs in the foreground with terminal access logs. `start` runs in the background and writes logs under the user data directory. Startup opens a browser unless `--no-open` is supplied. HTTP is the default; `--enable-tls` or explicit certificate options select HTTPS. Restart does not silently inherit a previous TLS mode. A running instance with different HTTP/HTTPS settings requires an explicit restart.

Run one service per data directory. `serve` and `start` share its registration; use `restart` to switch HTTP/HTTPS without replacing certificates. If `status` finds a reachable but unregistered service, close the original `serve` window before starting again. Read-only status checks do not take control of unknown processes.

For phone access use `webui serve --lan`, which prepares LAN HTTPS and an access password. Logs include method, path, status and duration, without request bodies/query parameters. Navigation through `#` routes does not necessarily make an HTTP request or produce an access log.

Find is experimental. `findmy` detects the actual local service URL and HTTPS certificate. Select another local instance using `findmy --server URL --ca-cert CA.pem`. Certificate verification is not disabled and HTTPS is not silently downgraded. Account login and verification live under `findmy account`, with hidden password/code prompts. `findmy tracking` manages query schedules and saved history. CLI and WebUI share local sessions and identities.

## Host dynamic pages

Templates live under `host template`; there is no top-level `template` command. Create jobs with `host init / import`; use `host fetch / render / validate` for data, rendering and validation. `host run --once` can be called by an OS scheduler. Preview is the default; only explicit `--send` permits delivery. Automatic updates require an online host within BLE range. Jobs bind the actual BLE address and expected device identity rather than relying only on names.

See the [InkTag user manual](https://solar2ain.github.io/ink-tag-manual/) for further guidance. Applicable licensing and third-party notices are provided in the project license files.
