# InkTag command-line client

Releases that include the CLI provide InkTag-cli-vVERSION.zip. It requires
Python 3.11 or newer and supports Windows, Linux, and macOS.

Download the CLI archive and SHA256SUMS.txt from the same release, verify it,
then install:

```text
python -m pip install ./InkTag-cli-vVERSION.zip
inktag --help
```

Common entry points:

```text
inktag device scan
inktag --device InkTag_OM_ device info
inktag display calendar
inktag display stop
inktag slot list
inktag slideshow start --interval 5m
inktag ota inspect InkTag-board-vVERSION-ota.zip
inktag ota update InkTag-board-vVERSION-ota.zip
```

Use a more precise --device or --address when several devices match. On macOS,
the address is normally a CoreBluetooth UUID. Read commands support --json;
scripts must pass --yes for slot deletion and OTA.

The CLI validates the package before selecting Nordic Secure DFU, Goodix OTAS
v2, OnMicro DFU v3, or Dialog SUOTA. It does not replace first flashing or the
required factory backup.
