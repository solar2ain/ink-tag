# Board configuration

[中文](README.md) | English

`EL022HWRL.toml` targets the nRF52811 EL022HWRL board (CAL_VER06) with its original 2.13-inch black/white/red panel. `EL022HWRL-A0.toml` targets the same board with an A0 panel.

The complete tools package selects this configuration by default. In a source checkout, specify it explicitly:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/ports/nordic/nrf52811/tools/flash.ps1 -Firmware firmware-full.zip -Config firmware/ports/nordic/nrf52811/tools/configs/EL022HWRL.toml -DryRun
```

This validates the complete firmware and configuration without opening a probe. The same precompiled complete package accepts different display and RGB pin configurations without recompilation. Existing devices can gain pin configuration through App OTA while retaining valid legacy preferences. App OTA does not create or replace factory defaults.

`[gpio.display]` configures MOSI, SCLK (`clock`), CS, DC, RESET, BUSY and display EN using `P0.xx`. The first six must all be connected or all set to `NONE`. EN is optional and must also be `NONE` when the display is disabled. Connected pins must be distinct and avoid crystal, external Flash, BS and reset resources. Signal polarity belongs to the controller/recipe; The required `enable_active_low` boolean configures EN polarity only (`false` for active high, `true` for active low). The display section rejects the old `active_low` list.

The device uses persisted WebUI overrides first, complete-programming factory defaults next, and compiled board defaults only when the factory record is confirmed missing. The WebUI can read default and effective configurations, modify pins and restore defaults. Successful changes take effect immediately and persist. Wiring changes are refused while display, image transfer or LED testing is active. Clearing settings removes the user override and restores defaults; clearing images preserves pins. Application OTA retains existing factory and user wiring records. Corrupt or unreadable records are never treated as missing.

`[gpio.led]` configures red, green and blue routes and each channel's `active_low` polarity. Set a disconnected channel to `NONE` and remove it from `active_low`. Defaults match the original EL022HWRL wiring; Recovery indications use the qualified fixed board routes. Identity, actual-capacity, signature and readback checks remain required.

The sections follow the main Nordic configuration: `[screen]`, `[gpio.display]`, and `[gpio.led]`. `note = "CAL_VER06"` records the board note; the tool derives the MCU, firmware family and native encoding from the marking.
