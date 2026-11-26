# OM6626 flashing tools for Windows

Tools version **0.2.0.20**. Install SEGGER J-Link/J-Flash 7.88j and run [flash.bat](tools/flash.bat). The tool uses the included [OM6626 device support files](device-pack/SOURCE.md) and prompts you to select the exact PCB marking.

[Board selection and flashing](../INSTALL.en.md) · [简体中文](README.md)

Keep `payload/` beside `records/`. Back up any data you need before a full flash. The BAT verifies the included tools. Use `-DryRun` to check a firmware and board configuration offline. See the [address guide](ADDRESSES.md) for flash regions.
