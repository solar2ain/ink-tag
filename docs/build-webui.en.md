# Build InkTag WebUI

[简体中文](build-webui.md)

This guide covers local WebUI development and distributable web builds. Use the [hosted WebUI](https://solar2ain.github.io/ink-tag/) or open the supplied [standalone HTML](../webui-standalone/InkTag-WebUI.html) to use the interface without building it.

## Start a development preview

Install Git, Node.js 24, and npm. From the repository root:

```sh
cd webui
npm ci
npm run dev -- --host 127.0.0.1
```

Open the URL printed in the terminal in Chrome or Edge on the same computer. The default is `http://127.0.0.1:5173/`. Press Ctrl+C to stop. Use this setup to edit the interface, preview work, and manage tags over Bluetooth. Find queries use the [local CLI service](en/webui.md#install-and-open).

## Build static and standalone pages

After running `npm ci`, run this command from `webui/`:

```sh
npm run build
```

The build writes `webui/dist/static/` for the local CLI and HTTPS sites and `webui/dist/standalone/` for local testing. The repository ships one offline copy in [webui-standalone/](../webui-standalone/InkTag-WebUI.html). Open its HTML and keep the adjacent `assets/`. Preparing a release candidate refreshes this copy from a local build when available. Installing and using the release requires no Node.js.

After building, return to the repository root and rerun the client installer to start the updated WebUI.

- On Windows, run `install_inktag.cmd`.
- On macOS or Linux, run `sh install_inktag.sh`.

If only the WebUI changed, use the [client wrapper](../cli/README.en.md) to run `webui restart --repo .`, or run `inktag-cli webui restart --repo .` once the CLI virtual environment is active. Add `--enable-tls` or your existing certificate options to keep using HTTPS.

## Rebuild the built-in page preview

A normal WebUI build verifies and uses the bundled preview WASM. After changing the shared calendar or clock renderer, activate [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) 4.0.18 on Linux, macOS or WSL. Run the following command from the repository root, then rebuild the WebUI.

```sh
python3 tools/emulator/build-native-font-preview.py --emcc emcc
```

`cli/src/inktag_cli/_assets/nativePreview.inputs.json` lists the C inputs, build command and checksums. The WebUI and CLI share the same preview asset.

## CLI installation and firmware builds

- The CLI requires Python 3.11 or later. To develop just the CLI, run `python -m pip install -e ./cli` from the repository root in your virtual environment. For the local WebUI and Find, follow the [client installation guide](../cli/README.en.md).
- Choose the SDK and compiler for your MCU. Run `make` from the repository root and set `BUILD_TARGET` to select a platform. See the [firmware build guide](build-firmware.en.md) for per-target parameters and outputs, and the [flashing guide](en/flash.md) for selecting an OTA or complete package.
