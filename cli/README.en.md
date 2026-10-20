# InkTag command-line client

[中文](README.md) | English

## Device credential persistence

The default is unchanged: successful authentication caches a reusable control
credential, not the plaintext password. `--password-stdin` changes input only;
it does NOT disable caching. Use
`inktag --no-credential-cache --password-stdin --device IKT_TL_1234 device info`
to avoid reading/writing device credential files, including in OTA children.
Existing cache files are left untouched; credentials remain in invocation
memory only. This does not change Find/Apple persistence or promise protection
from OS swap, terminal recording or other processes.

InkTag-owned CLI source is licensed under [AGPL-3.0-only](LICENSE). Third-party
components retain their licenses. Bundled compiled resources still need review
of Corresponding Source and combined-work obligations; see [licensing scope](LICENSING.md).
This grant does not automatically apply to the separate WebUI or firmware.

Python 3.11 or newer is required. Windows: run `install.ps1`, then `inktag.ps1 --help`.
Linux and macOS: run `bash install.sh`, then `bash inktag.sh --help`.
Installation uses this directory's virtual environment and downloads Python dependencies.

Use the exact identifier returned by scanning. Connections normally synchronize time;
use `--no-time-sync` during drift observations. OTA completion includes verified App return.

After successful device-password authentication, the CLI automatically saves a
reusable control credential in `device-passwords.sqlite3` in the user's InkTag
data directory. It is not a plaintext password, but remains sensitive: do not
share it, upload it or include it in release attachments. `--password-data-dir`
selects its directory. `--password-stdin` authenticates with an entered password
but still caches the credential after success; it is not a no-save option.
See the full release's `docs/en/cli.md` or the
[online manual](https://solar2ain.github.io/ink-tag/manual/en/cli.html).
## Canvas weather stations and host jobs

In WebUI Create, edit a Weather template, configure its data source, and Export
host task. Import the editable scene with the current CLI:

```console
inktag host import weather.inktag.json --config weather.toml
inktag host render --scene weather.inktag.json --demo -o preview.png
inktag host fetch weather.toml --output forecast.json
inktag host render --scene weather.inktag.json --snapshot forecast.json -o weather.png
```

Keep the scene file. Imported jobs start disabled and unbound. Font files are
local; use `--font-file` when necessary and check the host preview before enabling.
See `docs/en/host-pages.md` in the complete release or the
[online guide](https://solar2ain.github.io/ink-tag/manual/en/host-pages.html).

Built-in CLI layouts also remain available:

```console
inktag host templates
inktag host render weather-paper --demo --size 400x300 -o preview.png
inktag host locations Hangzhou
inktag host init weather.toml --latitude 30.27 --longitude 120.15 --label Hangzhou
inktag host validate weather.toml
inktag host run weather.toml --once
```

Jobs start disabled. Edit the TOML file and inspect a preview before enabling.
Default execution only writes local PNGs; explicit `--send` is required for
identity-bound delivery, which still needs physical validation. Omit `--once`
to keep the runner active. Targets bind to host Bluetooth addresses; names are
labels only. `inktag --device IKT_TL_1234 host target` reads a binding without
time sync or image delivery. Windows/Linux use MACs; macOS uses host-local
CoreBluetooth UUIDs. Keep the host running. Open-Meteo's free API is for
non-commercial use. See `docs/en/cli.md` in the complete release for details.
WebUI edits/exports scenes; the CLI runs schedules. Neither starts delivery implicitly.
