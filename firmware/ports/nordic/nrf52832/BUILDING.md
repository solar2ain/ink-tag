# nRF52832 PriceETag Basic build

The board target is `nrf52832_priceetag`. Supply Nordic nRF5 SDK 17.1.0
under `.sdk/nordic/nRF5_SDK_17.1.0_ddde560` and Arm GNU Toolchain
15.2.Rel1 (GCC 15.2.1) under
`.tools/arm-gnu-toolchain/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi`.
Use Linux or Windows with WSL and Python 3.10 or later.

From the repository root, build and generate local packages in a new directory:

```sh
python tools/sdk/build-nrf52832-base.py --output _build/nrf52832-basic-0.2.2
```

The script builds App and Common Recovery using the Basic make entries and
verifies the ELF, BIN and HEX before packaging App OTA and an unbound Full
template. The unsigned OTA requires Recovery reporting integrity policy E1;
the version string alone does not establish OTA compatibility. Use the
guarded native tools to prepare a per-device Full image from the template.
