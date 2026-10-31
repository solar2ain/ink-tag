# InkTag firmware and flashing tools

[中文](README.md) | English

Choose the exact controller/board family. Each target has `ota/` for wireless updates and `full/` for first installation or wired recovery; these are not interchangeable.

Targets: `gr5331_zkel`, `gr551x_zkel`, `gr551x_zkel_internal`, `om6626_zkel`, `tlsr8359_hsel`, `nrf52832_priceetag`, `nrf52811_aesl`, `da14585_aesl`.

The repository includes collected firmware. GitHub Releases carries attachments with identical bytes. Check `manifest.json` for board identity, size and SHA256. A directory does not imply support for every board using that controller.

Each target's bilingual README lists products, PCB markings, configurations and complete one-line flashing commands. See [complete flashing](../docs/en/flash.md) for the shared directory layout. Tools still require an explicit `-Firmware` argument and are not tied to a particular firmware version.
