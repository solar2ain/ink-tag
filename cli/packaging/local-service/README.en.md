# InkTag CLI and local WebUI

[中文](README.md) | English

This bundle includes the CLI, local WebUI and Find query service; no separate server download is needed. Connect over Bluetooth using the browser or CLI. Run the commands below from the extracted root containing both `cli/` and `webui/`.

## Installation, commands and Find recording

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

First setup requires internet access. macOS uses native system authentication and does not need 7-Zip or the Apple image below. Location queries on Linux are not yet qualified.

**Windows prerequisites:**

1. Download the Windows `.exe` installer for your system from the [official 7-Zip website](https://www.7-zip.org/download.html) (usually x64; use ARM64 for an ARM PC). Install the full version in the default `C:\Program Files\7-Zip` folder. The standalone `7zr.exe` is insufficient.
2. Approve the Windows administrator prompt when installing 7-Zip; use an administrator account if necessary. First-time InkTag Find setup uses `7z.exe` to extract a verified Apple authentication image of about 292 MB. It does not install iTunes. A complete cached setup does not need extraction again.
3. Run the InkTag installation command above. If it reports **Access denied** or `WinError 5`, use an administrator account with access to the reported data folder, right-click PowerShell and choose **Run as administrator**, then enter the InkTag directory and rerun the command. Check the account and path in the error; do not delete existing data. Switching accounts or choosing a new `--data-dir` uses a separate profile.

Normal InkTag use with your own writable data folder does not require administrator privileges.

To install without starting, run `powershell -NoProfile -ExecutionPolicy Bypass -File .\install_inktag.ps1 --prepare-only` on Windows or `sh install_inktag.sh --prepare-only` on macOS/Linux. After installation, choose either method below; command arguments are the same.

#### Method 1: run the client script without activation

Run from the repository or complete CLI package root. The script uses Python in `cli/.venv`; no global PATH entry is required.

**Windows PowerShell:**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\cli\inktag.ps1 device scan
```

**macOS/Linux:**

```sh
bash cli/inktag.sh device scan
```

Replace `device scan` with another command, such as `device -n B892 info`, or use `--help`. If `cli/.venv` does not exist yet, run the installer first. These client scripts do not install Python itself.

#### Method 2: activate the Python environment and use `inktag-cli`

Prepare `cli/.venv` using the unified installer above, then activate it in your current terminal:

**Windows PowerShell:**

```powershell
. .\cli\.venv\Scripts\Activate.ps1
inktag-cli device scan
```

**macOS/Linux:**

```sh
source cli/.venv/bin/activate
inktag-cli device scan
```

Activate again in each new terminal; run `deactivate` to leave. If policy blocks the activation script, use Method 1 instead of changing system execution policy.

For a manually installed CLI-only environment, first install Python3.11+ and follow the [CLI guide](docs/en/cli.md) to create a virtual environment and install `cli/`. Use the unified installer when you need Find queries so its dependencies are prepared as well. Rerun the installer after updates.

The examples below use `inktag-cli` after activation. For Method 1, replace that prefix with the client script invocation:

```console
inktag-cli device --ble-name IKT_TL_1234 info
inktag-cli display --ble-name IKT_TL_1234 calendar
inktag-cli image --ble-name IKT_TL_1234 upload photo.jpg --slot next --name photo
inktag-cli device --ble-name IKT_TL_1234 slot list
inktag-cli device --ble-name IKT_TL_1234 display slideshow start --interval 5m
inktag-cli device --ble-name IKT_TL_1234 display slideshow stop
```

Replace the name and path with your device and image. Disconnect the same device from WebUI before using the CLI. See [CLI usage](docs/en/cli.md) for commands, credential caching and troubleshooting.

### Local service: enable Find device track recording

The repository and complete CLI ZIP include local WebUI and the Find query service; no separate server package is needed. Use the unified installer initially, then start with the same client commands:

**Windows:**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\cli\inktag.ps1 webui start --repo . --enable-tls
```

**macOS/Linux:**

```sh
bash cli/inktag.sh webui start --repo . --enable-tls
```

With an activated Python environment, use:

```console
inktag-cli webui start --repo . --enable-tls
```

Open Find in the local WebUI, log in to your query account, register a tag or import its existing information, then choose “Start recording”. Keep the local service and network available so it can query periodically and save received location reports. A new location is not guaranteed for every query. See [WebUI usage](docs/en/webui.md) for installation/LAN access and [Find queries and migration](docs/en/find.md).

## Phone and LAN access

Replace `webui start --repo . --enable-tls` with `webui serve --repo . --lan` in the same client-script command. With an activated environment:

```console
inktag-cli webui serve --repo . --lan
```

The terminal shows HTTPS addresses, a public certificate and the password for this run; the username is `inktag`. Use the same LAN and trust the certificate generated by your own computer. Use Bluefy on iPhone/iPad or Chrome on Android. See `inktag-cli webui start --help` for listening, certificate and data-directory options; use the client script as the prefix if not activated.

## Data and sessions

Default data locations are `%LOCALAPPDATA%/InkTag` on Windows, `~/.inktag/` on macOS (existing installations retain their previous directory), and `$XDG_DATA_HOME/inktag` on Linux (`~/.local/share/inktag` if unset). To override, pass an absolute `--data-dir` to `webui start` and keep using that directory.

Restarting preserves tags, tracks and the same-machine encrypted session. Apple sessions can expire; log in again without registering the tags again. Use Find's tag export/import when changing computers rather than copying authentication files. Static hosting and standalone do not include the location query backend.

With an activated environment, use `inktag-cli webui status` and `inktag-cli webui stop` to check/stop the service. Otherwise use the same client-script prefix. Rerun the unified installer after updating.
