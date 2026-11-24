# Public tooling

- `findmy/`: local-client and Find query installation/authentication/UI resources required for local use.
- `nfc/`: PN532/PCR532 NFC reads, writes, text commands and image transfer.
- `platforms/<vendor>/<controller>/programmer/`: source and board presets for platform flashing tools.
- `sdk/`: shared SDK preparation, resource generation and firmware build tooling.
- Root Python scripts generate manuals and feeds and verify attachments and Pages. Publication runs from the maintainer repository.
- `tests/`: offline regressions for public maintenance scripts, without device-development diagnostics.

For installation, use the programmer bundled with your firmware in `release/firmware/<controller_board>/`. [Complete flashing guide](../docs/en/flash.md)
