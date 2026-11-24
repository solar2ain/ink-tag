# Common firmware code

Shared code for InkTag:

- `gui/`: rendering, fonts, calendar/lunar logic, and emulator-friendly drawing code.
- `epd/`: e-paper model catalog, controller flows, image write/refresh logic, and the current EPD BLE command handler.
- `protocol/`: MCU-independent capability gates and protocol-wide policy.
- `lifecycle/`: validation and effect planning for reset, data clearing, OTA and sleep actions.
- `platform/`: the cross-chip HAL boundary for vendor adapters.
- `security/`: signed activation plus the single table-driven base/Pro access
  decision, quota and visibility policy.
- `input/`, `ble/`, `power/`, `status/`, and `ota/`: reusable product
  algorithms and state machines.

Every new feature is planned across the common, vendor-family and exact-board
layers described in `docs/firmware-feature-layers.md`. Pure product behavior
belongs here even when the same change only builds one vendor family. Some
older EPD paths still include Nordic headers; treat those dependencies as an
extraction seam rather than copying Nordic APIs into another SDK.
